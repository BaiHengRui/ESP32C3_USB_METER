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


#include "INA228.h"

/* ----- Public ----- */

INA228::INA228(TwoWire &w) : wire(&w), i2cAddress(INA228_ADDRESS), currentLSB(0), powerLSB(0), rShunt(0), vBusMax(0) {}

bool INA228::begin(uint8_t address) {
    i2cAddress = address;

    // return configure(
    //     INA228_AVERAGES_16,
    //     INA228_CONV_TIME_1052US,
    //     INA228_CONV_TIME_1052US,
    //     INA228_CONV_TIME_1052US,
    //     INA228_MODE_ALL_CONTINUOUS
    // );
    return true;
}

bool INA228::configure(ina228_averages_t avg, ina228_convTime_t busConvTime,
                       ina228_convTime_t shuntConvTime, ina228_convTime_t tempConvTime,
                       ina228_mode_t mode) {
    // Configure ADC_CONFIG register (0x01)
    // Bits 15-12: MODE (4 bits)
    // Bits 11-9: VBUSCT (3 bits) - bus voltage conversion time
    // Bits 8-6: VSHCT (3 bits) - shunt voltage conversion time
    // Bits 5-3: VTCT (3 bits) - temperature conversion time
    // Bits 2-0: AVG (3 bits) - averaging
    uint16_t adcConfig = 0;
    adcConfig |= ((uint16_t)mode << 12) & INA228_ADC_CONFIG_MODE_MASK;
    adcConfig |= ((uint16_t)busConvTime << 9) & INA228_ADC_CONFIG_VBUSCT_MASK;
    adcConfig |= ((uint16_t)shuntConvTime << 6) & INA228_ADC_CONFIG_VSHCT_MASK;
    adcConfig |= ((uint16_t)tempConvTime << 3) & INA228_ADC_CONFIG_VTCT_MASK;
    adcConfig |= (uint16_t)avg & INA228_ADC_CONFIG_AVG_MASK;

    if (!writeRegister16(INA228_REG_ADC_CONFIG, adcConfig)) {
        return false;
    }

    // NOTE: CONFIG register (0x00) is NOT configured here.
    // ADCRANGE is set by calibrate()/setADCRange(), TEMPCOMP by
    // enableTemperatureCompensation(), CONVDLY/RST/RSTACC by their own functions.
    // Writing CONFIG here would overwrite those settings.

    return true;
}

bool INA228::calibrate(float rShuntValue, float iMaxCurrentExpected, ina228_adc_range_t adcRange) {
    rShunt = rShuntValue;

    // Calculate Current_LSB based on maximum expected current
    // Current_LSB = MaxCurrent / 2^19 (from datasheet section 8.1.2)
    currentLSB = iMaxCurrentExpected / 524288.0f;

    // Calculate Power_LSB
    // Power_LSB = 3.2 * Current_LSB (from datasheet section 8.1.4)
    powerLSB = 3.2f * currentLSB;

    // Calculate expected maximum shunt voltage
    // V_shunt_max = I_max * R_shunt
    float maxShuntVoltage = iMaxCurrentExpected * rShuntValue;

    // Determine optimal ADC range
    // ADCRANGE = 0: ±163.84mV range (LSB = 312.5 nV) - for higher currents
    // ADCRANGE = 1: ±40.96mV range (LSB = 78.125 nV) - for lower currents with better resolution
    ina228_adc_range_t selectedRange = adcRange;

    // Set the ADC range
    if (!setADCRange(selectedRange)) {
        return false;
    }

    // Calculate SHUNT_CAL register value according to the datasheet section 8.1.2.
    // SHUNT_CAL = 13107.2e6 * Current_LSB * Rshunt  (for ADCRANGE = 0)
    // For ADCRANGE = 1, SHUNT_CAL must be multiplied by 4.
    // This value must stay within the 16-bit register range (0x0000..0x7FFF).
    float shuntCalFloat = 13107.2e6f * currentLSB * rShunt;

    // Apply ×4 multiplier for ADCRANGE = 1 (±40.96mV range)
    if (selectedRange == ADC_RANGE_40_96mV) {
        shuntCalFloat *= 4.0f;
    }

    uint32_t shuntCal = (uint32_t)(shuntCalFloat + 0.5f);

    if (shuntCal > 0x7FFFu) {
        return false;
    }

    return writeRegister16(INA228_REG_SHUNT_CAL, (uint16_t)(shuntCal & 0x7FFF));
}

