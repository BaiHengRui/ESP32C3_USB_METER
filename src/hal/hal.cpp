#include "hal.h"
#include "../ui/ui.h"

void HAL::Sys_Init(){
    // esp_task_wdt_init(10, false); //watch dog 5s time out
    Serial.begin(912600); // Serial Init
    HAL::NVS_Init();
    HAL::NVS_Load();
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

// Returns the system run time in the format "HH:MM:SS"
// input parameter: esp_timer_get_time() return value in microseconds
String HAL::Get_System_RunTime(uint32_t us){
    uint64_t totalSec = us / 1000000;
    uint32_t hours   = totalSec / 3600;
    uint32_t minutes = (totalSec % 3600) / 60;
    uint32_t seconds = totalSec % 60;

    // 扩容缓冲区，防止小时≥100溢出；改用snprintf防越界
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hours, minutes, seconds);
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
    // 帧间安全应用待处理的屏幕方向切换 (避免 pushSprite 中途改 MADCTL 导致花屏/反色)
    HAL::ApplyPendingRotation();

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

void HAL::ShowToast(const char* msg) {
    strncpy(toastMessage, msg, sizeof(toastMessage) - 1);
    toastMessage[sizeof(toastMessage) - 1] = '\0';
    toastStartTime = millis();
}

bool HAL::IsToastActive() {
    return (millis() - toastStartTime < TOAST_DURATION_MS) && (toastMessage[0] != '\0');
}

void HAL::ApplyPendingRotation() {
    if (pendingRotation < 0) return;
    uint8_t rot = (uint8_t)pendingRotation;
    pendingRotation = -1;
    // 在帧间安全切换, 不会与 pushSprite 的 SPI 传输冲突
    tft.setRotation(rot);
    currentRotation = rot;
}