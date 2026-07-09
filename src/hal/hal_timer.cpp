#include "hal.h"

// 计时阈值变量
uint32_t thrStartVMv = 0;      // 起始电压阈值(mV), 0=无限制
uint32_t thrStartIMa = 0;      // 起始电流阈值(mA), 0=无限制
uint32_t thrEndVMv = 0;        // 结束电压阈值(mV), 0=无限制
uint32_t thrEndIMa = 0;        // 结束电流阈值(mA), 0=无限制
bool thrTimingActive = false;  // 计时是否激活
uint64_t thrElapsedUs = 0;     // 已计时时间(us)
static uint64_t thrStartTimeUs = 0;  // 计时开始时刻(us)

// 计时阈值更新, 需要在INA数据刷新后调用
void HAL::Threshold_Timing_Update() {
    uint32_t voltage_mV = (uint32_t)(INA.voltage * 1000.0f);  // V -> mV
    uint32_t current_mA = (uint32_t)(INA.current * 1000.0f);  // A -> mA

    // 检查起始条件: 电压>起始电压阈值 OR 电流>起始电流阈值 (0=无限制)
    bool startCondV = (thrStartVMv == 0) || (voltage_mV > thrStartVMv);
    bool startCondI = (thrStartIMa == 0) || (current_mA > thrStartIMa);
    bool shouldStart = startCondV || startCondI;

    // 检查结束条件: 电压<结束电压阈值 OR 电流<结束电流阈值 (0=无限制则永不触发)
    bool endCondV = (thrEndVMv != 0) && (voltage_mV < thrEndVMv);
    bool endCondI = (thrEndIMa != 0) && (current_mA < thrEndIMa);
    bool shouldStop = endCondV || endCondI;

    if (!thrTimingActive && shouldStart) {
        // 开始计时 (不清零, 在上次累积基础上继续)
        thrTimingActive = true;
        thrStartTimeUs = esp_timer_get_time();
    } else if (thrTimingActive && shouldStop) {
        // 停止计时, 累加本次计时段
        thrTimingActive = false;
        thrElapsedUs += esp_timer_get_time() - thrStartTimeUs;
    }
}

// 获取格式化的阈值计时字符串 (HH:MM:SS)
String HAL::Get_Threshold_Time() {
    uint64_t elapsed = thrElapsedUs;
    if (thrTimingActive) {
        elapsed += esp_timer_get_time() - thrStartTimeUs;  // 累积 + 当前段
    }

    if (elapsed == 0 && !thrTimingActive) {
        return " -- : -- : -- ";
    }

    uint64_t totalSec = elapsed / 1000000;
    uint32_t hours   = totalSec / 3600;
    uint32_t minutes = (totalSec % 3600) / 60;
    uint32_t seconds = totalSec % 60;

    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hours, minutes, seconds);
    return String(buffer);
}
