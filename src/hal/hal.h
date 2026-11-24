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
#define SOFTWARE_VERSION "v2.5.0"
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

    /* PD 挡位信息 (FUSB302 / USB Power Delivery) */
    enum PD_SupplyType : uint8_t {
        PD_FIXED = 0,
        PD_BATTERY,
        PD_VARIABLE,
        PD_PPS,
        PD_AVS,
        PD_UNKNOWN,
    };

    typedef struct
    {
        uint8_t  type;        // PD_SupplyType
        uint8_t  position;    // 1-based PDO position
        uint16_t min_mV;
        uint16_t max_mV;
        uint16_t max_mA;
        uint32_t max_mW;
    } PD_SupplyInfo;

    typedef struct
    {
        bool     available;   // FUSB302 芯片可用
        bool     connected;   // 检测到 Source
        bool     caps_valid;  // 已收到 Source_Capabilities
        bool     monitor;     // true = passive monitor, false = sink
        uint8_t  cc;          // monitored / active CC (0 = unknown)
        uint8_t  revision;    // 1=PD2.0, 2=PD3.x
        uint8_t  pdo_count;   // 有效挡位数
        uint8_t  selected;    // UI 光标位置
        uint32_t updated_ms;  // 最近更新时间
        uint32_t rx_frames;   // 已接收的 PD 报文数
        uint32_t rx_errors;   // CRC/帧错误数
        uint32_t rx_overflow; // RX FIFO 溢出次数(丢包)
        uint32_t rx_last_ms;  // 最近一次收到报文的时间
        uint8_t  status0;     // FUSB302 STATUS0 (bit6 = BMC ACTIVITY)
        uint8_t  status1;     // FUSB302 STATUS1 (bit5 = RX_EMPTY)
        uint8_t  irq;         // FUSB302 INTERRUPT 快照
        bool     request_valid;    // 已解析到当前 RDO
        uint8_t  request_position; // RDO 选中的 PDO 位置(1-based)
        uint16_t request_mV;       // RDO 实际工作电压
        uint16_t request_mA;       // RDO 实际工作电流
        PD_SupplyInfo pdo[7];
    } PD_Info;

    /* Cable e-Marker (SOP' Discover Identity) */
    enum EMark_State : uint8_t {
        EMARK_IDLE = 0,
        EMARK_READING,
        EMARK_OK,
        EMARK_NO_CABLE,
        EMARK_ERROR,
    };

    typedef struct
    {
        uint8_t  state;          // EMark_State
        bool     valid;          // 已解码
        bool     active_cable;
        bool     epr_capable;
        bool     vbus_through;
        bool     sop2_present;
        bool     current_assumed; // passive v1.0 保留电流码按 3A
        uint8_t  layout;         // fusb302::CableLayout
        uint8_t  speed_code;
        uint16_t vid;
        uint16_t pid;
        uint16_t bcd_device;
        uint32_t xid;
        uint16_t max_current_mA;
        uint16_t max_voltage_mV;
        uint8_t  error;          // fusb302::Error
        uint32_t updated_ms;
    } EMark_Info;
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

    /* PD (FUSB302) */
    void PD_Init();                  // 初始化 FUSB302 并配置为 Sink
    void PD_Task();                  // PD 任务周期调用: 轮询/解析 Source_Capabilities
    void PD_GetInfo(PD_Info& info);  // 线程安全地复制当前 PD 挡位信息
    void PD_SelectNext();            // UI: 切换挡位光标
    void PD_SelectPrev();
    void PD_ToggleCC();              // 监听模式: 运行时切换 CC1/CC2
    void PD_AllowSoftReset(bool allow); // 菜单开关: 允许主动 Soft Reset 重新协商

    /* Cable e-Marker */
    void EMark_RequestRead();        // 请求读取 SOP' e-Marker (PD 任务执行)
    void EMark_GetInfo(EMark_Info& info);

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
    void  LCD_PushSprite();          // 整屏推送(DMA 可用时走 GDMA)
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
    // Main pages reachable by SW0 page cycling (0..maxApp).
    constexpr uint8_t MAIN        = 0;
    constexpr uint8_t WAVEGRAPH   = 1;
    constexpr uint8_t STOREAGE_DATA = 2;
    constexpr uint8_t MENU        = 3;
    constexpr uint8_t SYSTEM_INFO = 4;
    // Menu-only pages (not part of the page cycle).
    constexpr uint8_t PD_INFO     = 5;
    constexpr uint8_t EMARK       = 6;
}

// ============================================================
// 子模块
// ============================================================
#include "globals.h"