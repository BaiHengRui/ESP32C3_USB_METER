// ============================================================
// 全局变量定义 — 所有 extern 声明在此赋初值
// ============================================================
#include "hal.h"

// I2C 总线互斥锁
SemaphoreHandle_t xWireMutex = NULL;

// System
uint64_t SNID(0);
int32_t  nowTime(0), lastTime(0);
int64_t  nowTime_us(0), lastTime_us(0);
int32_t  startTime(0);
uint8_t  nowApp(0), maxApp(4);

HAL::INA22x_Data INA;

// Display 
uint8_t  defaultBrightness = 50;
uint8_t  defaultRotation   = 3;
uint8_t  currentRotation   = 3;
int8_t   pendingRotation   = -1;
uint8_t  sample_mode       = 0;   // 0=Fast, 1=Normal, 2=Slow
uint8_t  ui_effects        = 1;   // 默认启用UI过渡动效

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

// Storage (LittleFS 离线记录)
uint32_t          record_interval_s    = 1;      // 默认 1 秒
volatile bool     storage_full         = false;
volatile bool     storage_exporting    = false;
volatile uint32_t storage_record_count = 0;
volatile uint32_t storage_file_bytes   = 0;
uint8_t          threshold_auto        = 0;   // 默认关闭
QueueHandle_t     xStorageQueue        = NULL;
SemaphoreHandle_t xStorageMutex        = NULL;

// Toast
char     toastMessage[40] = "";
uint32_t toastStartTime   = 0;
