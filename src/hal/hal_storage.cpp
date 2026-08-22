// ============================================================
// SPIFFS 离线记录模块 (多条目 / 会话)
// - 每条记录 = 一次记录会话, 文件 /m%04u.dat
// - SW1 长按开始新条目, 再次长按停止并保存
// - 采集任务按 record_interval_s 周期入队, 存储任务负责落盘
// - 满员策略: 停止记录并 Toast 提示
// - 导出: 复用 HAL::USB_CDC_Data 包头, pack_type 0x01/0x00/0x02 分块流式传输
// ============================================================
#include "hal.h"
#include <cstring>
#include <cstdlib>

using namespace HAL;

#define STORAGE_RECORD_MAGIC 0x5A
#define STORAGE_FILE_VERSION 1
#define STORAGE_MAX_ENTRIES  64

// RAM 写缓冲: 攒够 N 条或超过刷新周期才写 flash, 降低 SPIFFS 磨损
#define STORAGE_BUF_RECORDS 16
#define STORAGE_FLUSH_US    (10000ULL * 1000ULL)   // 10s (微秒)

// 剩余空间低于该值(字节)视为已满, 停止记录
#define STORAGE_RESERVE_BYTES (8UL * 1024UL)

static Storage_Record sBuf[STORAGE_BUF_RECORDS];
static uint8_t  sBufCount    = 0;
static uint32_t sSeq         = 0;      // 当前条目内记录序号
static int64_t  sLastFlushUs = 0;

// 多条目状态
static volatile bool gRecording  = false;   // 是否正在记录
static uint32_t gNextIndex       = 1;       // 下一条目编号 (NVS entry_next)
static char     gCurPath[24]     = {0};     // 当前记录文件路径
static int64_t  gRecStartUs      = 0;       // 本次记录开始时刻

// 条目列表 (扫描 SPIFFS 得到)
typedef struct { uint32_t index; uint32_t records; uint32_t bytes; } EntryInfo;
static EntryInfo gEntries[STORAGE_MAX_ENTRIES];
static uint32_t  gEntryCount = 0;
static uint32_t  gSelected   = 0;         // 0-based 选中

static Storage_Info gInfo;                // 对外只读信息

// 前向声明
static void RefreshEntries();
static void UpdateInfo();

// 异或校验(不包含 checksum 字段本身)
static uint8_t xorChecksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) sum ^= data[i];
    return sum;
}

