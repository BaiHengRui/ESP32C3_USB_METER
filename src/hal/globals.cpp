// ============================================================
// 全局变量定义 — 所有 extern 声明在此赋初值
// ============================================================
#include "hal.h"

// System
uint64_t SNID(0);
int32_t  nowTime(0), lastTime(0);
int64_t  nowTime_us(0), lastTime_us(0);
int32_t  startTime(0);
uint8_t  nowApp(0), maxApp(3);

HAL::INA22x_Data INA;

// Display 
uint8_t  defaultBrightness = 50;
uint8_t  defaultRotation   = 3;
uint8_t  currentRotation   = 3;
int8_t   pendingRotation   = -1;
uint8_t  sample_mode       = 0;   // 0=Fast, 1=Normal, 2=Slow

// Graph
float    voltageBuffer[GRAPH_WIDTH] = {0};
float    currentBuffer[GRAPH_WIDTH] = {0};
int      graphIndex          = 0;
bool     graphDataInitialized = false;
bool     graphRangeInitialized = false;
bool     graphPaused          = false;
float    vDisplayMin = 0.0f, vDisplayMax = 5.0f, vHistoryMax = 0.0f;
float    iDisplayMin = 0.0f, iDisplayMax = 2.0f, iHistoryMax = 0.0f;
float    frozenVoltage = 0.0f, frozenCurrent = 0.0f;

// Threshold Timing
uint32_t thrStartVMv     = 0;
uint32_t thrStartIMa     = 0;
uint32_t thrEndVMv       = 0;
uint32_t thrEndIMa       = 0;
bool     thrTimingActive = false;
uint64_t thrElapsedUs    = 0;

// Toast
char     toastMessage[40] = "";
uint32_t toastStartTime   = 0;
