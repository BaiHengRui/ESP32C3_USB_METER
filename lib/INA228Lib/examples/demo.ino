#include <Wire.h>
#include "INA228.h"

INA228 ina(Wire);

void setup() {
    Serial.begin(115200);
    Wire.begin();
    
    if (!ina.begin()) {
        Serial.println("Failed to find INA228");
        while (1);
    }
    
    // Configure with 16 averages, 1052 µs conversion times, continuous mode
    ina.configure(INA228_AVERAGES_16, INA228_CONV_TIME_1052US,
                  INA228_CONV_TIME_1052US, INA228_CONV_TIME_1052US,
                  INA228_MODE_ALL_CONTINUOUS);
    
    // Calibrate with 0.01 ohm shunt and max current 5A
    if (!ina.calibrate(0.01, 5.0)) {
        Serial.println("Calibration failed");
    }
    
    // Read IDs
    Serial.print("Manufacturer ID: 0x");
    Serial.println(ina.readManufacturerID(), HEX);
    Serial.print("Device ID: 0x");
    Serial.println(ina.readDeviceID(), HEX);
}

void loop() {
    float busV = ina.readBusVoltage();
    float shuntV = ina.readShuntVoltage();
    float current = ina.readCurrent();
    float power = ina.readPower();
    float temp = ina.readTemperature();
    
    Serial.print("Bus: "); Serial.print(busV, 3); Serial.print(" V, ");
    Serial.print("Shunt: "); Serial.print(shuntV * 1000, 2); Serial.print(" mV, ");
    Serial.print("Current: "); Serial.print(current, 3); Serial.print(" A, ");
    Serial.print("Power: "); Serial.print(power, 3); Serial.print(" W, ");
    Serial.print("Temp: "); Serial.print(temp, 2); Serial.println(" °C");
    
    delay(1000);
}