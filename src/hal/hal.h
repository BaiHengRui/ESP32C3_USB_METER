#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <esp32-hal-cpu.h>
#include <esp_task_wdt.h>
#include "esp_ota_ops.h"
#include <Preferences.h>
#include <Wire.h>

// 硬件引脚
#define I2C_SDA_PIN  7
#define I2C_SCL_PIN  6
#define LCD_BL_PIN   0
#define BUTTON_SW0   9   // 复用启动引脚
#define BUTTON_SW1   10

// 软件版本号 & 硬件版本号
// v Major.Minor.Patch(-branch)
#define SOFTWARE_VERSION "v2.2.0"
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
        uint8_t  header;
        uint8_t  pack_length;
        uint32_t snid;
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
    constexpr uint8_t MENU        = 2;
    constexpr uint8_t SYSTEM_INFO = 3;
}

// ============================================================
// 子模块
// ============================================================
#include "globals.h"