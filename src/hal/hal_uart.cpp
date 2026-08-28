#include "hal.h"
#include <cstring>
#include <cstdlib>

HAL::USB_CDC_Data USB_CDC_Data;

// ============================================================
// 命令处理函数（参数用 const char*，零堆分配）
// ============================================================
static void handle_brightness(const char* param);
static void handle_rotation(const char* param);
static void handle_sample(const char* param);
static void handle_set_start(const char* param);
static void handle_set_end(const char* param);
static void handle_threshold_show(const char* param);
static void handle_info(const char* param);
static void handle_restart(const char* param);
static void handle_help(const char* param);
static void handle_data(const char* param);
static void handle_record(const char* param);
static void handle_export_list(const char* param);
static void handle_export_erase(const char* param);
static void handle_export(const char* param);
static void handle_rec_enable(const char* param);
static void handle_rec_start(const char* param);
static void handle_rec_stop(const char* param);

// ============================================================
// 命令映射表
// ============================================================
struct CmdEntry {
    const char* name;
    void (*handler)(const char*);
    bool has_param;
};

static const CmdEntry cmdTable[] = {
    { "brightness:", handle_brightness,    true  },
    { "rotation:",   handle_rotation,      true  },
    { "sample:",     handle_sample,        true  },
    { "set_start=",  handle_set_start,     true  },
    { "set_end=",    handle_set_end,       true  },
    { "threshold",   handle_threshold_show,false },
    { "info",        handle_info,          false },
    { "restart",     handle_restart,       false },
    { "help",        handle_help,          false },
    { "data",        handle_data,          false },
    { "record:",     handle_record,        true  },
    { "rec:enable",  handle_rec_enable,    true  },
    { "rec:start",   handle_rec_start,     false },
    { "rec:stop",    handle_rec_stop,      false },
    { "export:list", handle_export_list,   false },
    { "export:erase",handle_export_erase,  false },
    { "export",      handle_export,        true  },
};
static const int cmdCount = sizeof(cmdTable) / sizeof(cmdTable[0]);

// ============================================================
// 命令处理实现
// ============================================================
static void handle_brightness(const char* param) {
    int value = atoi(param);
    if (value >= 1 && value <= 100) {
        HAL::LCD_SetBrightness(value);
        Serial.printf("亮度已设置为: %d\n设置已保存\n", value);
        HAL::Sys_NVS_Write("light", value);
    } else {
        Serial.println("错误: 亮度值必须为1-100");
    }
}

static void handle_rotation(const char* param) {
    int value = atoi(param);
    if (value >= 0 && value <= 3) {
        HAL::LCD_SetRotation(value);
        Serial.printf("屏幕方向已设置为: %d\n设置已保存\n", value);
        HAL::Sys_NVS_Write("rotation", value);
    } else {
        Serial.println("错误: 屏幕方向值必须为0-3");
    }
}

static void handle_sample(const char* param) {
    // 跳过前导空格
    while (*param == ' ') param++;

    int value = -1;
    if ((param[0] == 'f' || param[0] == 'F') && strcmp(param + 1, "ast") == 0)      value = 0;
    else if ((param[0] == 'n' || param[0] == 'N') && strcmp(param + 1, "ormal") == 0) value = 1;
    else if ((param[0] == 's' || param[0] == 'S') && strcmp(param + 1, "low") == 0)   value = 2;
    else if (strcmp(param, "0") == 0) value = 0;
    else if (strcmp(param, "1") == 0) value = 1;
    else if (strcmp(param, "2") == 0) value = 2;
    else value = atoi(param);

    if (value >= 0 && value <= 2) {
        HAL::INA22x_SetConfig(value);
        HAL::Sys_NVS_Write("sample_mode", value);
        sample_mode = value;
        const char* mode_str[] = {"0/Fast", "1/Normal", "2/Slow"};
        Serial.printf("采样率已设置为: %s\n设置已保存\n", mode_str[value]);
    } else {
        Serial.println("错误: 采样率值必须为 0(Fast)/1(Normal)/2(Slow) 或 fast/normal/slow");
    }
}

