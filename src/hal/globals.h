#pragma once
// ============================================================
// 全局变量声明 + Toast 通知系统
// 集中声明，按模块分组；定义见 globals.cpp
// ============================================================
#include <stdint.h>

// System
extern uint64_t SNID;
extern int32_t  nowTime, lastTime;
extern int64_t  nowTime_us, lastTime_us;
extern int32_t  startTime;
extern uint8_t  nowApp, maxApp;

extern HAL::INA22x_Data INA;

// Display
extern uint8_t  defaultBrightness;
extern uint8_t  defaultRotation;
extern uint8_t  currentRotation;
extern int8_t   pendingRotation;   // -1=无待处理, 0-3=目标方向
extern uint8_t  sample_mode;       // 0=Fast, 1=Normal, 2=Slow

// Graph
#define GRAPH_WIDTH 180

extern float    voltageBuffer[GRAPH_WIDTH];
extern float    currentBuffer[GRAPH_WIDTH];
extern int      graphIndex;
extern bool     graphDataInitialized;
extern bool     graphRangeInitialized;
extern bool     graphPaused;
extern float    vDisplayMin, vDisplayMax, vHistoryMax;
extern float    iDisplayMin, iDisplayMax, iHistoryMax;
extern float    frozenVoltage, frozenCurrent;

// Threshold Timing
extern uint32_t thrStartVMv;
extern uint32_t thrStartIMa;
extern uint32_t thrEndVMv;
extern uint32_t thrEndIMa;
extern bool     thrTimingActive;
extern uint64_t thrElapsedUs;

// Toast 通知系统
#define TOAST_DURATION_MS 1500

extern char     toastMessage[40];
extern uint32_t toastStartTime;

namespace HAL
{
    void ShowToast(const char* msg);
    bool IsToastActive();
}
