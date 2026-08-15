/**
 * @file    PD_Sink.cpp
 * @brief   USB PD Sink 主动控制器实现
 */

#include <string.h>
#include <Arduino.h>
#include <Wire.h>
#include "PD_Sink.h"

// 定时常量 (ms)
#define T_PD_POLLING        100     ///< 轮询间隔
#define T_WAIT_SRC_CAP      350     ///< 等待 Source Cap 超时
#define T_WAIT_PS_RDY       580     ///< 等待 PS_RDY 超时
#define T_PPS_REQUEST       5000    ///< PPS 定期请求间隔 (<10s)

static PD_Sink * g_activeSink = nullptr;

// I2C 回调
int PD_Sink::_i2cRead(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t count) {
    Wire.beginTransmission(devAddr);
    Wire.write(regAddr);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom(devAddr, count) != count) return -1;
    for (uint8_t i = 0; i < count; i++) data[i] = Wire.read();
    return 0;
}

int PD_Sink::_i2cWrite(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t count) {
    Wire.beginTransmission(devAddr);
    Wire.write(regAddr);
    Wire.write(data, count);
    return Wire.endTransmission() == 0 ? 0 : -1;
}

int PD_Sink::_delayMs(uint32_t ms) { delay(ms); return 0; }

// ============================================================================
// 构造与初始化
// ============================================================================

PD_Sink::PD_Sink() {
    memset(&_dev, 0, sizeof(_dev));
    _intPin = 0;
    _initialized = false;
    _callback = nullptr;
    _callbackData = nullptr;
    _powerOption = PD_OPTION_MAX_5V;
    _state = PD_SINK_DISCONNECTED;
    _messageID = 0;
    memset(_sourcePDOs, 0, sizeof(_sourcePDOs));
    _sourcePDOCount = 0;
    _selectedPosition = 0;
    _ppsCapable = false;
    _ppsPDOIndex = 0;
    _voltage = 5000;
    _current = 1000;
    _isPPS = false;
    _ppsTargetVoltage = 0;
    _ppsTargetCurrent = 0;
    _lastPollTime = 0;
    _waitSrcCapStart = 0;
    _waitPSRDYStart = 0;
    _lastPPSRequest = 0;
    _srcCapRetries = 0;
    _waitingSrcCap = false;
    _waitingPSRDY = false;
    _needRequest = false;
    _ccPin = 0;
    _cc1Level = 0;
    _cc2Level = 0;
    _startTime = 0;
}

bool PD_Sink::begin(uint8_t intPin, uint8_t i2cAddr) {
    _intPin = intPin;
    pinMode(intPin, INPUT_PULLUP);
    g_activeSink = this;

    FUSB302_Status status = fusb302_init(&_dev, i2cAddr, _i2cRead, _i2cWrite, _delayMs);
    if (status != FUSB302_OK) {
        Serial.printf("[Sink] Init failed: %s\n", fusb302_get_error(&_dev));
        return false;
    }

    // 主动模式
    fusb302_set_mode(&_dev, FUSB302_MODE_ACTIVE);

    _initialized = true;
    _startTime = millis();
    _state = PD_SINK_DISCONNECTED;

    uint8_t ver, rev;
    fusb302_get_device_id(&_dev, &ver, &rev);
    Serial.printf("[Sink] Active mode ready | DevID: %c_rev%c\n", 'A' + ver, 'A' + rev);
    return true;
}

bool PD_Sink::beginPPS(uint8_t intPin, float ppsVoltage, float ppsCurrent, PD_PowerOption option) {
    _ppsTargetVoltage = (uint16_t)(ppsVoltage * 50.0f + 0.5f);   // V → 20mV
    _ppsTargetCurrent = (uint8_t)(ppsCurrent * 20.0f + 0.5f);     // A → 50mA
    _powerOption = option;
    return begin(intPin);
}

void PD_Sink::onEvent(PD_SinkCallback cb, void *userData) {
    _callback = cb;
    _callbackData = userData;
}

void PD_Sink::setPowerOption(PD_PowerOption option) {
    _powerOption = option;
    if (_sourcePDOCount > 0) {
        _evaluateAndSelect();
        _needRequest = true;
    }
}