static void handle_set_start(const char* param) {
    // 格式: <mV>,<mA>  例如: 48000,5000
    while (*param == '<' || *param == ' ') param++;
    const char* comma = strchr(param, ',');
    uint32_t v = (uint32_t)atoi(param);
    uint32_t i = comma ? (uint32_t)atoi(comma + 1) : 0;

    thrStartVMv = v;
    thrStartIMa = i;
    HAL::Sys_NVS_WriteUInt("thr_sv", v);
    HAL::Sys_NVS_WriteUInt("thr_si", i);
    thrTimingActive = false;
    thrElapsedUs = 0;
    Serial.printf("起始阈值: %u mV %s / %u mA %s (已保存)\n",
        v, v == 0 ? "(无限制)" : "", i, i == 0 ? "(无限制)" : "");
}

static void handle_set_end(const char* param) {
    // 格式: <mV>,<mA>  例如: 4200,100
    while (*param == '<' || *param == ' ') param++;
    const char* comma = strchr(param, ',');
    uint32_t v = (uint32_t)atoi(param);
    uint32_t i = comma ? (uint32_t)atoi(comma + 1) : 0;

    thrEndVMv = v;
    thrEndIMa = i;
    HAL::Sys_NVS_WriteUInt("thr_ev", v);
    HAL::Sys_NVS_WriteUInt("thr_ei", i);
    thrTimingActive = false;
    thrElapsedUs = 0;
    Serial.printf("结束阈值: %u mV %s / %u mA %s (已保存)\n",
        v, v == 0 ? "(无限制)" : "", i, i == 0 ? "(无限制)" : "");
}

static void handle_threshold_show(const char* param) {
    (void)param;
    Serial.println("====== 计时阈值设置 ======");
    Serial.printf("起始电压: %u mV %s\n", thrStartVMv, thrStartVMv == 0 ? "(无限制)" : "");
    Serial.printf("起始电流: %u mA %s\n", thrStartIMa, thrStartIMa == 0 ? "(无限制)" : "");
    Serial.printf("结束电压: %u mV %s\n", thrEndVMv, thrEndVMv == 0 ? "(无限制)" : "");
    Serial.printf("结束电流: %u mA %s\n", thrEndIMa, thrEndIMa == 0 ? "(无限制)" : "");
    Serial.print("计时状态: ");
    Serial.println(thrTimingActive ? "计时中" : "已停止");
    Serial.print("计时结果: ");
    Serial.println(HAL::Get_Threshold_Time());
    Serial.println("==========================");
}

static void handle_info(const char* param) {
    (void)param;
    HAL::LOG_INFO("设备信息：");
    Serial.printf("上电运行时间: %s\n", HAL::Get_System_RunTime(esp_timer_get_time()).c_str());
    Serial.printf("上电启动时间: %d\n", startTime);
    Serial.printf("CPU温度: %.1f\n", HAL::Get_CPU_Temperature());
    Serial.printf("可用RAM: %d\n", ESP.getFreeHeap());
    Serial.printf("SDK版本: %s\n", ESP.getSdkVersion());
    Serial.printf("HW: %s\n", HARDWARE_VERSION);
    Serial.printf("SW: %s\n", SOFTWARE_VERSION);
    Serial.printf("INA: %04X\n", INA228_EN ? "228" : "226");
    Serial.printf("SN ID: %012llX\n", SNID);
    Serial.printf("Sketch MD5: %s\n", ESP.getSketchMD5().c_str());
    Serial.printf("状态: %s\n", HAL::Get_System_Status().c_str());
    Serial.printf("屏幕亮度: %d\n", HAL::Sys_NVS_Read("light", defaultBrightness));
    uint8_t current_sample = HAL::Sys_NVS_Read("sample_mode", sample_mode);
    if (current_sample > 2) current_sample = 0;
    const char* sample_str[] = {"Fast", "Normal", "Slow"};
    Serial.printf("采样率: %s\n", sample_str[current_sample]);
}