bool INA228::setMode(ina228_mode_t mode) {
    uint16_t adcConfig = readRegister16(INA228_REG_ADC_CONFIG);
    adcConfig &= ~INA228_ADC_CONFIG_MODE_MASK;
    adcConfig |= ((uint16_t)mode << 12) & INA228_ADC_CONFIG_MODE_MASK;
    return writeRegister16(INA228_REG_ADC_CONFIG, adcConfig);
}

// Read bus voltage in volts
// Formula: VBUS = raw_value * 195.3125 μV (from datasheet section 8.1.1)
float INA228::readBusVoltage() {
    // Read 24-bit register, right-shift by 4 bits
    uint32_t value = readRegister24(INA228_REG_VBUS) >> 4;

    // Bus voltage LSB = 195.3125 μV
    float bus_LSB = 195.3125e-6f;

    return value * bus_LSB;
}

// Read shunt voltage in volts
// Formula: VSHUNT = raw_value * LSB (from datasheet section 8.1.1)
// LSB = 312.5 nV when ADCRANGE=0 (±163.84mV range)
// LSB = 78.125 nV when ADCRANGE=1 (±40.96mV range)
float INA228::readShuntVoltage() {
    // Read 24-bit signed register, right-shift by 4 bits
    int32_t value = readRegister24Signed(INA228_REG_VSHUNT) >> 4;

    // Sign extend for 20-bit value
    if (value & 0x00080000) {
        value |= 0xFFF00000;
    }

    // Check ADC range from CONFIG register
    uint16_t config = readConfig();
    bool adcRange = (config >> 4) & 0x01;

    float shunt_LSB;
    if (adcRange) {
        // ADCRANGE = 1: ±40.96mV range, LSB = 78.125 nV
        shunt_LSB = 78.125e-9f;
    } else {
        // ADCRANGE = 0: ±163.84mV range, LSB = 312.5 nV
        shunt_LSB = 312.5e-9f;
    }

    return value * shunt_LSB;
}

// Read current in amperes
// Formula: I = raw_value * Current_LSB (from datasheet section 8.1.2)
// Current_LSB is set by calibrate() function
float INA228::readCurrent() {
    // Read 24-bit signed register, right-shift by 4 bits
    int32_t value = readRegister24Signed(INA228_REG_CURRENT) >> 4;

    // Sign extend for 20-bit value
    if (value & 0x00080000) {
        value |= 0xFFF00000;
    }

    return value * currentLSB;
}

// Read power in watts
// Formula: P = raw_value * 3.2 * Current_LSB (from datasheet section 8.1.4)
float INA228::readPower() {
    // Read 24-bit unsigned register
    uint32_t value = readRegister24(INA228_REG_POWER);

    return value * 3.2f * currentLSB;
}

// Read temperature in degrees Celsius
// Formula: T = raw_value * 7.8125 m°C (from datasheet section 8.1.3)
// Range: -256°C to +256°C (limited to -40°C to +125°C by package)
float INA228::readTemperature() {
    // Read 16-bit register as signed (two's complement) to support negative temperatures
    int16_t value = (int16_t)readRegister16(INA228_REG_DIETEMP);

    // Temperature LSB = 7.8125 m°C = 0.0078125 °C
    float temp_LSB = 7.8125e-3f;

    return value * temp_LSB;
}

// Read energy in joules
// Formula: E = raw_value * 16 * 3.2 * Current_LSB = raw_value * 51.2 * Current_LSB
// (from datasheet section 8.1.5)
float INA228::readEnergy() {
    // Read 40-bit unsigned register
    uint64_t value = readRegister40(INA228_REG_ENERGY);

    // Energy multiplier = 16 * 3.2 = 51.2
    return value * 51.2f * currentLSB;
}

// Read charge in coulombs
// Formula: Q = raw_value * Current_LSB (from datasheet section 8.1.6)
float INA228::readCharge() {
    // Read 40-bit signed register
    int64_t value = readRegister40Signed(INA228_REG_CHARGE);

    return value * currentLSB;
}

// Read charge in milliampere-hours (mAh)
float INA228::readCharge_mAh() {
    // Convert coulombs to mAh: 1 C = 1000 mAh / 3600 = 0.27778 mAh
    // Or: Q(mAh) = Q(C) * 1000 / 3600 = Q(C) / 3.6
    return readCharge() / 3.6f;
}

// Read energy in milliwatt-hours (mWh)
float INA228::readEnergy_mWh() {
    // Convert joules to mWh: 1 J = 1000 mWh / 3600 = 0.27778 mWh
    // Or: E(mWh) = E(J) * 1000 / 3600 = E(J) / 3.6
    return readEnergy() / 3.6f;
}

// Temperature compensation functions

