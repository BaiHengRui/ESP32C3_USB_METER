#include "hal.h"
#include "../ui/ui.h"

void HAL::Sys_Init(){
    // esp_task_wdt_init(10, false); //watch dog 5s time out
    Serial.begin(912600); // Serial Init
    HAL::NVS_Init();
    HAL::NVS_Load();
    Wire.begin(I2C_SDA_PIN,I2C_SCL_PIN,400000); // I2C Init
    // Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); // I2C Init
    xWireMutex = xSemaphoreCreateMutex(); // I2C 总线互斥锁, INA 与其它外设共用
    pinMode(LCD_BL_PIN, OUTPUT); // LCD Backlight Pin
    SNID = ESP.getEfuseMac();
    HAL::LOG_INFO("System Initialized.");
    HAL::LOG_INFO("SN: %012llX / SW: %s / HW: %s", SNID, SOFTWARE_VERSION, HARDWARE_VERSION);
}

void HAL::LOG_INFO(const char* str, ...){
    uint64_t now_ms = esp_timer_get_time() / 1000ULL;  // Convert microseconds to milliseconds
    Serial.print("[" + String((uint64_t)now_ms) + " ms] ");
    char buf[128];
    va_list args;
    va_start(args, str);
    vsnprintf(buf, sizeof(buf), str, args);
    va_end(args);
    Serial.println(buf);
}

// Returns the system run time in the format "HH:MM:SS"
// input parameter: esp_timer_get_time() return value in microseconds
String HAL::Get_System_RunTime(uint64_t us){
    uint64_t totalSec = us / 1000000ULL;
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
    const float OVP = 50.0f; // 48V for PD3.1 max voltage(48V-5A)
    const float OCP = 8.0f;  // 8A for 40.96mV shunt range
    const float LVP = 4.2f;  // 4.2V for DC-DC buck converter under-voltage lockout
    const float OTP = 60.0f; // 60°C Temperature max for safe and reliable operation

    // OV/C > OV > OC > HOT > LV
    // Priority 1: OV/C — simultaneous over-voltage AND over-current (most dangerous)
    if (INA.voltage >= OVP && INA.current >= OCP) {
        HAL::LCD_SetTextColor(0xF800); // RED
        return "OV/C!";
    }

    // Priority 2: OV — over-voltage only
    if (INA.voltage >= OVP) {
        HAL::LCD_SetTextColor(0xF800); // RED
        return "OV !";
    }

    // Priority 3: OC — over-current only
    if (INA.current >= OCP) {
        HAL::LCD_SetTextColor(0xF800); // RED
        return "OC !";
    }

    // Priority 4: HOT — over-temperature (suppressed if electrical fault active)
    if (INA.temperature >= OTP) {
        HAL::LCD_SetTextColor(0xF800); // RED
        return "HOT!";
    }

    // Priority 5: LV — under-voltage (voltage < 4.2V; mutually exclusive with OV/OVC)
    if (INA.voltage <= LVP) {
        HAL::LCD_SetTextColor(0xFFE0); // YELLOW
        return "LV !";
    }

    // All clear — no faults
    HAL::LCD_SetTextColor(0x0400); // GREEN
    return "RDY";
}

float HAL::Get_CPU_Temperature(){
    // 线程安全的 CPU 温度读取，带缓存避免频繁调用 temperatureRead()
    // temperatureRead() 内部 start/stop 温度传感器，非线程安全且开销大
    static SemaphoreHandle_t tempMutex = NULL;
    static float   cachedCpuTemp   = 0.0f;
    static int64_t lastTempReadUs  = 0;

    if (tempMutex == NULL) {
        tempMutex = xSemaphoreCreateMutex();
        if (tempMutex == NULL) return 0.0f;
    }

    if (xSemaphoreTake(tempMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int64_t now = esp_timer_get_time();
        // 每 1000ms读取一次温度传感器
        if (now - lastTempReadUs > 1000000 || lastTempReadUs == 0) {
            cachedCpuTemp  = temperatureRead();
            lastTempReadUs = now;
        }
        float result = cachedCpuTemp;
        xSemaphoreGive(tempMutex);
        return result;
    }
    return cachedCpuTemp;  // 互斥锁获取失败时返回旧值
}

void HAL::APP_Run(){
    static uint8_t prevApp = 0xFF;  // 0xFF 表示首次渲染, 不触发过渡动画

    // 帧间安全应用待处理的屏幕方向切换 (避免 pushSprite 中途改 MADCTL 导致花屏/反色)
    HAL::ApplyPendingRotation();

    // 防止最大app溢出
    if (nowApp > maxApp)
    {
        nowApp = 0;
    }

    // 页面切换时触发过渡动画 (跳过首次渲染)
    if (nowApp != prevApp && prevApp != 0xFF) {
        UI::TransitionTo(prevApp, nowApp);
        prevApp = nowApp;
        return;
    }

    prevApp = nowApp;

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
    toastStartTime = (uint32_t)(esp_timer_get_time() / 1000ULL);
}

bool HAL::IsToastActive() {
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return (now_ms - toastStartTime < TOAST_DURATION_MS) && (toastMessage[0] != '\0');
}

void HAL::ApplyPendingRotation() {
    if (pendingRotation < 0) return;
    uint8_t rot = (uint8_t)pendingRotation;
    pendingRotation = -1;
    // 在帧间安全切换, 不会与 pushSprite 的 SPI 传输冲突
    tft.setRotation(rot);
    currentRotation = rot;
}