#include "hal.h"
#include "../ui/ui.h"

uint64_t SNID(0);
int32_t startTime(0);
uint8_t nowApp(0),maxApp(3);

// Graph curve data buffers
float voltageBuffer[GRAPH_WIDTH] = {0};
float currentBuffer[GRAPH_WIDTH] = {0};
int graphIndex = 0;
bool graphDataInitialized = false;
bool graphRangeInitialized = false;
float vDisplayMin = 0.0f, vDisplayMax = 5.0f, vHistoryMax = 0.0f;
float iDisplayMin = 0.0f, iDisplayMax = 2.0f, iHistoryMax = 0.0f;
float frozenVoltage = 0.0f, frozenCurrent = 0.0f;
bool graphPaused = false;

Preferences Prefs;  // NVS Preferences object

void HAL::Sys_Init(){
    // esp_task_wdt_init(10, false); //watch dog 5s time out
    Serial.begin(912600); // Serial Init
    Prefs.begin("config", false); // NVS namespace "config", read-write mode
    Serial.println("NVS Init!");
    // Wire.begin(I2C_SDA_PIN,I2C_SCL_PIN,400000); // I2C Init
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); // I2C Init
    pinMode(LCD_BL_PIN, OUTPUT); // LCD Backlight Pin
    SNID = ESP.getEfuseMac();
    HAL::LOG_INFO("System Initialized.");
    HAL::LOG_INFO("SN: " + String(SNID, HEX) + "/ SW: " + SOFTWARE_VERSION + "/ HW: " + HARDWARE_VERSION);
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
    const float OVP = 49.0f; //48V for PD3.1 max voltage(48V-5A)
    const float OCP = 8.0f; //8A for 40.96mV shunt range
    const float LVP = 4.2f; //4.2V for DC-DC buck converter under-voltage lockout
    
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
    case AppState::MAIN:
        UI::ShowMain();
        break;
    case AppState::WAVEGRAPH:
        UI::WaveGraph();
        break;
    case AppState::MENU:
        UI::Menu();
        break;
    case AppState::SYSTEM_INFO:
        UI::System_Info();
        break;
    default:
    UI::ShowMain();
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

void HAL::Update_Graph_Data() {
    static bool wasPaused = false;  // Previous pause state

    float newVoltage = INA.voltage;
    float newCurrent = INA.current;

    // --- Detect rising edge of pause: freeze current values ---
    if (!wasPaused && graphPaused) {
        frozenVoltage = newVoltage;
        frozenCurrent = newCurrent;
    }
    wasPaused = graphPaused;

    // --- Sampling (only when NOT paused) ---
    if (!graphPaused) {
        voltageBuffer[graphIndex] = newVoltage;
        currentBuffer[graphIndex] = newCurrent;
        graphIndex = (graphIndex + 1) % GRAPH_WIDTH;
        graphDataInitialized = true;

        //  更新最大值
        if (newVoltage > vHistoryMax) vHistoryMax = newVoltage;
        if (newCurrent > iHistoryMax) iHistoryMax = newCurrent;

        // Sticky auto-scale: only expand, never shrink
        const float marginFactor = 0.05f;

        if (!graphRangeInitialized) {
            vDisplayMin = 0.0f;
            vDisplayMax = fmaxf(0.1f, newVoltage * (1.0f + marginFactor));
            iDisplayMin = 0.0f;
            iDisplayMax = fmaxf(0.1f, newCurrent * (1.0f + marginFactor));
            graphRangeInitialized = true;
        } else {
            if (newVoltage > vDisplayMax) {
                vDisplayMax = newVoltage * (1.0f + marginFactor);
            }
            if (newCurrent > iDisplayMax) {
                iDisplayMax = newCurrent * (1.0f + marginFactor);
            }
            vDisplayMin = 0.0f;
            iDisplayMin = 0.0f;
        }

        if (vDisplayMax <= vDisplayMin) vDisplayMax = vDisplayMin + 0.1f;
        if (iDisplayMax <= iDisplayMin) iDisplayMax = iDisplayMin + 0.1f;
    }
}