// Read shunt temperature coefficient setting
// Returns the current ppm/°C value from SHUNT_TEMPCO register (0x03)
uint16_t INA228::readShuntTemperatureCoefficient() {
    return readRegister16(INA228_REG_SHUNT_TEMPCO);
}

// Enable or disable temperature compensation for shunt resistance
// When enabled, the CURRENT register is automatically corrected based on
// the temperature coefficient stored in SHUNT_TEMPCO register (0x03)
// Reference temperature is 25°C
// Formula: Radjusted = Rnominal + (Rnominal x (temperature - 25) x PPM) x 10^-6
bool INA228::enableTemperatureCompensation(bool enable) {
    uint16_t config = readConfig();

    if (enable) {
        config |= INA228_CONFIG_TEMPCOMP;  // Set bit 5
    } else {
        config &= ~INA228_CONFIG_TEMPCOMP; // Clear bit 5
    }

    return writeRegister16(INA228_REG_CONFIG, config);
}

// Set shunt temperature coefficient for temperature compensation
// ppm: temperature coefficient in ppm/°C (0-16383)
// Common values:
//   50  = Copper (Cu)
//   100 = Manganin (CuMnNi)
//   150 = Nichrome (NiCr)
//   0   = Temperature compensation disabled
// This function writes to SHUNT_TEMPCO register (0x03)
bool INA228::setShuntTemperatureCoefficient(uint16_t ppm) {
    // ppm value must be within 14-bit range (0-16383)
    if (ppm > 16383) {
        return false;
    }

    return writeRegister16(INA228_REG_SHUNT_TEMPCO, ppm);
}

// Alert and diagnostic functions

// Set alert latch enable
bool INA228::setAlertLatch(bool latch) {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);

    if (latch) {
        diag |= (1 << 15);  // Set ALATCH bit
    } else {
        diag &= ~(1 << 15); // Clear ALATCH bit
    }

    return writeRegister16(INA228_REG_DIAG_ALRT, diag);
}

// Set alert polarity
bool INA228::setAlertInvertedPolarity(bool inverted) {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);

    if (inverted) {
        diag |= (1 << 12);  // Set APOL bit
    } else {
        diag &= ~(1 << 12); // Clear APOL bit
    }

    return writeRegister16(INA228_REG_DIAG_ALRT, diag);
}

// Check for math overflow flag
bool INA228::isMathOverflow() {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);
    return (diag & (1 << 9)) != 0;
}

// Check if conversion is ready (CNVRF bit 1)
bool INA228::isConversionReady() {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);
    return (diag & (1 << 1)) != 0;
}

// Check if any threshold alert is triggered
bool INA228::isAlert() {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);
    // Check threshold alert flags: TMPOL(7), SHNTOL(6), SHNTUL(5), BUSOL(4), BUSUL(3), POL(2)
    return (diag & 0x00FC) != 0;
}

// Check energy overflow flag (ENERGYOF bit 11)
bool INA228::isEnergyOverflow() {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);
    return (diag & (1 << 11)) != 0;
}

// Check charge overflow flag (CHARGEOF bit 10)
bool INA228::isChargeOverflow() {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);
    return (diag & (1 << 10)) != 0;
}

// Check temperature over-limit flag (TMPOL bit 7)
bool INA228::isTempOverLimit() {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);
    return (diag & (1 << 7)) != 0;
}

// Check shunt over-limit flag (SHNTOL bit 6)
bool INA228::isShuntOverLimit() {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);
    return (diag & (1 << 6)) != 0;
}

// Check shunt under-limit flag (SHNTUL bit 5)
bool INA228::isShuntUnderLimit() {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);
    return (diag & (1 << 5)) != 0;
}

// Check bus over-limit flag (BUSOL bit 4)
bool INA228::isBusOverLimit() {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);
    return (diag & (1 << 4)) != 0;
}

// Check bus under-limit flag (BUSUL bit 3)
bool INA228::isBusUnderLimit() {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);
    return (diag & (1 << 3)) != 0;
}

// Check power over-limit flag (POL bit 2)
bool INA228::isPowerOverLimit() {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);
    return (diag & (1 << 2)) != 0;
}

// Threshold limit functions