// 写入文件头(仅当文件为空)
static bool ensureFileHeader(File& f) {
    if (f.size() == 0) {
        Storage_FileHeader hdr;
        hdr.magic0      = 'M';
        hdr.magic1      = 'T';
        hdr.version     = STORAGE_FILE_VERSION;
        hdr.record_size = (uint8_t)sizeof(Storage_Record);
        hdr.reserved    = 0;
        return f.write((const uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr);
    }
    return true;
}

void HAL::Storage_Init()
{
    bool mounted_ok = SPIFFS.begin(true);
    if (!mounted_ok)
    {
        HAL::LOG_INFO("SPIFFS mount failed!");
    }

    xStorageQueue = xQueueCreate(STORAGE_BUF_RECORDS * 2, sizeof(Storage_Record));
    if (xStorageQueue == nullptr)
    {
        HAL::LOG_INFO("Storage Queue create fail");
        xStorageMutex = nullptr;
        return;
    }

    xStorageMutex = xSemaphoreCreateMutex();
    if (xStorageMutex == nullptr)
    {
        HAL::LOG_INFO("Storage Mutex create fail");
        vQueueDelete(xStorageQueue);
        xStorageQueue = nullptr;
        return;
    }

    sBufCount   = 0;
    sSeq        = 0;
    gRecording  = false;
    storage_full   = false;
    storage_exporting = false;

    // 旧版单文件迁移: 仅尝试执行一次(NVS标记)，之后不再访问SPIFFS
    if (HAL::Sys_NVS_Read("migrated", 0) == 0)
    {
        if (mounted_ok)
        {
            if (SPIFFS.remove("/meter.dat"))
            {
                HAL::LOG_INFO("Removed legacy /meter.dat");
            }
            else
            {
                HAL::LOG_INFO("Legacy /meter.dat not found or remove failed");
            }
        }
        else
        {
            HAL::LOG_INFO("SPIFFS not mounted, skip meter.dat migrate");
        }
        //无论删除结果如何，打上已迁移标记，后续开机不再进入此分支
        HAL::Sys_NVS_Write("migrated", 1);
    }

    if(mounted_ok)
    {
        RefreshEntries();

        if (SPIFFS.usedBytes() >= SPIFFS.totalBytes() - STORAGE_RESERVE_BYTES)
        {
            storage_full = true;
        }

        HAL::LOG_INFO("Storage init: %lu entries, %lu records, %lu/%u bytes",
                (unsigned long)gEntryCount,
                (unsigned long)gInfo.total_records,
                (unsigned long)gInfo.total_bytes,
                (unsigned)SPIFFS.totalBytes());
    }
    else
    {
        // 文件系统挂载失败，标记存储空间已满禁止写入
        storage_full = true;
        HAL::LOG_INFO("Storage init skipped, SPIFFS mount error");
    }
}


// 生成条目文件路径 /m%04u.dat
static void entryPath(uint32_t index, char* buf, size_t len) {
    snprintf(buf, len, "/m%04u.dat", (unsigned)index);
}

// 解析条目路径 [可选 '/']m<digits>.dat → 编号
static bool parseEntryIndex(const char* name, uint32_t* out) {
    if (!name) return false;
    if (name[0] == '/') name++;          // openNextFile() 返回无前导 '/', open/exists 用带 '/'
    if (name[0] != 'm') return false;
    size_t len = strlen(name);
    if (len < 6) return false;           // "m1.dat" 最小
    if (strcmp(name + len - 4, ".dat") != 0) return false;
    uint32_t v = (uint32_t)atoi(name + 1);
    if (v == 0) return false;            // 排除 meter.dat 等非纯数字名
    if (out) *out = v;
    return true;
}

static uint32_t recordsOfSize(size_t sz) {
    if (sz <= sizeof(Storage_FileHeader)) return 0;
    return (uint32_t)((sz - sizeof(Storage_FileHeader)) / sizeof(Storage_Record));
}

// 更新对外只读信息 (不含 total_records/total_bytes, 由 RefreshEntries 赋值)
static void UpdateInfo() {
    gInfo.recording = gRecording;
    gInfo.entry_count = gEntryCount;
    gInfo.selected    = gSelected;
    if (gEntryCount > 0 && gSelected < gEntryCount) {
        gInfo.sel_index   = gEntries[gSelected].index;
        gInfo.sel_records = gEntries[gSelected].records;
        gInfo.sel_bytes   = gEntries[gSelected].bytes;
    } else {
        gInfo.sel_index   = 0;
        gInfo.sel_records = 0;
        gInfo.sel_bytes   = 0;
    }
    gInfo.rec_elapsed_sec = (gRecording && gRecStartUs > 0)
        ? (uint32_t)((esp_timer_get_time() - gRecStartUs) / 1000000ULL) : 0;
}

// 扫描 SPIFFS 条目, 重建列表并更新统计
static void RefreshEntries() {
    if (xStorageMutex) xSemaphoreTake(xStorageMutex, portMAX_DELAY);

    gEntryCount = 0;
    uint32_t totalRecords = 0, totalBytes = 0;

    File root = SPIFFS.open("/");
    File f = root.openNextFile();
    while (f && gEntryCount < STORAGE_MAX_ENTRIES) {
        uint32_t idx = 0;
        if (parseEntryIndex(f.name(), &idx)) {
            size_t sz = f.size();
            gEntries[gEntryCount].index   = idx;
            gEntries[gEntryCount].bytes   = (uint32_t)sz;
            gEntries[gEntryCount].records = recordsOfSize(sz);
            totalRecords += gEntries[gEntryCount].records;
            totalBytes   += gEntries[gEntryCount].bytes;
            gEntryCount++;
        }
        f = root.openNextFile();
    }

    // 按编号升序排序(插入排序, 条目少)
    for (uint32_t i = 1; i < gEntryCount; i++) {
        EntryInfo key = gEntries[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && gEntries[j].index > key.index) {
            gEntries[j + 1] = gEntries[j];
            j--;
        }
        gEntries[j + 1] = key;
    }

    if (gSelected >= gEntryCount) gSelected = (gEntryCount > 0) ? gEntryCount - 1 : 0;

    // 下一条目编号 = 现有最大编号 + 1 (全删后自动回到 1, 不沿用旧计数)
    gNextIndex = (gEntryCount > 0) ? (gEntries[gEntryCount - 1].index + 1) : 1;

    if (xStorageMutex) xSemaphoreGive(xStorageMutex);

    gInfo.total_records = totalRecords;
    gInfo.total_bytes   = totalBytes;
    UpdateInfo();
}

const Storage_Info& HAL::Storage_GetInfo() {
    UpdateInfo();
    return gInfo;
}

bool HAL::Storage_IsFull() {
    return storage_full;
}

void HAL::Storage_Flush() {
    if (sBufCount == 0) return;
    if (storage_exporting || !gRecording || gCurPath[0] == 0) return;

    if (xStorageMutex && xSemaphoreTake(xStorageMutex, portMAX_DELAY) != pdTRUE) return;
    // 双重检查(必要): 等待锁期间 storage_exporting/gRecording/sBufCount 可能已被其他任务改变。
    // storage_exporting 在 Storage_Export 中不持锁写入; gRecording 在 Storage_Start/Stop 中不持锁写入。
    if (storage_exporting || !gRecording || sBufCount == 0) {
        if (xStorageMutex) xSemaphoreGive(xStorageMutex);
        return;
    }

    File f = SPIFFS.open(gCurPath, FILE_APPEND);
    if (!f) {
        if (xStorageMutex) xSemaphoreGive(xStorageMutex);
        return;
    }

    ensureFileHeader(f);
    f.write((const uint8_t*)sBuf, sBufCount * sizeof(Storage_Record));
    f.close();

    sBufCount = 0;
    sLastFlushUs = esp_timer_get_time();

    // 满员检测: 剩余空间不足时停止记录
    if (SPIFFS.usedBytes() >= SPIFFS.totalBytes() - STORAGE_RESERVE_BYTES) {
        storage_full = true;
    }

    if (xStorageMutex) xSemaphoreGive(xStorageMutex);

    RefreshEntries();  // 更新条目统计
}

// 采集任务周期调用: 按 record_interval_s 采样入队 (仅在记录中)
void HAL::Storage_Sample() {
    if (record_interval_s == 0 || storage_full || storage_exporting || !gRecording) return;

    static int64_t lastUs = 0;
    int64_t now = esp_timer_get_time();
    if (now - lastUs < (int64_t)record_interval_s * 1000000LL) return;
    lastUs = now;

    Storage_Record rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic             = STORAGE_RECORD_MAGIC;
    rec.seq               = sSeq++;
    rec.timestamp_us      = (uint64_t)esp_timer_get_time();
    rec.voltage           = INA.voltage;
    rec.current           = INA.current;
    rec.power             = INA.power;
    rec.temperature       = INA.temperature;
    rec.temperature_cpu   = HAL::Get_CPU_Temperature();
    rec.energy_mWh        = INA.energy_mWh;
    rec.charge_mAh        = INA.charge_mAh;
    rec.current_direction = INA.current_direction;
    rec.checksum          = xorChecksum((const uint8_t*)&rec, sizeof(rec) - 1);

    if (xStorageQueue) xQueueSend(xStorageQueue, &rec, 0); // 满则丢弃(采样率低, 可接受)
}

// 存储任务主体: 阻塞接收队列, 攒批落盘; 超时则定期刷新
void HAL::Storage_Task() {
    Storage_Record rec;
    TickType_t timeout = pdMS_TO_TICKS(1000);

    if (xStorageQueue && xQueueReceive(xStorageQueue, &rec, timeout) == pdTRUE) {
        // 缓冲写入加锁, 避免与 Storage_Flush(按钮/阈值自动触发) 并发读写出错
        if (xStorageMutex) xSemaphoreTake(xStorageMutex, portMAX_DELAY);
        sBuf[sBufCount++] = rec;
        bool needFlush = (sBufCount >= STORAGE_BUF_RECORDS);
        if (xStorageMutex) xSemaphoreGive(xStorageMutex);
        if (needFlush) HAL::Storage_Flush();
    } else {
        // 队列超时: 若有未落盘数据则刷新(保证尾批不滞留)
        int64_t now = esp_timer_get_time();
        if (sBufCount > 0 && (now - sLastFlushUs >= STORAGE_FLUSH_US)) {
            HAL::Storage_Flush();
            sLastFlushUs = now;
        }
    }
}

// 单条记录 -> USB_CDC_Data 分块包
static void sendExportChunk(const Storage_Record* rec, uint32_t idx, uint32_t total) {
    HAL::USB_CDC_Data tx;
    memset(&tx, 0, sizeof(tx));
    tx.header0 = 0x55;
    tx.header1 = 0xAA;

    if (total == 1)          tx.pack_type = 0x02;  // 仅一条: 结束标记
    else if (idx == 0)       tx.pack_type = 0x01;  // 首块
    else if (idx == total - 1) tx.pack_type = 0x02; // 末块
    else                     tx.pack_type = 0x00;  // 中间块

    tx.pack_index        = (uint16_t)(idx & 0xFFFF);
    tx.pack_length       = sizeof(tx);
    tx.temperature_cpu   = rec->temperature_cpu;
    tx.temperature_adc   = rec->temperature;
    tx.voltage           = rec->voltage;
    tx.current           = rec->current;
    tx.power             = rec->power;
    tx.energy_mWh        = rec->energy_mWh;
    tx.charge_mAh        = rec->charge_mAh;
    tx.esp_time_us       = rec->timestamp_us;
    tx.current_direction = rec->current_direction;

    uint8_t* bytes = (uint8_t*)&tx;
    tx.checksum = xorChecksum(bytes, sizeof(tx) - 1);

    // 流控: 等待发送缓冲可用
    while (Serial.availableForWrite() < (int)sizeof(tx)) vTaskDelay(1);
    Serial.write(bytes, sizeof(tx));
}

// 分块导出: 复用 USB_CDC_Data 包头, pack_type 0x01(首)/0x00(中)/0x02(末)
bool HAL::Storage_Export(const char* filename) {
    if (storage_exporting) return false;

    HAL::Storage_Flush();                 // 先落盘

    char path[24];
    if (filename && filename[0]) {
        strncpy(path, filename, sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
    } else {
        RefreshEntries();
        if (gEntryCount == 0) {
            Serial.println("导出失败: 无条目");
            return false;
        }
        entryPath(gEntries[gSelected].index, path, sizeof(path));
    }

    if (!SPIFFS.exists(path)) {
        Serial.println("导出失败: 文件不存在");
        return false;
    }

    File f = SPIFFS.open(path, "r");
    if (!f) {
        Serial.println("导出失败: 打开文件错误");
        return false;
    }
    size_t total = f.size();
    f.close();

    if (total <= sizeof(Storage_FileHeader)) {
        Serial.println("导出: 无记录");
        return true;
    }

    uint32_t count = (uint32_t)((total - sizeof(Storage_FileHeader)) / sizeof(Storage_Record));

    storage_exporting = true;
    Serial.printf("EXPORT:%s:%u:%u\n", path, (unsigned)total, (unsigned)count);

    File fr = SPIFFS.open(path, "r");
    if (!fr) {
        storage_exporting = false;
        return false;
    }

    for (uint32_t i = 0; i < count; i++) {
        Storage_Record rec;
        uint32_t off = (uint32_t)sizeof(Storage_FileHeader) + i * (uint32_t)sizeof(Storage_Record);
        if (!fr.seek(off)) break;
        if (fr.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
        sendExportChunk(&rec, i, count);
        taskYIELD();
    }
    fr.close();

    storage_exporting = false;
    Serial.println("EXPORT_DONE");
    return true;
}

// 清空所有条目
bool HAL::Storage_Erase() {
    if (storage_exporting || gRecording) return false;

    char paths[STORAGE_MAX_ENTRIES][24];
    uint32_t n = 0;

    if (xStorageMutex) xSemaphoreTake(xStorageMutex, portMAX_DELAY);
    if (xStorageQueue) xQueueReset(xStorageQueue);
    sBufCount = 0;
    sSeq = 0;

    File root = SPIFFS.open("/");
    File f = root.openNextFile();
    while (f && n < STORAGE_MAX_ENTRIES) {
        uint32_t idx = 0;
        if (parseEntryIndex(f.name(), &idx)) {
            entryPath(idx, paths[n], 24);   // 重建带 '/' 的完整路径
            n++;
        }
        f = root.openNextFile();
    }
    // 同时清除旧版单文件
    SPIFFS.remove("/meter.dat");

    bool ok = true;
    for (uint32_t i = 0; i < n; i++) {
        if (!SPIFFS.remove(paths[i])) ok = false;
    }

    gSelected  = 0;
    storage_full = false;
    if (xStorageMutex) xSemaphoreGive(xStorageMutex);

    RefreshEntries();  // 重新计算 gNextIndex (无条目时 = 1)
    return ok;
}

// 开始新条目(内部, 供手动/阈值自动共用)
static Storage_Result Storage_Start() {
    if (storage_full) return SR_FULL;

    RefreshEntries();  // 重新计算 gNextIndex = 现有最大编号 + 1

    // 开始新条目
    if (xStorageMutex) xSemaphoreTake(xStorageMutex, portMAX_DELAY);
    entryPath(gNextIndex, gCurPath, sizeof(gCurPath));
    File f = SPIFFS.open(gCurPath, FILE_APPEND);
    if (f) {
        ensureFileHeader(f);
        f.close();
    }
    if (xStorageMutex) xSemaphoreGive(xStorageMutex);

    sSeq = 0;
    sBufCount = 0;
    gRecording = true;
    gRecStartUs = esp_timer_get_time();

    uint32_t created = gNextIndex;
    gNextIndex++;

    RefreshEntries();
    for (uint32_t i = 0; i < gEntryCount; i++) {
        if (gEntries[i].index == created) gSelected = i;
    }
    UpdateInfo();
    return SR_STARTED;
}

// 停止并保存(内部, 供手动/阈值自动共用)
static Storage_Result Storage_Stop() {
    HAL::Storage_Flush();
    gRecording = false;
    gRecStartUs = 0;

    // 若本条无数据则删除空条目
    File f = SPIFFS.open(gCurPath, "r");
    size_t sz = f ? f.size() : 0;
    if (f) f.close();
    if (recordsOfSize(sz) == 0) SPIFFS.remove(gCurPath);

    gCurPath[0] = 0;
    RefreshEntries();
    return SR_SAVED;
}

// SW1 长按: 开始/停止记录 (只返回结果, 不弹通知)
HAL::Storage_Result HAL::Storage_ToggleRecord() {
    if (storage_exporting) return SR_BUSY;
    return gRecording ? Storage_Stop() : Storage_Start();
}

// 阈值自动开始/停止记录 (采集任务周期调用)
void HAL::Storage_AutoControl() {
    if (!threshold_auto || storage_exporting) return;

    uint32_t voltage_mV = (uint32_t)(INA.voltage * 1000.0f);
    uint32_t current_mA = (uint32_t)(INA.current * 1000.0f);

    // 起始条件: 电压/电流均大于等于起始阈值(0=无限制); 需至少设置一项起始阈值
    bool hasStart = (thrStartVMv != 0) || (thrStartIMa != 0);
    bool startCondV = (thrStartVMv == 0) || (voltage_mV >= thrStartVMv);
    bool startCondI = (thrStartIMa == 0) || (current_mA >= thrStartIMa);
    bool shouldStart = hasStart && startCondV && startCondI;

    // 结束条件: 电压/电流均小于等于结束阈值(需同时设置两项结束阈值)
    bool endCondV = (thrEndVMv != 0) && (voltage_mV <= thrEndVMv);
    bool endCondI = (thrEndIMa != 0) && (current_mA <= thrEndIMa);
    bool shouldStop = endCondV && endCondI;

    if (!gRecording && shouldStart) {
        Storage_Start();
    } else if (gRecording && shouldStop) {
        Storage_Stop();
    }
}

// SW1 短按: 选中下一条目
void HAL::Storage_SelectNext() {
    RefreshEntries();
    if (gEntryCount == 0) return;
    gSelected = (gSelected + 1) % gEntryCount;
    UpdateInfo();
}

// SW1 双击: 删除选中条目 (只返回结果, 不弹通知)
HAL::Storage_Result HAL::Storage_DeleteSelected() {
    if (storage_exporting) return SR_BUSY;
    if (gRecording) return SR_RECORDING;
    RefreshEntries();
    if (gEntryCount == 0) return SR_EMPTY;

    char path[24];
    entryPath(gEntries[gSelected].index, path, sizeof(path));

    bool ok;
    if (xStorageMutex) xSemaphoreTake(xStorageMutex, portMAX_DELAY);
    ok = SPIFFS.remove(path);
    if (xStorageMutex) xSemaphoreGive(xStorageMutex);

    if (ok) {
        RefreshEntries();
        return SR_DELETED;
    }
    return SR_ERROR;
}
