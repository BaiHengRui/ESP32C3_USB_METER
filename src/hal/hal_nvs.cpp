#include "hal.h"

Preferences Prefs;  // NVS Preferences object

// NVS Initialization and Loading Functions
void HAL::NVS_Init() {
    Prefs.begin("config", false);
    Serial.println("NVS Init!");
}

// Load all settings from NVS into global variables
void HAL::NVS_Load() {
    // LCD 设置
    defaultBrightness = HAL::Sys_NVS_Valid("light", 50, 100, 1);
    defaultRotation   = HAL::Sys_NVS_Valid("rotation", 3, 3);
    // INA 采样模式
    sample_mode = HAL::Sys_NVS_Valid("sample_mode", 0, 2);
    // UI 过渡动效
    ui_effects = HAL::Sys_NVS_Valid("ui_effects", 1, 1, 0);
    // 计时阈值
    thrStartVMv = HAL::Sys_NVS_ReadUInt("thr_sv", 0);
    thrStartIMa = HAL::Sys_NVS_ReadUInt("thr_si", 0);
    thrEndVMv   = HAL::Sys_NVS_ReadUInt("thr_ev", 0);
    thrEndIMa   = HAL::Sys_NVS_ReadUInt("thr_ei", 0);
    // 离线记录间隔(秒), 0=关闭
    record_interval_s = HAL::Sys_NVS_ReadUInt("record_s", 1);
    if (record_interval_s > 3600) record_interval_s = 1;
    // 阈值自动开始/停止记录
    threshold_auto = HAL::Sys_NVS_Valid("thr_auto", 0, 1, 0);
}

// NVS Read/Write Helper Functions
uint8_t HAL::Sys_NVS_Valid(const char* key, uint8_t default_val, uint8_t max_val, uint8_t min_val) {
    uint8_t value = Prefs.getUChar(key, default_val);
    if (value > max_val || value < min_val) {
        value = default_val;
        Prefs.putUChar(key, value);
    }
    return value;
}

uint8_t HAL::Sys_NVS_Read(const char* key, uint8_t default_val) {
    return Prefs.getUChar(key, default_val);
}

void HAL::Sys_NVS_Write(const char* key, uint8_t value) {
    Prefs.putUChar(key, value);
}

// NVS Read/Write for uint32_t
uint32_t HAL::Sys_NVS_ReadUInt(const char* key, uint32_t default_val) {
    return Prefs.getUInt(key, default_val);
}

void HAL::Sys_NVS_WriteUInt(const char* key, uint32_t value) {
    Prefs.putUInt(key, value);
}