static void handle_restart(const char* param) {
    (void)param;
    Serial.println("ESP Restart!");
    ESP.restart();
}

static void handle_help(const char* param) {
    (void)param;
    // 合并为单次 printf 输出，减少逐行 Serial.println 的调用开销
    Serial.printf(
        "\n====== 串口命令帮助 ======\n"
        "发送类型:      <command>:<value>\n"
        "brightness:<1-100>  -设置亮度\n"
        "rotation:<0-3>      -设置屏幕方向(1/3)\n"
        "sample:<0-2>        -设置采样率(0:Fast/1:Normal/2:Slow)\n"
        "sample:<fast>/<normal>/<slow> -设置采样率\n"
        "set_start=<mV>,<mA> -起始阈值(电压mV,电流mA), 0=无限制\n"
        "set_end=<mV>,<mA>   -结束阈值(电压mV,电流mA), 0=无限制\n"
        "threshold           -查看当前阈值/计时状态\n"
        "info                -设备信息\n"
        "restart             -重启\n"
        "data                -发送数据包\n"
        "record:<0|1|5|10|30|60> -设置离线记录间隔(秒), 0=关闭\n"
        "rec:enable:<0|1>   -离线数据功能开关(0=关, 1=开)\n"
        "rec:start           -开始记录\n"
        "rec:stop            -停止记录\n"
        "export:list         -列出存储文件\n"
        "export:erase        -清除全部条目\n"
        "export[:<编号>]     -分块导出条目(默认当前选中条目)\n"
        "help                -显示此帮助信息\n"
        "=========================\n");
}

static void handle_data(const char* param) {
    (void)param;

    HAL::USB_CDC_Data tx;
    tx.header0       = 0x55;
    tx.header1       = 0xAA;
    tx.pack_type   = 0x00; // 单包
    tx.pack_index  = 0;
    tx.pack_length  = sizeof(tx);
    tx.temperature_cpu = HAL::Get_CPU_Temperature();
    tx.temperature_adc = INA.temperature;
    tx.voltage         = INA.voltage;
    tx.current         = INA.current;
    tx.power           = INA.power;
    tx.energy_mWh      = INA.energy_mWh;
    tx.charge_mAh      = INA.charge_mAh;
    tx.esp_time_us     = esp_timer_get_time();
    tx.current_direction = INA.current_direction;

    // 校验和
    uint8_t* bytes = (uint8_t*)&tx;
    uint8_t sum = 0;
    for (size_t i = 0; i < sizeof(tx) - 1; i++) {
        sum ^= bytes[i];
    }
    tx.checksum = sum;

    if (Serial.availableForWrite() >= (int)sizeof(tx)) {
        Serial.write(bytes, sizeof(tx));
    }
}

static void handle_record(const char* param) {
    int value = atoi(param);
    // 0=关闭; 非 0 时必须为分段间隔, 与"保存时间"菜单保持一致
    if (value != 0 && value != 1 && value != 5 && value != 10 && value != 30 && value != 60) {
        Serial.println("错误: record 值必须为 0(关闭) 或 1/5/10/30/60 (秒)");
        return;
    }
    record_interval_s = (uint32_t)value;
    HAL::Sys_NVS_WriteUInt("record_s", record_interval_s);
    if (record_interval_s == 0) {
        Serial.println("离线记录: 已关闭");
    } else {
        Serial.printf("离线记录间隔: %lu 秒 (已保存)\n", (unsigned long)record_interval_s);
    }
}

