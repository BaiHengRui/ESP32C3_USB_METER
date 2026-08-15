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
    Serial.println("\n====== 串口命令帮助 ======");
    Serial.println("发送类型:      <command>:<value>");
    Serial.println("brightness:<1-100>  -设置亮度");
    Serial.println("rotation:<0-3>      -设置屏幕方向(1/3)");
    Serial.println("sample:<0-2>        -设置采样率(0:Fast/1:Normal/2:Slow)");
    Serial.println("sample:<fast>/<normal>/<slow> -设置采样率");
    Serial.println("set_start=<mV>,<mA> -起始阈值(电压mV,电流mA), 0=无限制");
    Serial.println("set_end=<mV>,<mA>   -结束阈值(电压mV,电流mA), 0=无限制");
    Serial.println("threshold           -查看当前阈值/计时状态");
    Serial.println("info                -设备信息");
    Serial.println("restart             -重启");
    Serial.println("data                -发送数据包");
    Serial.println("help                -显示此帮助信息");
    Serial.println("=========================\n");
}

static void handle_data(const char* param) {
    (void)param;

    HAL::USB_CDC_Data tx;
    tx.header       = 0xAA;
    tx.pack_length  = sizeof(tx);
    tx.snid         = SNID;

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