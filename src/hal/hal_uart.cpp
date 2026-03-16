#include "hal.h"

void HAL::UART_Command(){
    if (Serial.available() <= 0) {
        return; // 没有数据
    }
    
    String message = "";
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '\n') {
            break; // 遇到换行符，停止读取
        }
        message += c;
    }
    
    message.trim();
    
    if (message.length() == 0) {
        return; // 空命令
    }

    //  控制亮度
    if (message.startsWith("brightness:")) {
        // 设置亮度
        String param = message.substring(11);
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
    //  控制方向
    else if (message.startsWith("rotation:")) {
        String param = message.substring(9);
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
    //  控制采样率
    else if (message.startsWith("sample:")) {
        String param = message.substring(7);
        param.trim();
        int value = -1;

        // 支持数字和文字两种输入
        if (param.equalsIgnoreCase("fast") || param.equals("0")) {
            value = 0;
        } else if (param.equalsIgnoreCase("normal") || param.equals("1")) {
            value = 1;
        } else if (param.equalsIgnoreCase("slow") || param.equals("2")) {
            value = 2;
        } else {
            value = param.toInt();
        }

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
    //  显示帮助
    else if (message.startsWith("info"))
    {
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
    else if (message.startsWith("restart"))
    {
        Serial.println("ESP Restart!");
        ESP.restart();
    }
    else if (message.equals("help"))
    {
        // 显示帮助
        Serial.println("\n====== 串口命令帮助 ======");
        Serial.println("发送类型:      <command>:<value>");
        Serial.println("brightness:<1-100>  -设置亮度");
        Serial.println("rotation:<0-3>      -设置屏幕方向(1/3)");
        Serial.println("sample:<0-2>        -设置采样率(0:Fast/1:Normal/2:Slow)");
        Serial.println("sample:<fast>/<normal>/<slow> -设置采样率");
        Serial.println("info                -设备信息");
        Serial.println("restart             -重启");
        Serial.println("help                -显示此帮助信息");
        Serial.println("=========================\n");
    }
    else {
        // 未知命令
        Serial.print("未知命令: ");
        Serial.println(message);
        Serial.println("输入 'help' 查看可用命令");
    }
}