#include "hal.h"

HAL::USB_CDC_Data USB_CDC_Data;

static void handle_brightness(const String& param);
static void handle_rotation(const String& param);
static void handle_sample(const String& param);
static void handle_info();
static void handle_restart();
static void handle_help();
static void handle_data();

// 命令映射表项
struct CommandEntry {
    const char* cmd;          // 命令字符串，如 "brightness"
    void (*handler)(const String&); // 处理函数，参数为冒号后面的内容（若无参数则传入空串）
    bool has_param;           // 是否需要参数
};

// 静态映射表
static const CommandEntry cmdTable[] = {
    { "brightness:", handle_brightness, true },
    { "rotation:",   handle_rotation,   true },
    { "sample:",     handle_sample,     true },
    { "info",        [](const String&){ handle_info(); }, false },
    { "restart",     [](const String&){ handle_restart(); }, false },
    { "help",        [](const String&){ handle_help(); }, false },
    { "data",        [](const String&){ handle_data(); }, false },
};
static const int cmdCount = sizeof(cmdTable) / sizeof(cmdTable[0]);
static void handle_brightness(const String& param) {
    int value = param.toInt();
    if (value >= 1 && value <= 100) {
        HAL::LCD_SetBrightness(value);
        Serial.print("亮度已设置为: ");
        Serial.println(value);
        HAL::Sys_NVS_Write("light", value);
        Serial.print("设置已保存");
    } else {
        Serial.println("错误: 亮度值必须为1-100");
    }
}

static void handle_rotation(const String& param) {
    int value = param.toInt();
    if (value >= 0 && value <= 3) {
        HAL::LCD_SetRotation(value);
        Serial.print("屏幕方向已设置为: ");
        Serial.println(value);
        HAL::Sys_NVS_Write("rotation", value);
        Serial.println("设置已保存");
    } else {
        Serial.println("错误: 屏幕方向值必须为1/3");
    }
}

static void handle_sample(const String& param) {
    String p = param;
    p.trim();
    int value = -1;
    if (p.equalsIgnoreCase("fast") || p.equals("0")) value = 0;
    else if (p.equalsIgnoreCase("normal") || p.equals("1")) value = 1;
    else if (p.equalsIgnoreCase("slow") || p.equals("2")) value = 2;
    else value = p.toInt();

    if (value >= 0 && value <= 2) {
        HAL::INA22x_SetConfig(value);
        HAL::Sys_NVS_Write("sample_mode", value);
        const char* mode_str[] = {"0/Fast", "1/Normal", "2/Slow"};
        Serial.print("采样率已设置为: ");
        Serial.println(mode_str[value]);
        Serial.println("设置已保存");
    } else {
        Serial.println("错误: 采样率值必须为 0(Fast)/1(Normal)/2(Slow) 或 fast/normal/slow");
    }
}

static void handle_info() {
    HAL::LOG_INFO("设备信息：");
    Serial.println("运行时间: " + String(HAL::Get_System_RunTime(millis())));
    Serial.println("启动时间: " + String(startTime));
    Serial.println("CPU温度: " + String(HAL::Get_CPU_Temperature()));
    Serial.println("可用RAM: " + String(ESP.getFreeHeap()));
    Serial.println("SDK版本: " + String(ESP.getSdkVersion()));
    Serial.println("HW: " + String(HARDWARE_VERSION));
    Serial.println("SW: " + String(SOFTWARE_VERSION));
    Serial.println("SN: " + String(SNID,HEX));
    Serial.println("MD5: " + String(ESP.getSketchMD5()));
    Serial.println("状态: " + String(HAL::Get_System_Status()));
    Serial.println("屏幕亮度: " + String(HAL::Sys_NVS_Read("light", defaultBrightness)));
    uint8_t current_sample = HAL::Sys_NVS_Read("sample_mode", sample_mode);
    if (current_sample > 2) current_sample = 0;
    const char* sample_str[] = {"Fast", "Normal", "Slow"};
    Serial.println("采样率: " + String(sample_str[current_sample]));
}

static void handle_restart() {
    Serial.println("ESP Restart!");
    ESP.restart();
}

static void handle_help() {
    Serial.println("\n====== 串口命令帮助 ======");
    Serial.println("发送类型:      <command>:<value>");
    Serial.println("brightness:<1-100>  -设置亮度");
    Serial.println("rotation:<0-3>      -设置屏幕方向(1/3)");
    Serial.println("sample:<0-2>        -设置采样率(0:Fast/1:Normal/2:Slow)");
    Serial.println("sample:<fast>/<normal>/<slow> -设置采样率");
    Serial.println("info                -设备信息");
    Serial.println("restart             -重启");
    Serial.println("data                -发送二进制数据包");
    Serial.println("help                -显示此帮助信息");
    Serial.println("=========================\n");
}

static void handle_data() {
    HAL::INA22x_Data ina;
    HAL::INA22x_GetData(&ina);

    // 检查温度是否异常
    if (ina.temperature > 100.0f || ina.temperature < -50.0f) {
        Serial.print("警告: 温度读数异常: ");
        Serial.println(ina.temperature);
    }

    HAL::USB_CDC_Data tx;
    tx.header = 0xAA;
    tx.voltage = ina.voltage;
    tx.current = ina.current;
    tx.power = ina.power;
    tx.energy_mWh = ina.energy_mWh;
    tx.charge_mAh = ina.charge_mAh;
    tx.energy_Wh = ina.energy_Wh;
    tx.charge_Ah = ina.charge_Ah;
    tx.temperature = ina.temperature;
    tx.time_ms = millis();
    tx.current_direction = ina.current_direction ? 1 : 0;

    // 计算校验和
    uint8_t* bytes = (uint8_t*)&tx;
    uint8_t sum = 0;
    for (size_t i = 0; i < sizeof(tx) - 1; i++) {
        sum ^= bytes[i];
    }
    tx.checksum = sum;

    Serial.write(bytes, sizeof(tx));
}

void HAL::UART_Command() {
    if (Serial.available() <= 0) return;

    String message = "";
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '\n') break;
        message += c;
    }
    message.trim();
    if (message.length() == 0) return;

    // 遍历命令表，寻找匹配的命令前缀
    bool found = false;
    for (int i = 0; i < cmdCount; ++i) {
        const CommandEntry& entry = cmdTable[i];
        if (message.startsWith(entry.cmd)) {
            String param = "";
            if (entry.has_param) {
                param = message.substring(strlen(entry.cmd));
                param.trim();
            }
            entry.handler(param);
            found = true;
            break;
        }
    }

    if (!found) {
        Serial.print("未知命令: ");
        Serial.println(message);
        Serial.println("输入 'help' 查看可用命令");
    }
}