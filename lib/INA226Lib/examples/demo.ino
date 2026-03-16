#include <Wire.h>
#include "INA226.h"          // INA226 库头文件

INA226 ina(Wire);            // 创建 INA226 对象，使用默认 Wire

void setup() {
    Serial.begin(115200);
    Wire.begin();            // 初始化 I2C 总线

    // 尝试初始化 INA226，默认地址 0x40
    if (!ina.begin()) {
        Serial.println("Failed to find INA226");
        while (1);           // 无限循环，停止执行
    }

    // 配置器件参数：16 次平均值，总线转换时间 1100µs，分流转换时间 1100µs，连续分流+总线模式
    // INA226 无温度测量，因此不需要温度转换时间参数
    ina.configure(INA226_AVERAGES_16,
                  INA226_CONV_TIME_1100US,
                  INA226_CONV_TIME_1100US,
                  INA226_MODE_SHUNT_BUS_CONT);

    // 校准：分流电阻 0.01Ω，最大预期电流 5A
    if (!ina.calibrate(0.01, 5.0)) {
        Serial.println("Calibration failed");
    }

    Serial.print("Manufacturer ID: 0x");
    Serial.println(ina.getManufacturerID(), HEX);
    Serial.print("Die ID: 0x");
    Serial.println(ina.readDeviceID(), HEX);
}

void loop() {
    // 读取各项数据
    float busV     = ina.readBusVoltage();        // 总线电压，单位 V
    float shuntV   = ina.readShuntVoltage();      // 分流电压，单位 V
    float current  = ina.readCurrent();      // 电流，单位 A
    float power    = ina.readPower();          // 功率，单位 W

    // 打印数据（分流电压转换为 mV 显示）
    Serial.print("Bus: "); Serial.print(busV, 3); Serial.print(" V, ");
    Serial.print("Shunt: "); Serial.print(shuntV * 1000, 2); Serial.print(" mV, ");
    Serial.print("Current: "); Serial.print(current, 3); Serial.print(" A, ");
    Serial.print("Power: "); Serial.print(power, 3); Serial.println(" W");

    delay(1000);   // 每秒更新一次
}