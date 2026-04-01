#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <esp32-hal-cpu.h>
#include <esp_task_wdt.h>
#include "esp_ota_ops.h"
#include <Preferences.h>
#include <Wire.h>

#define I2C_SDA_PIN 7
#define I2C_SCL_PIN 6
#define LCD_BL_PIN 0
#define BUTTON_SW0 9 //复用启动引脚
#define BUTTON_SW1 10

//Mojor,Minor,Patch
#define SOFTWARE_VERSION "v1.1.15"
#define HARDWARE_VERSION "v1.0.2"

#define INA228_EN 1

extern uint64_t SNID;
extern int32_t nowTime, lastTime;
extern int32_t startTime;
extern uint8_t nowApp,maxApp;
extern bool graphPaused;    //VA曲线暂停标志
extern uint8_t defaultBrightness; //LCD亮度
extern uint8_t defaultRotation;   //LCD旋转
extern uint8_t sample_mode; //0: fast, 1: normal, 2: slow

// HAL命名空间 INA接口定义
namespace HAL
{
    typedef struct
    {
        float voltage;       // in Volts
        float current;       // in Amperes
        float power;         // in Watts
        float energy_mWh;        // in Watt-hours
        float charge_mAh;        // in Ampere-hours
        float energy_Wh;         // in Watt-hours
        float charge_Ah;         // in Ampere-hours
        float temperature;       // in Die-temperature
        bool current_direction; // true for left, false for right

        uint16_t device_id;  // in device id
        uint16_t status;  // status flags
    } INA22x_Data;

    #pragma pack(push, 1)
    typedef struct
    {
        uint8_t header;        // Packet header (0xAA)
        float voltage;       // in Volts
        float current;       // in Amperes
        float power;         // in Watts
        float energy_mWh;        // in Watt-hours
        float charge_mAh;        // in Ampere-hours
        float energy_Wh;         // in Watt-hours
        float charge_Ah;         // in Ampere-hours
        float temperature;       // in Die-temperature
        uint64_t time_ms;       // in milliseconds since device start
        bool current_direction; // true for left, false for right
        uint8_t checksum;       // Simple checksum XOR of all previous bytes
    } USB_CDC_Data;
    // all 43 bytes are sent as raw binary data over USB CDC, no delimiters, header byte (0xAA)
    #pragma pack(pop)
}

//HAL命名空间 UI和功能接口定义
namespace HAL
{
    /* System */
    void Sys_Init();
    void LOG_INFO(const String msg);
    String Get_System_RunTime(uint32_t ms);
    String Get_System_Status();
    float Get_CPU_Temperature();
    void APP_Run();
    uint8_t Sys_NVS_Valid(const char* key, uint8_t default_val, uint8_t max_val = 255, uint8_t min_val = 0);
    uint8_t Sys_NVS_Read(const char* key, uint8_t default_val);
    void Sys_NVS_Write(const char* key, uint8_t value);
    /* USB */
    void UART_Command();
    /* Button */
    void Button_Init();
    void Button_Click();
    /* INA */
    bool INA22x_Init();
    void INA22x_GetData(INA22x_Data *data);
    void INA22x_SetConfig(uint8_t sample_mode);
    /* LCD */
    void LCD_Init();
    void LCD_SetBrightness(uint8_t brightness);
    void LCD_SetRotation(uint8_t rotation);
    void LCD_SetTextColor(uint16_t color);
    void LCD_Refresh_Screen(uint32_t bgcolor);
    float Get_FPS();
    /* UI */
    void UI_ShowMain();
    void UI_System_Info();
    void UI_WaveGraph();
    void UI_Menu();
} // namespace HAL

// AppState命名空间 定义应用代号
namespace AppState
{
    constexpr uint8_t UI_MAIN = 0;
    constexpr uint8_t UI_WAVEGRAPH = 1;
    constexpr uint8_t UI_MENU = 2;
    constexpr uint8_t UI_SYSTEM_INFO = 3;

} // namespace AppState

extern HAL::INA22x_Data INA;