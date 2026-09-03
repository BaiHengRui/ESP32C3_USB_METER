#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <esp32-hal-cpu.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <Wire.h>
#include <LittleFS.h>

// 硬件引脚
#define I2C_SDA_PIN  7
#define I2C_SCL_PIN  6
#define LCD_BL_PIN   0
#define BUTTON_SW0   9   // 复用启动引脚
#define BUTTON_SW1   10

// 软件版本号 & 硬件版本号
// v Major.Minor.Patch(-branch)
#define SOFTWARE_VERSION "v2.4.4"
#define HARDWARE_VERSION "v1.0.5"

#define INA228_EN 1

// HAL 数据类型
namespace HAL
{
    typedef struct
    {
        float    voltage;
        float    current;
        float    power;
        float    energy_mWh;
        float    charge_mAh;
        float    energy_Wh;
        float    charge_Ah;
        float    temperature;
        bool     current_direction;   // true=left, false=right
        uint16_t device_id;
        uint16_t status;
    } INA22x_Data;

    #pragma pack(push, 1)
    typedef struct
    {
        uint8_t  magic;             // 0x5A 记录完整性标记
        uint32_t seq;               // 记录序号(文件内自增)
        uint64_t timestamp_us;      // 采集时刻(esp_timer, us)
        float    voltage;           // V
        float    current;           // A
        float    power;             // W
        float    temperature;       // INA ADC 温度(°C)
        float    temperature_cpu;   // CPU 温度(°C)
        float    energy_mWh;        // 累计能量
        float    charge_mAh;        // 累计电荷
        bool     current_direction; // true=left, false=right
        uint8_t  checksum;          // 异或校验
    } Storage_Record;

    typedef struct
    {
        uint8_t  magic0;       // 'M'
        uint8_t  magic1;       // 'T'
        uint8_t  version;      // 文件格式版本
        uint8_t  record_size;  // sizeof(Storage_Record)
        uint32_t reserved;     // 保留
    } Storage_FileHeader;
    #pragma pack(pop)

    #pragma pack(push, 1)
    typedef struct
    {
        uint8_t  header0; // 0x55
        uint8_t  header1; // 0xAA
        uint8_t  pack_type; // 如果是数据包分块传输，0x01=开始标记，0x02=结束标记，如果是单包传输，0x00=单包
        uint16_t  pack_index; // 分块传输时的包索引，单包传输时为0
        uint8_t  pack_length;
        // uint32_t snid;
        float    temperature_cpu;
        float    temperature_adc;
        float    voltage;
        float    current;
        float    power;
        float    energy_mWh;
        float    charge_mAh;
        uint64_t esp_time_us;
        bool     current_direction;
        uint8_t  checksum;
    } USB_CDC_Data;
    #pragma pack(pop)

    /* Storage 信息 (LittleFS 离线记录, 多条目) */
    typedef struct
    {
        bool     recording;        // 正在记录
        uint32_t entry_count;      // 条目总数
        uint32_t total_records;    // 总记录数
        uint32_t total_bytes;      // 总字节数
        uint32_t selected;         // 选中条目(0-based)
        uint32_t sel_index;        // 选中条目编号
        uint32_t sel_records;      // 选中条目记录数
        uint32_t sel_bytes;        // 选中条目字节数
        uint32_t rec_elapsed_sec;  // 本次记录时长(秒)
    } Storage_Info;

    // 存储操作结果码
    enum Storage_Result : uint8_t {
        SR_OK = 0,       // 成功
        SR_STARTED,      // 已开始记录
        SR_SAVED,        // 已停止并保存
        SR_DELETED,      // 已删除
        SR_EMPTY,        // 无条目
        SR_FULL,         // 存储已满
        SR_RECORDING,    // 正在记录中
        SR_BUSY,         // 导出进行中
        SR_ERROR,        // 失败
    };
}

// ============================================================
// HAL 功能接口
// ============================================================
namespace HAL
{
    /* System */
    void   Sys_Init();
    void   LOG_INFO(const char* fmt, ...);
    String Get_System_RunTime(uint64_t us);
    String Get_System_Status();
    float  Get_CPU_Temperature();
    void   APP_Run();

    /* NVS */
    void     NVS_Init();
    void     NVS_Load();
    uint8_t  Sys_NVS_Valid(const char* key, uint8_t default_val, uint8_t max_val = 255, uint8_t min_val = 0);
    uint8_t  Sys_NVS_Read(const char* key, uint8_t default_val);
    void     Sys_NVS_Write(const char* key, uint8_t value);
    uint32_t Sys_NVS_ReadUInt(const char* key, uint32_t default_val);
    void     Sys_NVS_WriteUInt(const char* key, uint32_t value);

    /* USB */
    void UART_Command();

    /* Storage (LittleFS 离线记录) */
    void     Storage_Init();
    void     Storage_Sample();          // 采集任务周期调用, 按记录间隔入队
    void     Storage_AutoControl();     // 阈值自动开始/停止记录(采集任务周期调用)
    void     Storage_AutoControl_Reset(); // 复位自动记录防抖计时(修改阈值后调用)
    void     Storage_Task();            // 存储任务主体(阻塞在队列)
    void     Storage_Flush();           // 刷新 RAM 缓冲到文件
    Storage_Result Storage_ToggleRecord();   // SW1 长按: 开始/停止记录
    void     Storage_SelectNext();           // SW1 短按: 选中下一条目
    Storage_Result Storage_DeleteSelected(); // SW1 双击: 删除选中条目
    // 按钮任务委托接口: 仅入队, FS 操作由存储任务执行(避免按钮任务栈上做 LittleFS)
    void     Storage_RequestSelectNext();
    void     Storage_RequestDeleteSelected();
    void     Storage_RequestToggleRecord();
    const Storage_Info& Storage_GetInfo();
    bool     Storage_Export(const char* filename); // 分块导出
    bool     Storage_Erase();
    bool     Storage_IsFull();

    /* PD */
    // void PD_Init();
    // void PD_GetData(PDO_Data *data);
    // String PD_Protocol();

    /* Button */
    void Button_Init();
    void Button_Click();

    /* INA */
    bool INA22x_Init();
    void INA22x_GetData(INA22x_Data *data);
    void INA22x_SetConfig(uint8_t sample_mode);

    /* LCD */
    void  LCD_Init();
    void  LCD_SetBrightness(uint8_t brightness);
    void  LCD_SetRotation(uint8_t rotation);
    void  LCD_SetTextColor(uint16_t color);
    void  LCD_Refresh_Screen(uint32_t bgcolor);
    float Get_FPS();

    /* Graph */
    void Update_Graph_Data();

    /* Threshold Timing */
    void   Threshold_Timing_Update();
    String Get_Threshold_Time();
    void   Threshold_Timing_Reset();   // 复位计时状态与防抖计时

    /* Safe Rotation (帧间切换，避免 SPI 竞态) */
    void ApplyPendingRotation();
} // namespace HAL

// ============================================================
// 应用状态枚举
// ============================================================
namespace AppState
{
    constexpr uint8_t MAIN        = 0;
    constexpr uint8_t WAVEGRAPH   = 1;
    constexpr uint8_t STOREAGE_DATA = 2;
    constexpr uint8_t MENU        = 3;
    constexpr uint8_t SYSTEM_INFO = 4;
}

// ============================================================
// 子模块
// ============================================================
#include "globals.h"