// ============================================================================
// 主循环
// ============================================================================

void PD_Sink::update() {
    if (!_initialized) return;

    uint32_t now = millis();

    // INT 引脚检测
    if (digitalRead(_intPin) == LOW) {
        FUSB302_Event events = FUSB302_EVT_NONE;
        for (uint8_t i = 0; i < 3; i++) {
            if (fusb302_poll(&_dev, &events) == FUSB302_OK) break;
        }

        if (events & FUSB302_EVT_ATTACHED)   _handleConnected();
        if (events & FUSB302_EVT_DETACHED)   _handleDisconnected();

        if (events & FUSB302_EVT_RX_SOP) {
            uint16_t header; uint32_t data[7];
            fusb302_read_message(&_dev, &header, data);
            _handleRX(header, data);
        }

        if (events & FUSB302_EVT_GOOD_CRC_SENT) {
            // 可以在此发送响应消息
            if (_needRequest) {
                _sendRequest();
                _needRequest = false;
            }
        }
    }

    // 定时器处理
    if (now - _lastPollTime < T_PD_POLLING) return;
    _lastPollTime = now;

    // 等待 Source Cap 超时
    if (_waitingSrcCap) {
        if (now - _waitSrcCapStart > T_WAIT_SRC_CAP) {
            if (_srcCapRetries < 3) {
                _srcCapRetries++;
                _sendGetSourceCap();
            } else {
                // 超时, Hard Reset
                _srcCapRetries = 0;
                fusb302_send_hard_reset(&_dev);
                _waitingSrcCap = false;
                _state = PD_SINK_CONNECTED;
            }
        }
    }

    // 等待 PS_RDY 超时
    if (_waitingPSRDY) {
        if (now - _waitPSRDYStart > T_WAIT_PS_RDY) {
            _waitingPSRDY = false;
            _fireEvent(PD_SINK_EVT_ERROR);
        }
    }

    // PPS 定期刷新 (>10s不刷新则断电)
    if (_isPPS && _state == PD_SINK_PPS_ACTIVE) {
        if (now - _lastPPSRequest > T_PPS_REQUEST) {
            _needRequest = true;
            _lastPPSRequest = now;
        }
    }
}

// ============================================================================
// 事件处理
// ============================================================================

void PD_Sink::_handleConnected() {
    fusb302_get_cc_levels(&_dev, &_cc1Level, &_cc2Level);

    if (_cc1Level > 0 && _cc2Level == 0) _ccPin = 1;
    else if (_cc2Level > 0 && _cc1Level == 0) _ccPin = 2;
    else if (_cc1Level > 0) _ccPin = 1;
    else _ccPin = 0;

    _state = PD_SINK_CONNECTED;
    _messageID = 0;
    _sourcePDOCount = 0;
    _selectedPosition = 0;
    _ppsCapable = false;
    _voltage = 5000;
    _current = 1000;
    _isPPS = false;

    _waitingSrcCap = true;
    _waitSrcCapStart = millis();
    _srcCapRetries = 0;

    _fireEvent(PD_SINK_EVT_CONNECTED);
}

void PD_Sink::_handleDisconnected() {
    _state = PD_SINK_DISCONNECTED;
    _waitingSrcCap = false;
    _waitingPSRDY = false;
    _needRequest = false;
    _ccPin = 0;
    _fireEvent(PD_SINK_EVT_DISCONNECTED);
}

void PD_Sink::_handleRX(uint16_t header, uint32_t *data) {
    if (pd_is_source_cap(header)) {
        _handleSourceCap(header, data);
    } else if (pd_is_ps_rdy(header)) {
        _handlePSRDY();
    } else if (pd_is_accept(header)) {
        _handleAccept();
    } else if (pd_is_reject(header)) {
        _handleReject();
    } else if (pd_is_pps_status(header)) {
        _handlePPSStatus(data);
    }

    // 收到 GoodCRC 后可能需要发送响应
    // (在 update 的 GOOD_CRC_SENT 事件中处理)
}

