#include <Wire.h>
#include "INA226.h"

INA226 ina(Wire);

void setup() {
    Serial.begin(115200);
    Wire.begin();

    if (!ina.begin()) {
        Serial.println("Failed to find INA226");
        while (1);
    }
    // Configure with 16 averages, 1100 µs conversion times, continuous mode
    ina.configure(INA226_AVERAGES_16,
                  INA226_CONV_TIME_1100US,
                  INA226_CONV_TIME_1100US,
                  INA226_MODE_SHUNT_BUS_CONT);

    // Calibrate with 0.1 ohm shunt and max current 2A
    if (!ina.calibrate(0.01, 5.0)) {
        Serial.println("Calibration failed");
    }

    // Read IDs
    Serial.print("Manufacturer ID: 0x");
    Serial.println(ina.getManufacturerID(), HEX);
    Serial.print("Die ID: 0x");
    Serial.println(ina.readDeviceID(), HEX);
}

void loop() {
    float busV     = ina.readBusVoltage();
    float shuntV   = ina.readShuntVoltage();
    float current  = ina.readCurrent();
    float power    = ina.readPower();

    Serial.print("Bus: "); Serial.print(busV, 3); Serial.print(" V, ");
    Serial.print("Shunt: "); Serial.print(shuntV * 1000, 2); Serial.print(" mV, ");
    Serial.print("Current: "); Serial.print(current, 3); Serial.print(" A, ");
    Serial.print("Power: "); Serial.print(power, 3); Serial.println(" W");

    delay(1000);
}