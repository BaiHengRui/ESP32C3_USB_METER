#include "hal.h"
#include <PD_Sniffer.h>

static PD_Sniffer pdSniffer;
static bool pdInitialized  = false;
static bool pdLastConn     = false;
static uint32_t pdDbgTimer = 0;
static uint32_t pdCallCnt   = 0;       // 心跳计数

// ============================================================================
// PD_Init — I2C 快速探测 + 假 INT 脚初始化
// ============================================================================

void HAL::PD_Init() {
    // 1ms 超时快速探测 FUSB302, 不在立即跳过
    uint32_t prevTimeout = Wire.getTimeout();
    Wire.setTimeout(1);
    Wire.beginTransmission(0x22);
    uint8_t err = Wire.endTransmission(true);
    Wire.setTimeout(prevTimeout);

    if (err != 0) {
        HAL::LOG_INFO("PD: FUSB302 not found, skipped.");
        return;
    }

    // 使用 GPIO8 作为假 INT 脚: begin() 会设 INPUT_PULLUP,
    // 之后立即改为 OUTPUT LOW, 让 digitalRead 恒返回 0 → update() 持续轮询
    if (pdSniffer.begin(8)) {
        pinMode(8, OUTPUT);
        digitalWrite(8, LOW);
        pdInitialized = true;
        HAL::LOG_INFO("PD: Sniffer ready (polling mode).");
    } else {
        HAL::LOG_INFO("PD: Init failed.");
    }
}

// ============================================================================
// PD_Update
// ============================================================================

void HAL::PD_Update() {
    static bool firstCall = true;
    if (firstCall) {
        firstCall = false;
        Serial.print(F("[PD] PD_Update() entered, pdInitialized="));
        Serial.println(pdInitialized);
    }
    if (!pdInitialized) return;

    pdCallCnt++;

    // I2C 互斥: 与 INA 任务共享 Wire 总线
    if (xWireMutex) xSemaphoreTake(xWireMutex, portMAX_DELAY);
    pdSniffer.update();
    if (xWireMutex) xSemaphoreGive(xWireMutex);

    bool connected = pdSniffer.isConnected();

    if (pdCallCnt == 1 || (pdCallCnt % 100 == 0)) {
        Serial.print(F("[PD] #")); Serial.print(pdCallCnt);
        Serial.print(F(" update() done, connected="));
        Serial.println(connected);
    }

    if (connected) {
        PD.pd_connected    = true;
        PD.pd_voltage      = pdSniffer.getVoltage();
        PD.pd_current      = pdSniffer.getCurrent();
        PD.pd_power        = pdSniffer.getPower();
        PD.pd_is_pps       = pdSniffer.isPPS();
        PD.pd_ready        = (pdSniffer.getPowerStatus() != PD_POWER_NONE);
        PD.pd_cc_pin       = pdSniffer.getCCPin();
        PD.pd_pos          = pdSniffer.getSelectedPosition();
        PD.pd_pdo_count    = pdSniffer.getSourcePDOCount();
        PD.pd_packet_count = pdSniffer.getPacketCount();
    } else {
        PD.pd_connected    = false;
        PD.pd_voltage      = 0.0f;
        PD.pd_current      = 0.0f;
        PD.pd_power        = 0.0f;
        PD.pd_is_pps       = false;
        PD.pd_ready        = false;
        PD.pd_cc_pin       = 0;
        PD.pd_pos          = 0;
        PD.pd_pdo_count    = 0;
        PD.pd_packet_count = 0;
    }

    // --- 调试输出 (用 print 避免 newlib-nano 不支持 %f/%lu) ---
    bool connChanged = (PD.pd_connected != pdLastConn);
    pdLastConn = PD.pd_connected;

    uint32_t now = millis();
    if (connChanged || now - pdDbgTimer > 2000) {
        pdDbgTimer = now;

        if (connChanged) {
            Serial.print(F("[PD] "));
            Serial.println(PD.pd_connected ? F(">>> CONNECTED <<<") : F("--- DISCONNECTED ---"));
        }

        if (PD.pd_connected) {
            Serial.print(F("[PD] V=")); Serial.print(PD.pd_voltage, 3);
            Serial.print(F("V I="));   Serial.print(PD.pd_current, 3);
            Serial.print(F("A P="));   Serial.print(PD.pd_power, 2);
            Serial.print(F("W | "));
            if (PD.pd_is_pps) Serial.print(F("PPS "));
            Serial.print(PD.pd_ready ? F("READY") : F("WAIT"));
            Serial.print(F(" | pos=")); Serial.print(PD.pd_pos);
            Serial.print(F(" pdo="));   Serial.print(PD.pd_pdo_count);
            Serial.print(F(" pkts="));  Serial.print(PD.pd_packet_count);
            Serial.print(F(" cc="));    Serial.println(PD.pd_cc_pin);
        } else {
            Serial.print(F("[PD] Waiting for PD source... calls="));
            Serial.println(pdCallCnt);
        }
    }
}
