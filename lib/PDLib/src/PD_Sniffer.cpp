/**
 * @file    PD_Sniffer.cpp
 * @brief   PD 总线被动监听器实现
 * 
 * 核心: 将 FUSB302 配置为 MODE_PASSIVE, 纯监听不干扰
 */

#include <string.h>
#include <Arduino.h>
#include <Wire.h>
#include "PD_Sniffer.h"

// ============================================================================
// 静态 I2C 回调 — 使用 Arduino Wire 库
// ============================================================================

// 全局设备指针 (仅用于 I2C 回调, 因为静态函数无 this 指针)
// 简单场景下只有一个 Sniffer, 使用全局变量即可
static PD_Sniffer * g_activeSniffer = nullptr;

int PD_Sniffer::_i2cRead(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t count) {
    Wire.beginTransmission(devAddr);
    Wire.write(regAddr);
    if (Wire.endTransmission(false) != 0) return -1;
    
    uint8_t n = Wire.requestFrom(devAddr, count);
    if (n != count) return -1;
    for (uint8_t i = 0; i < count; i++) {
        data[i] = Wire.read();
    }
    return 0;
}

int PD_Sniffer::_i2cWrite(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t count) {
    Wire.beginTransmission(devAddr);
    Wire.write(regAddr);
    Wire.write(data, count);
    return Wire.endTransmission() == 0 ? 0 : -1;
}

int PD_Sniffer::_delayMs(uint32_t ms) {
    delay(ms);
    return 0;
}

// ============================================================================
// 构造与初始化
// ============================================================================

PD_Sniffer::PD_Sniffer() {
    memset(&_dev, 0, sizeof(_dev));
    _intPin = 0;
    _initialized = false;
    _callback = nullptr;
    _callbackData = nullptr;
    
    _connected = false;
    _ccPin = 0;
    _cc1Level = 0;
    _cc2Level = 0;
    
    memset(_sourcePDOs, 0, sizeof(_sourcePDOs));
    _sourcePDOCount = 0;
    _sourcePPSCapable = false;
    _ppsPDOIndex = 0;
    _ppsMinVoltage = 0;
    _ppsMaxVoltage = 0;
    _ppsMaxCurrent = 0;
    
    _powerStatus = PD_POWER_NONE;
    _voltage = 5000;     // 默认 5V
    _current = 1000;     // 默认 1A
    _selectedPosition = 0;
    
    _ppsOutputVoltage = 0xFFFF;
    _ppsOutputCurrent = 0xFF;
    _ppsTempFlag = 0;
    
    _packetCount = 0;
    _goodCRCCount = 0;
    _rejectCount = 0;
    _startTime = 0;
    _lastHeader = 0;
}

bool PD_Sniffer::begin(uint8_t intPin, uint8_t i2cAddr) {
    _intPin = intPin;
    
    // 设置 INT 引脚为输入上拉
    pinMode(intPin, INPUT_PULLUP);
    
    // 初始化 Wire (用户在外部已调用 Wire.begin())
    
    // 保存全局指针用于静态回调
    g_activeSniffer = this;
    
    // 初始化 FUSB302 为被动模式
    FUSB302_Status status = fusb302_init(&_dev, i2cAddr, _i2cRead, _i2cWrite, _delayMs);
    if (status != FUSB302_OK) {
        Serial.printf("[Sniffer] FUSB302 init failed: %s\n", fusb302_get_error(&_dev));
        return false;
    }
    
    // 设置为被动模式 (关键!)
    fusb302_set_mode(&_dev, FUSB302_MODE_PASSIVE);
    
    // 关闭 VBUS 检测 — PPS 电压可能 <4V (FUSB302 VBUS 阈值),
    // 否则低电压时芯片误判 VBUS 断开, 导致状态重置
    fusb302_enable_vbus_sense(&_dev, false);
    
    _initialized = true;
    _startTime = millis();
    
    uint8_t ver, rev;
    fusb302_get_device_id(&_dev, &ver, &rev);
    Serial.printf("[Sniffer] FUSB302 passive mode ready | DevID: %c_rev%c | INT pin: %d\n",
                  'A' + ver, 'A' + rev, intPin);
    
    return true;
}

void PD_Sniffer::onEvent(PD_SnifferCallback cb, void *userData) {
    _callback = cb;
    _callbackData = userData;
}

// ============================================================================
// 主循环 — 双重条件: INT引脚 + 额外追赶读取
// ============================================================================

