#include "hal.h"

uint64_t SNID(0);
int32_t startTime(0);
uint8_t nowApp(0),maxApp(3);

Preferences Prefs;  // NVS Preferences object

void HAL::Sys_Init(){
    // The System initialization code goes here
    // setCpuFrequencyMhz(240);
    // esp_task_wdt_init(10, false); //watch dog 5s time out
    Serial.begin(115200); // Serial Init
    Prefs.begin("config", false); // NVS namespace "config", read-write mode
    Serial.println("NVS Init!");
    // Wire.begin(I2C_SDA_PIN,I2C_SCL_PIN,400000); // I2C Init
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); // I2C Init
    pinMode(LCD_BL_PIN, OUTPUT); // LCD Backlight Pin
    SNID = ESP.getEfuseMac();
    HAL::LOG_INFO("System Initialized.");
    HAL::LOG_INFO("SNID: " + String(SNID, HEX) + "/ SW: " + SOFTWARE_VERSION + "/ HW: " + HARDWARE_VERSION);
}

void HAL::LOG_INFO(const String msg){
    Serial.print("[" +String(millis()) + " ms] ");
    Serial.println(msg);
}

String HAL::Get_System_RunTime(uint32_t ms){
    uint32_t seconds = ms / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;

    seconds %= 60;
    minutes %= 60;
    char buffer[9];
    sprintf(buffer, "%02lu:%02lu:%02lu", hours, minutes, seconds);
    return String(buffer);
    // Returns a string in the format "HH:MM:SS"
}

String HAL::Get_System_Status(){
    const float OVP = 30.0f; //30V
    const float OCP = 8.0f; //8A
    const float LVP = 4.2f; //4.2V
    
    if (INA.voltage >= OVP && INA.current >= OCP)
    {
        HAL::LCD_SetTextColor(0xF800); // RED
        return "OV/C!";
    }
    if (INA.voltage >= OVP && INA.current < OCP)
    {
        HAL::LCD_SetTextColor(0xF800);
        return "OV !";
    }
    if (INA.voltage < OVP && INA.current >= OCP)
    {
        HAL::LCD_SetTextColor(0xF800);
        return "OC !";
    }
    if (INA.voltage <= LVP && INA.current < OCP)
    {
        HAL::LCD_SetTextColor(0xFFE0); //YELLOW
        return "LV !";
    }
    HAL::LCD_SetTextColor(0x0400); //GREEN
    return "RDY";
}

float HAL::Get_CPU_Temperature(){
    // Returns the CPU temperature in Celsius
    float Temperature = temperatureRead();
    return Temperature;
}

void HAL::APP_Run(){
    switch (nowApp)
    {
    case AppState::UI_MAIN:
        HAL::UI_ShowMain();
        break;
    case AppState::UI_WAVEGRAPH:
        HAL::UI_WaveGraph();
        break;
    case AppState::UI_MENU:
        HAL::UI_Menu();
        break;
    case AppState::UI_SYSTEM_INFO:
        HAL::UI_System_Info();
        break;
    default:
    HAL::UI_ShowMain();
        break;
    }

    //防止最大app溢出
    if (nowApp > maxApp)
    {
        nowApp = 0;
    }
}

uint8_t HAL::Sys_NVS_Valid(const char* key, uint8_t default_val, uint8_t max_val, uint8_t min_val) {
    uint8_t value = Prefs.getUChar(key, default_val);
    if (value > max_val || value < min_val) {
        value = default_val;
        Prefs.putUChar(key, value);  // 写入修正后的默认值
    }
    return value;
}

uint8_t HAL::Sys_NVS_Read(const char* key, uint8_t default_val) {
    return Prefs.getUChar(key, default_val);
}

void HAL::Sys_NVS_Write(const char* key, uint8_t value) {
    Prefs.putUChar(key, value);
}