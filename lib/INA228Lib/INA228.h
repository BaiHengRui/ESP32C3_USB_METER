/*
INA228.h - Header file for the INA228 High-Precision Current/Voltage/Power Monitor Arduino Library.

(c) 2026 BaiHengRui
Refer to TI INA228 Datasheet for details.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef INA228_h
#define INA228_h

#include "Arduino.h"
#include <Wire.h>


#define INA228_ADDRESS              (0x40)   // Default I2C address

// Register addresses (see Table 1 in datasheet)
// INA228 Datasheet 7.6.1
#define INA228_REG_CONFIG           (0x00)
#define INA228_REG_ADC_CONFIG       (0x01)
#define INA228_REG_SHUNT_CAL        (0x02)
#define INA228_REG_SHUNT_TEMPCO      (0x03)
#define INA228_REG_VSHUNT           (0x04)
#define INA228_REG_VBUS             (0x05)
#define INA228_REG_DIETEMP          (0x06)
#define INA228_REG_CURRENT          (0x07)
#define INA228_REG_POWER            (0x08)
#define INA228_REG_ENERGY           (0x09)
#define INA228_REG_CHARGE           (0x0A)
#define INA228_REG_DIAG_ALRT        (0x0B)
#define INA228_REG_SOVL             (0x0C)
#define INA228_REG_SUVL             (0x0D)
#define INA228_REG_BOVL             (0x0E)
#define INA228_REG_BUVL             (0x0F)
#define INA228_REG_TEMP_LIMIT       (0x10)
#define INA228_REG_PWR_LIMIT        (0x11)
#define INA228_REG_MANUFACTURER_ID   (0x3E)
#define INA228_REG_DEVICE_ID         (0x3F)

// CONFIG register bit masks
// bits 15 RESET
// bits 14 RSTACC
// bits 13-6 CONVDLY
// bits 5 TEMPCOMP
// bits 4 ADCRANGE
// bits 3-0 NO EFFECT
#define INA228_CONFIG_RST            (1 << 15)  // Software reset
#define INA228_CONFIG_RSTACC         (1 << 14)  // Reset energy and charge accumulation
#define INA228_CONFIG_CONVDLY_MASK   (0x3F << 6) // Conversion delay (bits 13-6)
#define INA228_CONFIG_TEMPCOMP       (1 << 5)   // Temperature compensation
#define INA228_CONFIG_ADCRANGE       (1 << 4)   // ADC range: 0 = ±163.84mV, 1 = ±40.96mV
// ADC_CONFIG register bit masks
// bits 15-12 MODE
// bits 11-9 VBUSCT
// bits 8-6 VSHCT
// bits 5-3 VTCT
// bits 2-0 AVG
#define INA228_ADC_CONFIG_MODE_MASK   (0x0F << 12)
#define INA228_ADC_CONFIG_VBUSCT_MASK (0x07 << 9)
#define INA228_ADC_CONFIG_VSHCT_MASK  (0x07 << 6)
#define INA228_ADC_CONFIG_VTCT_MASK   (0x07 << 3)
#define INA228_ADC_CONFIG_AVG_MASK    (0x07)

// INA228 Datasheet 7.6.1.1
typedef enum {
    INA228_CONV_TIME_50US   = 0x0,  // 50 µs
    INA228_CONV_TIME_84US   = 0x1,  // 84 µs
    INA228_CONV_TIME_150US  = 0x2,  // 150 µs
    INA228_CONV_TIME_280US  = 0x3,  // 280 µs
    INA228_CONV_TIME_540US  = 0x4,  // 540 µs
    INA228_CONV_TIME_1052US = 0x5,  // 1052 µs
    INA228_CONV_TIME_2074US = 0x6,  // 2074 µs
    INA228_CONV_TIME_4120US = 0x7,  // 4120 µs
} ina228_convTime_t;

// Averaging modes (for ADC_CONFIG register)
typedef enum {
    INA228_AVERAGES_1        = 0x0,   // 1 sample
    INA228_AVERAGES_4        = 0x1,   // 4 samples
    INA228_AVERAGES_16       = 0x2,   // 16 samples
    INA228_AVERAGES_64       = 0x3,   // 64 samples
    INA228_AVERAGES_128      = 0x4,   // 128 samples
    INA228_AVERAGES_256      = 0x5,   // 256 samples
    INA228_AVERAGES_512      = 0x6,   // 512 samples
    INA228_AVERAGES_1024     = 0x7,   // 1024 samples
} ina228_averages_t;

// Operating modes (for ADC_CONFIG register)
typedef enum {
    INA228_MODE_SHUTDOWN                = 0x0,   // Shutdown
    INA228_MODE_BUS_SINGLE              = 0x1,   // Bus voltage, single-shot
    INA228_MODE_SHUNT_SINGLE            = 0x2,   // Shunt voltage, single-shot
    INA228_MODE_BUS_SHUNT_SINGLE        = 0x3,   // Bus + shunt voltage, single-shot
    INA228_MODE_TEMP_SINGLE             = 0x4,   // Temperature, single-shot
    INA228_MODE_BUS_TEMP_SINGLE         = 0x5,   // Bus + temperature, single-shot
    INA228_MODE_SHUNT_TEMP_SINGLE       = 0x6,   // Shunt + temperature, single-shot
    INA228_MODE_ALL_SINGLE              = 0x7,   // Bus + shunt + temperature, single-shot
    INA228_MODE_SHUTDOWN_ALT            = 0x8,   // Shutdown (alternate)
    INA228_MODE_BUS_CONTINUOUS          = 0x9,   // Bus voltage, continuous
    INA228_MODE_SHUNT_CONTINUOUS        = 0xA,   // Shunt voltage, continuous
    INA228_MODE_BUS_SHUNT_CONTINUOUS    = 0xB,   // Bus + shunt voltage, continuous
    INA228_MODE_TEMP_CONTINUOUS         = 0xC,   // Temperature, continuous
    INA228_MODE_BUS_TEMP_CONTINUOUS     = 0xD,   // Bus + temperature, continuous
    INA228_MODE_SHUNT_TEMP_CONTINUOUS   = 0xE,   // Shunt + temperature, continuous
    INA228_MODE_ALL_CONTINUOUS          = 0xF,   // Bus + shunt + temperature, continuous
} ina228_mode_t;

// Threshold limit functions
typedef enum {
    ADC_RANGE_163_84mV = 0,   // ADCRANGE = 0, ±163.84 mV, LSB = 312.5 nV
    ADC_RANGE_40_96mV  = 1    // ADCRANGE = 1, ±40.96 mV, LSB = 78.125 nV
} ina228_adc_range_t;

class INA228 {
public:
    explicit INA228(TwoWire &w);

    // Initialization
    bool begin(uint8_t address = INA228_ADDRESS);
    
    // Configure ADC (averaging, conversion times, mode)
    bool configure(ina228_averages_t avg = INA228_AVERAGES_1,
                   ina228_convTime_t busConvTime = INA228_CONV_TIME_1052US,
                   ina228_convTime_t shuntConvTime = INA228_CONV_TIME_1052US,
                   ina228_convTime_t tempConvTime = INA228_CONV_TIME_1052US,
                   ina228_mode_t mode = INA228_MODE_ALL_CONTINUOUS);
    
    // Calibration: rShuntValue in ohms, iMaxCurrentExcepted in amperes, adcRange for setting power LSB (default ±163.84mV)
    bool calibrate(float rShuntValue, float iMaxCurrentExcepted, ina228_adc_range_t adcRange = ADC_RANGE_163_84mV);
    bool setMode(ina228_mode_t mode);
    // Read functions
    float readBusVoltage();        // Volts
    float readShuntVoltage();      // Volts
    float readCurrent();           // Amperes
    float readPower();             // Watts
    float readTemperature();       // Degrees Celsius
    float readEnergy();            // Joules (requires continuous read)
    float readCharge();            // Coulombs (requires continuous read)
    float readCharge_mAh();         // mAh (requires continuous read)
    float readEnergy_mWh();         // mWh (requires continuous read)
    // Compensation and configuration functions
    uint16_t readShuntTemperatureCoefficient();
    bool enableTemperatureCompensation(bool enable);
    bool setShuntTemperatureCoefficient(uint16_t ppm);
    // Alert and diagnostic functions
    bool setAlertLatch(bool latch);
    bool setAlertInvertedPolarity(bool inverted);
    bool isMathOverflow();
    bool isConversionReady();
    bool isAlert();

    // Threshold limits (simplified: set threshold in volts/amps/°C/watts)
    bool setShuntOverVoltageLimit(float voltage);
    bool setShuntUnderVoltageLimit(float voltage);
    bool setBusOverVoltageLimit(float voltage);
    bool setBusUnderVoltageLimit(float voltage);
    bool setTemperatureLimit(float celsius);
    bool setPowerLimit(float watts);
    // CONFIG register functions
    bool reset();                    // Software reset
    bool resetAccumulation();        // Reset energy and charge accumulation
    bool setADCRange(ina228_adc_range_t range);     // Set ADC range (±163.84mV or ±40.96mV)
    uint16_t readConfig();           // Read CONFIG register

    // Read chip identification
    uint16_t readManufacturerID();   // Should be 0x5449 ("TI")
    uint16_t readDeviceID();         // Should be 0x2280 or 0x2281

private:
    TwoWire *wire;
    uint8_t i2cAddress;

    // Calibration values
    float currentLSB;        // Amperes per LSB, (1U << 19) / iMaxCurrentExcepted
    float powerLSB;          // Watts per LSB, 3.2f * currentLSB
    float rShunt;            // Shunt resistor value (ohms)
    float vBusMax;           // Maximum expected bus voltage (for power LSB)
    
    // Internal functions
    bool writeRegister16(uint8_t reg, uint16_t val);
    bool writeRegister24(uint8_t reg, uint32_t val);
    uint16_t readRegister16(uint8_t reg);
    uint32_t readRegister24(uint8_t reg);
    int32_t readRegister24Signed(uint8_t reg);
    uint64_t readRegister40(uint8_t reg);
    int64_t readRegister40Signed(uint8_t reg);
    uint16_t getDiagnosticFlags();
    bool setDiagnosticBits(uint16_t bits);
    bool clearDiagnosticBits(uint16_t bits);
};

#endif