// Set shunt overvoltage threshold in volts
// Formula: Threshold = voltage / LSB (from datasheet section 7.6.1.13)
// LSB = 5 µV when ADCRANGE=0, 1.25 µV when ADCRANGE=1
bool INA228::setShuntOverVoltageLimit(float voltage) {
    uint16_t config = readConfig();
    bool adcRange = (config >> 4) & 0x01;

    int32_t threshold;
    if (adcRange) {
        // ADCRANGE = 1: LSB = 1.25 µV
        threshold = (int32_t)(voltage / 1.25e-6f);
    } else {
        // ADCRANGE = 0: LSB = 5 µV
        threshold = (int32_t)(voltage / 5.0e-6f);
    }

    // Clamp to 16-bit signed range
    if (threshold > 32767) threshold = 32767;
    if (threshold < -32768) threshold = -32768;

    return writeRegister16(INA228_REG_SOVL, (uint16_t)threshold);
}

// Set shunt undervoltage threshold in volts
// Formula: Threshold = voltage / LSB (from datasheet section 7.6.1.14)
// LSB = 5 µV when ADCRANGE=0, 1.25 µV when ADCRANGE=1
bool INA228::setShuntUnderVoltageLimit(float voltage) {
    uint16_t config = readConfig();
    bool adcRange = (config >> 4) & 0x01;

    int32_t threshold;
    if (adcRange) {
        // ADCRANGE = 1: LSB = 1.25 µV
        threshold = (int32_t)(voltage / 1.25e-6f);
    } else {
        // ADCRANGE = 0: LSB = 5 µV
        threshold = (int32_t)(voltage / 5.0e-6f);
    }

    // Clamp to 16-bit signed range
    if (threshold > 32767) threshold = 32767;
    if (threshold < -32768) threshold = -32768;

    return writeRegister16(INA228_REG_SUVL, (uint16_t)threshold);
}

// Set bus overvoltage threshold in volts
// Formula: Threshold = voltage / 3.125 mV (from datasheet section 7.6.1.15)
bool INA228::setBusOverVoltageLimit(float voltage) {
    uint32_t threshold = (uint32_t)(voltage / 3.125e-3f);

    // BOVL is 15-bit (bit 15 reserved), max value 0x7FFF
    if (threshold > 0x7FFF) threshold = 0x7FFF;

    return writeRegister16(INA228_REG_BOVL, (uint16_t)threshold);
}

// Set bus undervoltage threshold in volts
// Formula: Threshold = voltage / 3.125 mV (from datasheet section 7.6.1.16)
bool INA228::setBusUnderVoltageLimit(float voltage) {
    uint32_t threshold = (uint32_t)(voltage / 3.125e-3f);

    // BUVL is 15-bit (bit 15 reserved), max value 0x7FFF
    if (threshold > 0x7FFF) threshold = 0x7FFF;

    return writeRegister16(INA228_REG_BUVL, (uint16_t)threshold);
}

// Set temperature limit in degrees Celsius
// Formula: Threshold = temperature / 7.8125 m°C (from datasheet section 7.6.1.17)
bool INA228::setTemperatureLimit(float celsius) {
    int16_t threshold = (int16_t)(celsius / 7.8125e-3f);

    return writeRegister16(INA228_REG_TEMP_LIMIT, (uint16_t)threshold);
}

// Set power limit in watts
// Formula: Threshold = watts / (256 * Power_LSB) = watts / (256 * 3.2 * Current_LSB)
// (from datasheet section 7.6.1.18)
bool INA228::setPowerLimit(float watts) {
    uint32_t threshold = (uint32_t)(watts / (256.0f * 3.2f * currentLSB));

    // PWR_LIMIT is 16-bit unsigned, max 0xFFFF
    if (threshold > 0xFFFF) threshold = 0xFFFF;

    return writeRegister16(INA228_REG_PWR_LIMIT, (uint16_t)threshold);
}

// CONFIG register functions

// Software reset
bool INA228::reset() {
    uint16_t config = readConfig();
    config |= INA228_CONFIG_RST;
    return writeRegister16(INA228_REG_CONFIG, config);
}

// Reset energy and charge accumulation
bool INA228::resetAccumulation() {
    uint16_t config = readConfig();
    config |= INA228_CONFIG_RSTACC;
    return writeRegister16(INA228_REG_CONFIG, config);
}

// Set ADC range
bool INA228::setADCRange(ina228_adc_range_t range) {
    uint16_t config = readConfig();
    if (range == ADC_RANGE_40_96mV) {
        config |= INA228_CONFIG_ADCRANGE;
    } else {
        config &= ~INA228_CONFIG_ADCRANGE;
    }
    return writeRegister16(INA228_REG_CONFIG, config);
}

// Read CONFIG register
uint16_t INA228::readConfig() {
    return readRegister16(INA228_REG_CONFIG);
}

// Read manufacturer ID (should be 0x5449 = "TI")
uint16_t INA228::readManufacturerID() {
    return readRegister16(INA228_REG_MANUFACTURER_ID);
}