void PD_Sniffer::update() {
    if (!_initialized) return;
    
    // FUSB302 的 INT 引脚在读取中断寄存器后会立即拉高,
    // 即使 RX FIFO 中还有未读消息。因此不能仅依赖 INT 引脚。
    // 
    // 策略: 当 INT=低 时持续读取; INT 拉高后额外追赶 3 次,
    //       确保 FIFO 中的积压消息全部被取出。
    
    uint8_t loopGuard = 0;
    bool gotData = false;           // 本轮是否收到数据
    uint8_t extraReads = 0;         // INT拉高后的额外追赶次数
    
    while (loopGuard < 30) {
        loopGuard++;
        
        // 退出条件: INT高 且 未收到数据 且 已额外追赶过
        if (digitalRead(_intPin) == HIGH && !gotData && extraReads > 0) break;
        
        // 如果 INT 高但我们还有额外追赶额度, 计数并继续
        if (digitalRead(_intPin) == HIGH && extraReads < 5) {
            extraReads++;
        }
        
        FUSB302_Event events = FUSB302_EVT_NONE;
        FUSB302_Status ret = fusb302_poll(&_dev, &events);
        if (ret != FUSB302_OK) break;
        if (events == FUSB302_EVT_NONE) {
            // 无事件: 如果 INT 已高, 退出
            if (digitalRead(_intPin) == HIGH) break;
            continue;
        }
        
        gotData = true;
        extraReads = 0;  // 收到数据, 重置追赶计数
        
        // --- 处理连接/断开 ---
        if (events & FUSB302_EVT_DETACHED) {
            _handleDetached();
        }
        
        if (events & FUSB302_EVT_ATTACHED) {
            _handleAttached();
        }
        
        // --- 处理收到消息 ---
        if (events & FUSB302_EVT_RX_SOP) {
            uint16_t header;
            uint32_t data[7];
            fusb302_read_message(&_dev, &header, data);
            _handleRXMessage(header, data);
        }
        
        // --- GoodCRC (被动模式通常不应出现) ---
        if (events & FUSB302_EVT_GOOD_CRC_SENT) {
            _goodCRCCount++;
        }
        
        // --- Hard Reset ---
        if (events & FUSB302_EVT_HARD_RESET) {
            _fireEvent(PD_SNIFF_EVT_HARD_RESET);
        }
    }
}

// ============================================================================
// 连接/断开处理
// ============================================================================

void PD_Sniffer::_handleAttached() {
    _connected = true;
    
    // 获取 CC 电平
    fusb302_get_cc_levels(&_dev, &_cc1Level, &_cc2Level);
    
    // 判断 CC 引脚
    if (_cc1Level > 0 && _cc2Level == 0) {
        _ccPin = 1;
    } else if (_cc2Level > 0 && _cc1Level == 0) {
        _ccPin = 2;
    } else if (_cc1Level > 0 && _cc2Level > 0) {
        _ccPin = 1;  // 两者都有效, 选 CC1
    } else {
        _ccPin = 0;
    }
    
    // 重置状态
    _powerStatus = PD_POWER_NONE;
    _voltage = 5000;
    _current = 500;
    _selectedPosition = 0;
    _sourcePDOCount = 0;
    _sourcePPSCapable = false;
    memset(_sourcePDOs, 0, sizeof(_sourcePDOs));
    _packetCount = 0;
    _goodCRCCount = 0;
    _rejectCount = 0;
    _ppsOutputVoltage = 0xFFFF;
    _ppsOutputCurrent = 0xFF;
    
    _fireEvent(PD_SNIFF_EVT_ATTACHED);
}

void PD_Sniffer::_handleDetached() {
    _connected = false;
    _ccPin = 0;
    _cc1Level = 0;
    _cc2Level = 0;
    _powerStatus = PD_POWER_NONE;
    _voltage = 0;
    _current = 0;
    _selectedPosition = 0;
    _sourcePDOCount = 0;
    _sourcePPSCapable = false;
    memset(_sourcePDOs, 0, sizeof(_sourcePDOs));
    
    _fireEvent(PD_SNIFF_EVT_DETACHED);
}

// ============================================================================
// 消息处理
// ============================================================================

