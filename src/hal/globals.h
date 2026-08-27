#pragma once
// ============================================================
// 全局变量声明 + Toast 通知系统
// 集中声明，按模块分组；定义见 globals.cpp
// ============================================================
#include <stdint.h>
#include <freertos/semphr.h>

// I2C 总线互斥锁 (INA 与其它外设共享 Wire)
extern SemaphoreHandle_t xWireMutex;

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
extern uint8_t  ui_effects;        // 1=启用UI过渡动效, 0=禁用

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

// Storage (LittleFS 离线记录)
extern uint32_t           record_interval_s;   // 记录间隔(秒), 0=关闭
volatile extern bool      storage_full;        // 已满, 停止记录
volatile extern bool      storage_exporting;   // 导出进行中
volatile extern uint32_t  storage_record_count;// 当前记录数
volatile extern uint32_t  storage_file_bytes;  // 当前文件字节数
extern uint8_t            threshold_auto;      // 阈值自动开始/停止记录 0=关 1=开
extern QueueHandle_t      xStorageQueue;
extern SemaphoreHandle_t  xStorageMutex;

// Toast 通知系统
#define TOAST_DURATION_MS 1500

extern char     toastMessage[40];
extern uint32_t toastStartTime;

namespace HAL
{
    void ShowToast(const char* msg);
    bool IsToastActive();
}