void PD_Sink::_handleSourceCap(uint16_t header, uint32_t *data) {
    PD_HeaderInfo hdr;
    pd_parse_header(header, &hdr);

    _waitingSrcCap = false;
    _srcCapRetries = 0;
    _sourcePDOCount = (hdr.num_objects <= PD_SINK_MAX_PDO) ? hdr.num_objects : PD_SINK_MAX_PDO;
    _ppsCapable = false;

    for (uint8_t i = 0; i < _sourcePDOCount; i++) {
        _sourcePDOs[i] = data[i];
        uint8_t pdoType = (data[i] >> 30) & 0x3;
        if (pdoType == PD_PDO_TYPE_AUGMENTED) {
            _ppsCapable = true;
            _ppsPDOIndex = i;
        }
    }

    _evaluateAndSelect();
    _state = PD_SINK_NEGOTIATING;
    _needRequest = true;
    _waitingPSRDY = true;
    _waitPSRDYStart = millis();

    _fireEvent(PD_SINK_EVT_SOURCE_CAP);
}

void PD_Sink::_handlePSRDY() {
    _waitingPSRDY = false;
    
    if (_isPPS) {
        _state = PD_SINK_PPS_ACTIVE;
        _lastPPSRequest = millis();
        _fireEvent(PD_SINK_EVT_PPS_READY);
    } else {
        _state = PD_SINK_READY;
        _fireEvent(PD_SINK_EVT_POWER_READY);
    }
}

void PD_Sink::_handleReject() {
    _waitingPSRDY = false;
    _fireEvent(PD_SINK_EVT_REJECTED);
}

void PD_Sink::_handleAccept() {
    // Accept 之后等待 PS_RDY
}

void PD_Sink::_handlePPSStatus(uint32_t *data) {
    PD_PPSStatus status;
    pd_parse_pps_status(data, &status);
    if (status.output_voltage != 0xFFFF) {
        _voltage = status.output_voltage * 20;  // 20mV → mV
    }
}

// ============================================================================
// PD 操作
// ============================================================================

void PD_Sink::_evaluateAndSelect() {
    if (_sourcePDOCount == 0) {
        _selectedPosition = 0;
        return;
    }

    // 检查是否有 PPS 目标
    if (_ppsTargetVoltage > 0 && _ppsCapable) {
        PD_PDOInfo ppsInfo;
        pd_parse_pdo(_sourcePDOs[_ppsPDOIndex], &ppsInfo);
        uint16_t target50mv = _ppsTargetVoltage * 2 / 5;  // 20mV → 50mV
        
        if (target50mv >= ppsInfo.min_voltage && target50mv <= ppsInfo.max_voltage &&
            _ppsTargetCurrent <= ppsInfo.max_current) {
            _selectedPosition = _ppsPDOIndex + 1;
            _isPPS = true;
            return;
        }
    }

    // 按策略选择最佳 PDO
    _isPPS = false;
    uint8_t bestIndex = 0;
    uint16_t bestScore = 0;

    for (uint8_t i = 0; i < _sourcePDOCount; i++) {
        PD_PDOInfo info;
        pd_parse_pdo(_sourcePDOs[i], &info);
        if (info.pdo_type == PD_PDO_TYPE_AUGMENTED) continue;  // 非PPS时跳过APDO

        uint16_t v = info.max_voltage;
        uint16_t c = info.max_current;

        switch (_powerOption) {
            case PD_OPTION_MAX_5V:
                if (v <= 100 && c > bestScore) { bestScore = c; bestIndex = i; }
                break;
            case PD_OPTION_MAX_9V:
                if (v <= 180 && c > bestScore) { bestScore = c; bestIndex = i; }
                break;
            case PD_OPTION_MAX_12V:
                if (v <= 240 && c > bestScore) { bestScore = c; bestIndex = i; }
                break;
            case PD_OPTION_MAX_15V:
                if (v <= 300 && c > bestScore) { bestScore = c; bestIndex = i; }
                break;
            case PD_OPTION_MAX_20V:
                if (v <= 400 && c > bestScore) { bestScore = c; bestIndex = i; }
                break;
            case PD_OPTION_MAX_VOLTAGE:
                if (v > bestScore) { bestScore = v; bestIndex = i; }
                break;
            case PD_OPTION_MAX_CURRENT:
                if (c > bestScore) { bestScore = c; bestIndex = i; }
                break;
            case PD_OPTION_MAX_POWER: {
                uint32_t p = (uint32_t)v * c;
                if (p > bestScore) { bestScore = p; bestIndex = i; }
                break;
            }
        }
    }

    _selectedPosition = bestIndex + 1;
}