void PD_Sniffer::_handleRXMessage(uint16_t header, uint32_t *data) {
    _packetCount++;
    _lastHeader = header;
    
    PD_HeaderInfo hdr;
    pd_parse_header(header, &hdr);
    
    // 调试: 打印每条收到的原始消息
    Serial.printf("[PD-SNIFF] raw=0x%04X type=0x%02X(%s) ext=%d objs=%d id=%d\r\n",
                  header, hdr.msg_type, pd_get_message_name(header),
                  hdr.is_extended, hdr.num_objects, hdr.message_id);
    
    // 打印原始数据对象用于诊断
    if (hdr.num_objects > 0 && data) {
        for (uint8_t i = 0; i < hdr.num_objects && i < 4; i++) {
            Serial.printf("[PD-SNIFF]   obj[%d]=0x%08lX\r\n", i, data[i]);
        }
    }
    
    // 根据消息类型分发
    if (pd_is_source_cap(header)) {
        Serial.println("[PD-SNIFF] >>> SOURCE_CAP detected!");
        _handleSourceCap(header, data);
    } else if (pd_is_request(header)) {
        Serial.println("[PD-SNIFF] >>> REQUEST detected!");
        _handleRequest(header, data);
    } else if (pd_is_ps_rdy(header)) {
        Serial.println("[PD-SNIFF] >>> PS_RDY detected!");
        _handlePSRDY();
    } else if (pd_is_reject(header)) {
        _rejectCount++;
        _fireEvent(PD_SNIFF_EVT_REJECT);
    } else if (pd_is_pps_status(header)) {
        _handlePPSStatus(data);
    }
}

void PD_Sniffer::_handleSourceCap(uint16_t header, uint32_t *data) {
    PD_HeaderInfo hdr;
    pd_parse_header(header, &hdr);
    
    _sourcePDOCount = (hdr.num_objects <= PD_SNIFFER_MAX_PDO) ? hdr.num_objects : PD_SNIFFER_MAX_PDO;
    _sourcePPSCapable = false;
    
    for (uint8_t i = 0; i < _sourcePDOCount; i++) {
        _sourcePDOs[i] = data[i];
        
        PD_PDOInfo pdoInfo;
        pd_parse_pdo(data[i], &pdoInfo);
        
        if (pdoInfo.is_pps) {
            _sourcePPSCapable = true;
            _ppsPDOIndex = i;
            _ppsMinVoltage = pdoInfo.min_voltage;
            _ppsMaxVoltage = pdoInfo.max_voltage;
            _ppsMaxCurrent = pdoInfo.max_current;
        }
    }
    
    _fireEvent(PD_SNIFF_EVT_SOURCE_CAP);
}

void PD_Sniffer::_handleRequest(uint16_t header, uint32_t *data) {
    PD_RequestInfo req;
    pd_parse_request(data[0], &req);  // 先按 Fixed 格式解析基础字段
    
    _selectedPosition = req.object_position;
    
    // 关键: 根据目标 PDO 类型判断是 PPS 还是 Fixed
    // PPS APDO 的 type = 3 (PD_PDO_TYPE_AUGMENTED)
    if (req.object_position > 0 && req.object_position <= _sourcePDOCount) {
        uint32_t targetPDO = _sourcePDOs[req.object_position - 1];
        uint8_t pdoType = (targetPDO >> 30) & 0x3;
        
        if (pdoType == PD_PDO_TYPE_AUGMENTED) {
            // --- PPS 请求: 重新按 PPS 格式解析 ---
            pd_parse_request_pps(data[0], &req);
            _powerStatus = PD_POWER_PPS;
            _voltage = req.operating_voltage * 20;    // 20mV → mV
            _current = req.operating_current * 50;     // 50mA → mA
            Serial.printf("[PD-SNIFF] PPS Request: %umV %umA pos=%d\r\n",
                          _voltage, _current, req.object_position);
        } else {
            // --- Fixed/Variable/Battery 请求 ---
            PD_PDOInfo pdoInfo;
            pd_parse_pdo(targetPDO, &pdoInfo);
            _powerStatus = PD_POWER_FIXED;
            _voltage = pdoInfo.max_voltage * 50;        // 50mV → mV (电压来自PDO)
            _current = req.operating_current * 10;       // 10mA → mA (电流来自Request)
            Serial.printf("[PD-SNIFF] Fixed Request: PDO#%d %umV %umA\r\n",
                          req.object_position, _voltage, _current);
        }
    } else {
        // Position 无效, 保持上次状态不变
        Serial.printf("[PD-SNIFF] Request pos=%d out of range (max=%d)\r\n",
                      req.object_position, _sourcePDOCount);
    }
    
    _fireEvent(PD_SNIFF_EVT_REQUEST);
}