static void handle_rec_enable(const char* param) {
    // rec:enable:<0|1>  1=开启, 0=关闭 (与菜单"功能开关"一致)
    while (*param == ':' || *param == ' ') param++;
    if (*param == 0) {
        Serial.println("用法: rec:enable:<0|1>  (0=关, 1=开)");
        return;
    }
    int v = atoi(param);
    if (v == 0) {
        record_interval_s = 0;
        HAL::Sys_NVS_WriteUInt("record_s", record_interval_s);
        Serial.println("离线数据: 已关闭");
    } else {
        if (record_interval_s == 0) record_interval_s = 1;   // 恢复默认 1s
        HAL::Sys_NVS_WriteUInt("record_s", record_interval_s);
        Serial.printf("离线数据: 已开启 (间隔 %lu s)\n", (unsigned long)record_interval_s);
    }
}

static void handle_rec_start(const char* param) {
    (void)param;
    if (record_interval_s == 0) {
        Serial.println("开始失败: 离线数据已关闭 (先 rec:enable:1)");
        return;
    }
    if (HAL::Storage_GetInfo().recording) {
        Serial.println("已在记录中");
        return;
    }
    HAL::Storage_Result r = HAL::Storage_ToggleRecord();
    if (r == HAL::SR_STARTED)      { Serial.println("已开始记录"); HAL::ShowToast("开始"); }
    else if (r == HAL::SR_FULL)    { Serial.println("开始失败: 存储已满"); HAL::ShowToast("NVS Full!"); }
    else if (r == HAL::SR_BUSY)    { Serial.println("导出中, 请稍后"); }
    else                           { Serial.println("开始失败"); }
}

static void handle_rec_stop(const char* param) {
    (void)param;
    if (!HAL::Storage_GetInfo().recording) {
        Serial.println("未在记录中");
        return;
    }
    HAL::Storage_Result r = HAL::Storage_ToggleRecord();
    if (r == HAL::SR_SAVED)        { Serial.println("已停止并保存"); HAL::ShowToast("保存"); }
    else if (r == HAL::SR_BUSY)    { Serial.println("导出中, 请稍后"); }
    else                           { Serial.println("停止失败"); }
}

static void handle_export_list(const char* param) {
    (void)param;
    Serial.println("====== 存储文件列表 ======");
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    if (!file) {
        Serial.println("(无文件)");
    }
    while (file) {
        Serial.printf("  %s  %u bytes\n", file.name(), (unsigned)file.size());
        file = root.openNextFile();
    }
    Serial.printf("LittleFS 占用: %u / %u bytes\n", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
    Serial.println("==========================");
}

static void handle_export_erase(const char* param) {
    (void)param;
    if (HAL::Storage_Erase()) {
        Serial.println("全部条目已清除");
        HAL::ShowToast("数据已清除");
    } else {
        Serial.println("清除失败(记录中或导出中)");
    }
}

static void handle_export(const char* param) {
    HAL::ShowToast("导出中");

    // 支持 export[:<编号>] 导出指定条目, 否则导出当前选中条目
    while (*param == ':' || *param == ' ') param++;
    int idx = atoi(param);
    char path[24] = {0};
    if (idx > 0) {
        snprintf(path, sizeof(path), "/m%04u.dat", (unsigned)idx);
    }

    bool ok = HAL::Storage_Export(idx > 0 ? path : nullptr);
    HAL::ShowToast(ok ? "已导出" : "导出失败");
}

// ============================================================
// UART 命令分发（固定栈缓冲区 + strncmp，零 String 分配）
// ============================================================
void HAL::UART_Command() {
    if (Serial.available() <= 0) return;

    // 固定栈缓冲区读取一行，避免 String 堆分配
    char message[64] = {};
    int  len = 0;
    while (Serial.available() > 0 && len < (int)sizeof(message) - 1) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') break;
        message[len++] = c;
    }
    if (len == 0) return;

    // 遍历命令表，strncmp 匹配前缀
    for (int i = 0; i < cmdCount; ++i) {
        const CmdEntry& entry = cmdTable[i];
        size_t nameLen = strlen(entry.name);
        if (strncmp(message, entry.name, nameLen) == 0) {
            const char* param = entry.has_param ? (message + nameLen) : "";
            entry.handler(param);
            return;
        }
    }

    Serial.printf("未知命令: %s\n输入 'help' 查看帮助\n", message);
}