#include "hal.h"

static uint64_t thrStartTimeUs = 0;     // 计时开始时刻(us)
static uint64_t thrStartDebounceUs = 0; // 起始条件防抖起始时刻(us), 0=未在防抖
static uint64_t thrStopDebounceUs = 0;  // 结束条件防抖起始时刻(us), 0=未在防抖

#define THR_DEBOUNCE_US 100000  // 防抖时间 100ms

// 计时阈值更新, 需要在INA数据刷新后调用
void HAL::Threshold_Timing_Update() {
    uint32_t voltage_mV = (uint32_t)(INA.voltage * 1000.0f);  // V -> mV
    uint32_t current_mA = (uint32_t)(INA.current * 1000.0f);  // A -> mA

    // 起始条件: 电压>=起始电压阈值 AND 电流>=起始电流阈值 (0=无限制)
    bool startCondV = (thrStartVMv == 0) || (voltage_mV >= thrStartVMv);
    bool startCondI = (thrStartIMa == 0) || (current_mA >= thrStartIMa);
    bool shouldStart = startCondV && startCondI;

    // 结束条件: 电压<=结束电压阈值 OR 电流<=结束电流阈值
    // (任一满足即结束, 规避PD协议后期电流接近0但电压仍高导致不停止的情况; 0=该项不生效)
    bool endCondV = (thrEndVMv != 0) && (voltage_mV <= thrEndVMv);
    bool endCondI = (thrEndIMa != 0) && (current_mA <= thrEndIMa);
    bool shouldStop = endCondV || endCondI;

    uint64_t nowUs = esp_timer_get_time();

    // 未计时: 起始条件需连续满足 100ms 才触发, 中途不满足则重新防抖
    if (!thrTimingActive) {
        if (shouldStart) {
            if (thrStartDebounceUs == 0) {
                thrStartDebounceUs = nowUs;
            } else if ((nowUs - thrStartDebounceUs) >= THR_DEBOUNCE_US) {
                // 开始计时 (不清零, 在上次累积基础上继续)
                thrTimingActive = true;
                thrStartTimeUs = nowUs;
                thrStartDebounceUs = 0;
            }
        } else {
            thrStartDebounceUs = 0;
        }
    }

    // 计时中: 结束条件需连续满足 100ms 才触发, 中途恢复则不停止
    if (thrTimingActive) {
        if (shouldStop) {
            if (thrStopDebounceUs == 0) {
                thrStopDebounceUs = nowUs;
            } else if ((nowUs - thrStopDebounceUs) >= THR_DEBOUNCE_US) {
                // 停止计时, 累加本次计时段
                thrTimingActive = false;
                thrElapsedUs += nowUs - thrStartTimeUs;
                thrStopDebounceUs = 0;
            }
        } else {
            thrStopDebounceUs = 0;
        }
    }
}

// 复位阈值计时全部状态 (含防抖计时), 修改阈值后调用
void HAL::Threshold_Timing_Reset() {
    thrTimingActive    = false;
    thrElapsedUs       = 0;
    thrStartTimeUs     = 0;
    thrStartDebounceUs = 0;
    thrStopDebounceUs  = 0;
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