void PD_Sniffer::_handlePSRDY() {
    _fireEvent(PD_SNIFF_EVT_PS_RDY);
}

void PD_Sniffer::_handlePPSStatus(uint32_t *data) {
    PD_PPSStatus status;
    pd_parse_pps_status(data, &status);
    
    if (status.output_voltage != 0xFFFF) {
        _ppsOutputVoltage = status.output_voltage;
    }
    if (status.output_current != 0xFF) {
        _ppsOutputCurrent = status.output_current;
    }
    _ppsTempFlag = status.ptf;
    
    // 使用 PPS_Status 更新电压电流 (更精确)
    if (status.output_voltage != 0xFFFF) {
        _voltage = status.output_voltage * 20;  // 20mV → mV
    }
    if (status.output_current != 0xFF) {
        _current = status.output_current * 50;  // 50mA → mA
    }
    _powerStatus = PD_POWER_PPS;
    
    _fireEvent(PD_SNIFF_EVT_PPS_STATUS);
}

void PD_Sniffer::_updatePowerFromRequest(const PD_RequestInfo *req) {
    if (!req) return;
    // 已被 _handleRequest 内联处理
}

void PD_Sniffer::_fireEvent(PD_SnifferEvent event) {
    if (_callback) {
        _callback(this, event, _callbackData);
    }
}

// ============================================================================
// 公开查询 API
// ============================================================================

float PD_Sniffer::getVoltage() {
    return _voltage / 1000.0f;  // mV → V
}

float PD_Sniffer::getCurrent() {
    return _current / 1000.0f;  // mA → A
}

float PD_Sniffer::getPower() {
    return (_voltage * _current) / 1000000.0f;  // mW → W
}

PD_PowerStatus PD_Sniffer::getPowerStatus() {
    return _powerStatus;
}

bool PD_Sniffer::isPPS() {
    return _powerStatus == PD_POWER_PPS;
}

bool PD_Sniffer::isConnected() {
    return _connected;
}

uint8_t PD_Sniffer::getSelectedPosition() {
    return _selectedPosition;
}

uint8_t PD_Sniffer::getSourcePDOCount() {
    return _sourcePDOCount;
}

bool PD_Sniffer::getSourcePDO(uint8_t index, PD_PDOInfo *info) {
    if (index >= _sourcePDOCount || !info) return false;
    pd_parse_pdo(_sourcePDOs[index], info);
    return true;
}

uint32_t PD_Sniffer::getSourcePDORaw(uint8_t index) {
    if (index >= _sourcePDOCount) return 0;
    return _sourcePDOs[index];
}

bool PD_Sniffer::isSourcePPSCapable() {
    return _sourcePPSCapable;
}

float PD_Sniffer::getPPSMinVoltage() {
    return _ppsMinVoltage * 0.05f;
}

float PD_Sniffer::getPPSMaxVoltage() {
    return _ppsMaxVoltage * 0.05f;
}

float PD_Sniffer::getPPSMaxCurrent() {
    return _ppsMaxCurrent * 0.05f;
}

uint8_t PD_Sniffer::getCCPin() {
    return _ccPin;
}

uint8_t PD_Sniffer::getCC1Level() {
    return _cc1Level;
}

uint8_t PD_Sniffer::getCC2Level() {
    return _cc2Level;
}

uint32_t PD_Sniffer::getPacketCount() {
    return _packetCount;
}

uint32_t PD_Sniffer::getGoodCRCCount() {
    return _goodCRCCount;
}

uint32_t PD_Sniffer::getRejectCount() {
    return _rejectCount;
}

const char * PD_Sniffer::getLastMessageName() {
    return pd_get_message_name(_lastHeader);
}

uint32_t PD_Sniffer::getUptime() {
    return millis() - _startTime;
}

float PD_Sniffer::getPPSOutputVoltage() {
    if (_ppsOutputVoltage == 0xFFFF) return 0;
    return _ppsOutputVoltage * 0.02f;
}

float PD_Sniffer::getPPSOutputCurrent() {
    if (_ppsOutputCurrent == 0xFF) return 0;
    return _ppsOutputCurrent * 0.05f;
}

uint8_t PD_Sniffer::getPPSTempFlag() {
    return _ppsTempFlag;
}

void PD_Sniffer::resetStats() {
    _packetCount = 0;
    _goodCRCCount = 0;
    _rejectCount = 0;
}

void PD_Sniffer::enableVBUSSense(bool enable) {
    fusb302_enable_vbus_sense(&_dev, enable);
}