// Read device ID (should be 0x2280 or 0x2281)
uint16_t INA228::readDeviceID() {
    return readRegister16(INA228_REG_DEVICE_ID);
}

/* ----- Private ----- */

// Write 16-bit register
bool INA228::writeRegister16(uint8_t reg, uint16_t val) {
    wire->beginTransmission(i2cAddress);
    wire->write(reg);
    wire->write((uint8_t)(val >> 8));   // High byte
    wire->write((uint8_t)(val & 0xFF)); // Low byte
    return (wire->endTransmission() == 0);
}

// Write 24-bit register
bool INA228::writeRegister24(uint8_t reg, uint32_t val) {
    wire->beginTransmission(i2cAddress);
    wire->write(reg);
    wire->write((uint8_t)(val >> 16)); // High byte
    wire->write((uint8_t)(val >> 8));  // Middle byte
    wire->write((uint8_t)(val & 0xFF)); // Low byte
    return (wire->endTransmission() == 0);
}

// Read 16-bit register
uint16_t INA228::readRegister16(uint8_t reg) {
    wire->beginTransmission(i2cAddress);
    wire->write(reg);
    wire->endTransmission(false);

    wire->requestFrom(i2cAddress, (uint8_t)2);
    uint16_t highByte = wire->read();
    uint16_t lowByte = wire->read();

    return (highByte << 8) | lowByte;
}

// Read 24-bit unsigned register
uint32_t INA228::readRegister24(uint8_t reg) {
    wire->beginTransmission(i2cAddress);
    wire->write(reg);
    wire->endTransmission(false);

    wire->requestFrom(i2cAddress, (uint8_t)3);
    uint32_t highByte = wire->read();
    uint32_t midByte = wire->read();
    uint32_t lowByte = wire->read();

    return (highByte << 16) | (midByte << 8) | lowByte;
}

// Read 24-bit signed register
int32_t INA228::readRegister24Signed(uint8_t reg) {
    wire->beginTransmission(i2cAddress);
    wire->write(reg);
    wire->endTransmission(false);

    wire->requestFrom(i2cAddress, (uint8_t)3);
    int32_t highByte = wire->read();
    int32_t midByte = wire->read();
    int32_t lowByte = wire->read();

    int32_t value = (highByte << 16) | (midByte << 8) | lowByte;

    // Sign extend if negative (bit 23 is set)
    if (highByte & 0x80) {
        value |= 0xFF000000;
    }

    return value;
}

// Read 40-bit unsigned register
uint64_t INA228::readRegister40(uint8_t reg) {
    wire->beginTransmission(i2cAddress);
    wire->write(reg);
    wire->endTransmission(false);

    wire->requestFrom(i2cAddress, (uint8_t)5);
    uint64_t byte4 = wire->read(); // Bits 39-32
    uint64_t byte3 = wire->read(); // Bits 31-24
    uint64_t byte2 = wire->read(); // Bits 23-16
    uint64_t byte1 = wire->read(); // Bits 15-8
    uint64_t byte0 = wire->read(); // Bits 7-0

    return (byte4 << 32) | (byte3 << 24) | (byte2 << 16) | (byte1 << 8) | byte0;
}

// Read 40-bit signed register
int64_t INA228::readRegister40Signed(uint8_t reg) {
    wire->beginTransmission(i2cAddress);
    wire->write(reg);
    wire->endTransmission(false);

    wire->requestFrom(i2cAddress, (uint8_t)5);
    int64_t byte4 = wire->read();
    int64_t byte3 = wire->read();
    int64_t byte2 = wire->read();
    int64_t byte1 = wire->read();
    int64_t byte0 = wire->read();

    int64_t value = (byte4 << 32) | (byte3 << 24) | (byte2 << 16) | (byte1 << 8) | byte0;

    // Sign extend if negative (bit 39 is set)
    if (byte4 & 0x80) {
        value |= 0xFFFFFF0000000000LL;
    }

    return value;
}

// Get diagnostic flags
uint16_t INA228::getDiagnosticFlags() {
    return readRegister16(INA228_REG_DIAG_ALRT);
}

// Set diagnostic bits
bool INA228::setDiagnosticBits(uint16_t bits) {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);
    diag |= bits;
    return writeRegister16(INA228_REG_DIAG_ALRT, diag);
}

// Clear diagnostic bits
bool INA228::clearDiagnosticBits(uint16_t bits) {
    uint16_t diag = readRegister16(INA228_REG_DIAG_ALRT);
    diag &= ~bits;
    return writeRegister16(INA228_REG_DIAG_ALRT, diag);
}