void PD_Sink::_sendRequest() {
    if (_selectedPosition == 0) return;

    PD_PDOInfo info;
    pd_parse_pdo(_sourcePDOs[_selectedPosition - 1], &info);

    uint16_t header = pd_make_header(PD_DATA_REQUEST, 1, _messageID, false);
    uint32_t reqObj;

    if (_isPPS && info.pdo_type == PD_PDO_TYPE_AUGMENTED) {
        // PPS Request: 20mV / 50mA 单位
        reqObj = ((uint32_t)_ppsTargetCurrent << 0)  |    // bit 0..6: Current (50mA)
                 ((uint32_t)_ppsTargetVoltage << 9)   |    // bit 9..19: Voltage (20mV)
                 ((uint32_t)1 << 25)                  |    // USB Comm capable
                 ((uint32_t)_selectedPosition << 28);       // Object Position
    } else {
        // Fixed Request: 50mV / 10mA 单位
        uint32_t maxCurr = info.max_current ? info.max_current : info.max_power;
        reqObj = ((uint32_t)maxCurr << 0)          |     // Max Operating Current/Power
                 ((uint32_t)maxCurr << 10)         |     // Operating Current/Power
                 ((uint32_t)1 << 25)               |     // USB Comm capable
                 ((uint32_t)_selectedPosition << 28);     // Object Position
    }

    fusb302_send_message(&_dev, header, &reqObj, 1);

    // 递增 MessageID
    _messageID = (_messageID + 1) & 0x7;
}

void PD_Sink::_sendGetSourceCap() {
    uint16_t header = pd_make_header(PD_CTRL_GET_SOURCE_CAP, 0, _messageID, false);
    fusb302_send_message(&_dev, header, nullptr, 0);
    _messageID = (_messageID + 1) & 0x7;
}

void PD_Sink::_fireEvent(PD_SinkEvent event) {
    if (_callback) _callback(this, event, _callbackData);
}

// ============================================================================
// 公开查询
// ============================================================================

PD_SinkState PD_Sink::getState() { return _state; }
bool PD_Sink::isReady() { return _state == PD_SINK_READY || _state == PD_SINK_PPS_ACTIVE; }
bool PD_Sink::isPPS() { return _state == PD_SINK_PPS_ACTIVE; }
bool PD_Sink::isConnected() { return _state >= PD_SINK_CONNECTED; }
float PD_Sink::getVoltage() { return _voltage / 1000.0f; }
float PD_Sink::getCurrent() { return _current / 1000.0f; }

bool PD_Sink::setPPS(float voltage, float current) {
    if (!_isPPS || _state != PD_SINK_PPS_ACTIVE) return false;

    _ppsTargetVoltage = (uint16_t)(voltage * 50.0f + 0.5f);
    _ppsTargetCurrent = (uint8_t)(current * 20.0f + 0.5f);
    _needRequest = true;
    return true;
}

uint8_t PD_Sink::getSourcePDOCount() { return _sourcePDOCount; }

bool PD_Sink::getSourcePDO(uint8_t index, PD_PDOInfo *info) {
    if (index >= _sourcePDOCount || !info) return false;
    pd_parse_pdo(_sourcePDOs[index], info);
    return true;
}

uint8_t PD_Sink::getSelectedPosition() { return _selectedPosition; }
bool PD_Sink::isPPSCapable() { return _ppsCapable; }

void PD_Sink::enableVBUS(bool enable) {
    // VBUS 检测控制 (通过 MASK 寄存器)
    if (enable) {
        // 使能 VBUSOK 中断
        uint8_t mask = _dev.reg_cache[0x0A - 0x01];
        mask &= ~(1 << 7);
        _dev.i2c_write(_dev.i2c_address, 0x0A, &mask, 1);
    }
}
