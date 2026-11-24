// USB PD protocol reading (FUSB302) and PDO cache for the UI.
//
// Default role: passive monitor (no Rd, no GoodCRC, no transmission), suitable
// for tapping an active USB-C link. Select the tapped CC with
// -DFUSB302_PD_MONITOR_CC=1|2.
// Build with -DFUSB302_PD_MONITOR=0 to act as a Sink instead (charger meter).
#include "hal.h"

#include <FUSB302.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#ifndef FUSB302_PD_MONITOR
#define FUSB302_PD_MONITOR 1
#endif

#ifndef FUSB302_PD_MONITOR_CC
#define FUSB302_PD_MONITOR_CC 1
#endif

// Optional boot-time CC probe. It only helps when a Source is already attached
// during boot; otherwise both pins read 0 and it falls back to
// FUSB302_PD_MONITOR_CC. The PD page can switch CC1/CC2 at runtime with a long
// SW1 press, so this is off by default to keep startup short.
// Build with -DFUSB302_PD_AUTO_CC=1 to enable it.
#ifndef FUSB302_PD_AUTO_CC
#define FUSB302_PD_AUTO_CC 0
#endif

// Runtime CC scan: while monitoring with no parsed capabilities, check the CC
// pins about ten times per second and switch to the one showing a Source Rp.
// A PD handshake starts ~150-300 ms after attach, so a one-second scan left a
// blind window long enough to lose the whole Source_Capabilities exchange.
#ifndef FUSB302_PD_AUTO_CC_RUNTIME
#define FUSB302_PD_AUTO_CC_RUNTIME 1
#endif

// Interval of the runtime CC scan while no capabilities have been parsed.
#ifndef FUSB302_PD_AUTO_CC_INTERVAL_MS
#define FUSB302_PD_AUTO_CC_INTERVAL_MS 100
#endif

#ifndef FUSB302_PD_ADDRESS
#define FUSB302_PD_ADDRESS 0x22
#endif

// Keep a safe vSafe5V contract after Source_Capabilities. This is required by
// the PD SenderResponseTimer when the port presents Rd as a Sink. Set to 0 only
// if an external policy engine owns the Request (the Source may hard reset).
#ifndef FUSB302_PD_AUTO_REQUEST
#define FUSB302_PD_AUTO_REQUEST 1
#endif

// Periodic STATUS0/STATUS1/INTERRUPT logging to Serial. Useful while wiring a
// passive tap: ACTIVITY (STATUS0 bit 6) proves the FUSB302 sees BMC on the
// selected CC, while RX/ERR counters show whether framing/CRC works.
// Keep PD diagnostics off by default: the periodic serial lines interfere with
// the host protocol. Build with -DFUSB302_PD_DEBUG=1 when debugging PD.
#ifndef FUSB302_PD_DEBUG
#define FUSB302_PD_DEBUG 0
#endif

// Extra register dump at boot (14 I2C reads). Keep off for normal startup.
#ifndef FUSB302_PD_CFG_LOG
#define FUSB302_PD_CFG_LOG 0
#endif

// Active re-negotiation fallback. The meter is powered from the link it
// measures, so a link that was already negotiated during boot can never be
// sniffed: the FUSB302 is only configured ~200 ms after power-up. When the
// "Soft Reset" menu option is on, the monitor briefly takes over the CC line
// and asks the Source to re-advertise by sending Soft_Reset.
#ifndef FUSB302_PD_SOFT_RESET_ON_MISS
#define FUSB302_PD_SOFT_RESET_ON_MISS 1
#endif
// Delay after PD init before the first attempt, and spacing between attempts.
#ifndef FUSB302_PD_SOFT_RESET_DELAY_MS
#define FUSB302_PD_SOFT_RESET_DELAY_MS 1200
#endif
#ifndef FUSB302_PD_SOFT_RESET_RETRY_MS
#define FUSB302_PD_SOFT_RESET_RETRY_MS 1500
#endif
#ifndef FUSB302_PD_SOFT_RESET_ATTEMPTS
#define FUSB302_PD_SOFT_RESET_ATTEMPTS 3
#endif

// Never let periodic diagnostics block the PD task when USB CDC has no host.
#if FUSB302_PD_DEBUG
#define PD_DEBUG_LOG(...) \
    do { if (Serial.availableForWrite() >= 160) HAL::LOG_INFO(__VA_ARGS__); } while (0)
#else
#define PD_DEBUG_LOG(...) do { } while (0)
#endif

namespace {

FUSB302 s_pd(Wire, FUSB302_PD_ADDRESS);
SemaphoreHandle_t s_pdMutex = nullptr;
SemaphoreHandle_t s_pdOpMutex = nullptr;
HAL::PD_Info s_pdInfo;
HAL::EMark_Info s_emarkInfo;
volatile bool s_emarkRequested = false;
bool s_pdReady = false;
bool s_pdRequestSent = false;
uint32_t s_pdLastConnectMs = 0;
uint8_t s_pdMissCount = 0;
uint32_t s_pdLastRxCount = 0;
uint32_t s_pdLastStatsLog = 0;
uint32_t s_pdLastStatsMs = 0;
uint32_t s_pdLastAutoCCMs = 0;
volatile uint32_t s_pdManualCCMs = 0;
bool s_pdAutoCCFirst = true;
uint32_t s_pdInitMs = 0;
uint8_t s_pdResyncTries = 0;
uint32_t s_pdResyncLastMs = 0;
uint8_t s_pdLoggedFrames = 0;
uint8_t s_pdLoggedCaps = 0;

void pdBusLock() {
    if (xWireMutex) xSemaphoreTake(xWireMutex, portMAX_DELAY);
}

void pdBusUnlock() {
    if (xWireMutex) xSemaphoreGive(xWireMutex);
}

void pdInfoLock() {
    if (s_pdMutex) xSemaphoreTake(s_pdMutex, portMAX_DELAY);
}

void pdInfoUnlock() {
    if (s_pdMutex) xSemaphoreGive(s_pdMutex);
}

HAL::PD_SupplyType convertSupplyType(fusb302::SupplyType type) {
    switch (type) {
        case fusb302::SupplyType::Fixed:    return HAL::PD_FIXED;
        case fusb302::SupplyType::Battery:  return HAL::PD_BATTERY;
        case fusb302::SupplyType::Variable: return HAL::PD_VARIABLE;
        case fusb302::SupplyType::PPS:      return HAL::PD_PPS;
        case fusb302::SupplyType::AVS:      return HAL::PD_AVS;
        default:                            return HAL::PD_UNKNOWN;
    }
}

void storeCapabilities(const fusb302::PDMessage& message) {
    fusb302::PDAdvertisements ads;
    if (!fusb302::parseSourceCapabilities(message, ads)) {
        return;
    }

#if FUSB302_PD_DEBUG
    if (s_pdLoggedCaps < 8) {
        ++s_pdLoggedCaps;
        for (uint8_t i = 0; i < ads.count; ++i) {
            PD_DEBUG_LOG("PD PDO%u type=%u raw=0x%08lX %u-%umV %umA",
                         (unsigned)(i + 1), (unsigned)ads.pdo[i].type,
                         (unsigned long)ads.pdo[i].raw,
                         (unsigned)ads.pdo[i].minVoltage_mV,
                         (unsigned)ads.pdo[i].maxVoltage_mV,
                         (unsigned)ads.pdo[i].maxCurrent_mA);
        }
    }
#endif

    uint8_t requestPosition = 0;
    uint16_t requestCurrent = 0;

    pdInfoLock();
    s_pdInfo.connected = true;
    s_pdInfo.caps_valid = true;
    s_pdInfo.revision = message.revision();
    s_pdInfo.pdo_count = (ads.count > 7) ? 7 : ads.count;
    s_pdInfo.request_valid = false;
    s_pdInfo.request_position = 0;
    s_pdInfo.request_mV = 0;
    s_pdInfo.request_mA = 0;
    s_pdInfo.updated_ms = millis();

    for (uint8_t i = 0; i < s_pdInfo.pdo_count; ++i) {
        HAL::PD_SupplyInfo& dst = s_pdInfo.pdo[i];
        const fusb302::PDSupply& src = ads.pdo[i];
        dst.type = (uint8_t)convertSupplyType(src.type);
        dst.position = src.position;
        dst.min_mV = src.minVoltage_mV;
        dst.max_mV = src.maxVoltage_mV;
        dst.max_mA = src.maxCurrent_mA;
        dst.max_mW = src.maxPower_mW;
    }
    if (s_pdInfo.selected >= s_pdInfo.pdo_count) {
        s_pdInfo.selected = 0;
    }

    // A Sink must answer Source_Capabilities with a Request, otherwise the
    // Source's SenderResponseTimer expires. Keep a safe vSafe5V contract so
    // the link stays up while the UI only reads the advertised gears.
#if FUSB302_PD_AUTO_REQUEST
    if (!s_pdRequestSent && s_pd.role() == fusb302::Role::Sink) {
        for (uint8_t i = 0; i < s_pdInfo.pdo_count; ++i) {
            const HAL::PD_SupplyInfo& p = s_pdInfo.pdo[i];
            if (p.type == HAL::PD_FIXED && p.max_mV == 5000 && p.max_mA > 0) {
                requestPosition = p.position;
                requestCurrent = (p.max_mA > 3000) ? 3000 : p.max_mA;
                break;
            }
        }
    }
#endif

    s_pdMissCount = 0;
    pdInfoUnlock();

    if (requestPosition != 0) {
        if (s_pd.requestFixed(requestPosition, requestCurrent)) {
            s_pdRequestSent = true;
        }
    }
}

// Parses the Sink's Request RDO so the UI can show the actual PPS/Fixed setpoint
// instead of only the advertised PDO range.
void storeRequest(const fusb302::PDMessage& message) {
    if (!message.isData() ||
        message.type() != (uint8_t)fusb302::DataType::Request ||
        message.objectCount() < 1) {
        return;
    }

    uint32_t rdo = message.object(0);
    uint8_t position = (uint8_t)((rdo >> 28) & 0x07);
    if (position == 0) {
        return;
    }

    pdInfoLock();
    if (position > s_pdInfo.pdo_count) {
        pdInfoUnlock();
        return;
    }
    const HAL::PD_SupplyInfo& pdo = s_pdInfo.pdo[position - 1];
    uint16_t mv = pdo.max_mV;
    uint16_t ma = 0;
    switch (pdo.type) {
        case HAL::PD_FIXED:
            mv = pdo.max_mV;
            ma = (uint16_t)(((rdo >> 10) & 0x3FF) * 10);
            break;
        case HAL::PD_PPS:
            mv = (uint16_t)(((rdo >> 9) & 0x7FF) * 20);
            ma = (uint16_t)((rdo & 0x7F) * 50);
            break;
        case HAL::PD_AVS:
            mv = (uint16_t)(((rdo >> 9) & 0x7FF) * 25);
            ma = (uint16_t)((rdo & 0x7F) * 50);
            break;
        default:
            pdInfoUnlock();
            return;
    }
    s_pdInfo.request_valid = true;
    s_pdInfo.request_position = position;
    s_pdInfo.request_mV = mv;
    s_pdInfo.request_mA = ma;
    if (s_pdInfo.monitor) {
        // No cursor while monitoring: follow the contract the Sink asked for.
        s_pdInfo.selected = (uint8_t)(position - 1);
    }
    s_pdInfo.updated_ms = millis();
    uint8_t pdoType = pdo.type;
    pdInfoUnlock();

#if FUSB302_PD_DEBUG
    PD_DEBUG_LOG("PD REQ pos=%u type=%u %umV %umA",
                 (unsigned)position, (unsigned)pdoType,
                 (unsigned)mv, (unsigned)ma);
#endif
}

void emarkRead() {
    uint8_t cc = s_pd.activeCC();
    if (cc != 1 && cc != 2) {
        cc = FUSB302_PD_MONITOR_CC;
    }

    fusb302::CableIdentity id;
    bool ok = s_pd.readCableIdentity(cc, id, 300);

    // With no charger/device attached there is no Rp/Rd to establish the
    // orientation, so the eMarker may be on the other CC. Try the opposite
    // VCONN/SOP' pair only when the link is unpowered, to avoid disturbing an
    // active PD contract.
    if (!ok) {
        bool connected = false;
        uint8_t status0 = 0;
        pdInfoLock();
        connected = s_pdInfo.connected;
        status0 = s_pdInfo.status0;
        pdInfoUnlock();

        if (!connected && (status0 & 0x03) == 0) {
            uint8_t other = (cc == 1) ? 2 : 1;
            PD_DEBUG_LOG("PD: eMarker retry CC%u", (unsigned)other);
            fusb302::CableIdentity alt;
            if (s_pd.readCableIdentity(other, alt, 300)) {
                id = alt;
                ok = true;
            }
            s_pd.setActiveCC(cc);
        }
    }

    pdInfoLock();
    s_emarkInfo.updated_ms = millis();
    s_emarkInfo.error = (uint8_t)s_pd.lastError();
    if (ok && (id.nak || id.busy)) {
        s_emarkInfo.state = HAL::EMARK_NO_CABLE;
        s_emarkInfo.valid = false;
    } else if (ok && id.valid) {
        s_emarkInfo.state = HAL::EMARK_OK;
        s_emarkInfo.valid = true;
        s_emarkInfo.active_cable = (id.kind == fusb302::CableKind::Active);
        s_emarkInfo.epr_capable = id.eprCapable;
        s_emarkInfo.vbus_through = id.vbusThroughCable;
        s_emarkInfo.sop2_present = id.sop2Present;
        s_emarkInfo.current_assumed = id.currentAssumed;
        s_emarkInfo.layout = (uint8_t)id.layout;
        s_emarkInfo.speed_code = id.speedCode;
        s_emarkInfo.vid = id.vid;
        s_emarkInfo.pid = id.pid;
        s_emarkInfo.bcd_device = id.bcdDevice;
        s_emarkInfo.xid = id.xid;
        s_emarkInfo.max_current_mA = id.maxCurrent_mA;
        s_emarkInfo.max_voltage_mV = id.maxVoltage_mV;
    } else {
        s_emarkInfo.state = (s_pd.lastError() == fusb302::Error::TxTimeout)
                                ? HAL::EMARK_NO_CABLE
                                : HAL::EMARK_ERROR;
        s_emarkInfo.valid = false;
    }
    HAL::EMark_State state = (HAL::EMark_State)s_emarkInfo.state;
    uint16_t vid = s_emarkInfo.vid;
    uint16_t pid = s_emarkInfo.pid;
    uint16_t mv = s_emarkInfo.max_voltage_mV;
    uint16_t ma = s_emarkInfo.max_current_mA;
    pdInfoUnlock();

#if FUSB302_PD_DEBUG
    PD_DEBUG_LOG("PD: eMarker state=%u vid=0x%04X pid=0x%04X %umV %umA",
                 (unsigned)state, (unsigned)vid, (unsigned)pid,
                 (unsigned)mv, (unsigned)ma);
#endif
}

void clearCapabilities() {
    pdInfoLock();
    s_pdInfo.caps_valid = false;
    s_pdInfo.pdo_count = 0;
    s_pdInfo.selected = 0;
    s_pdInfo.request_valid = false;
    s_pdInfo.request_position = 0;
    s_pdInfo.request_mV = 0;
    s_pdInfo.request_mA = 0;
    s_pdInfo.updated_ms = millis();
    s_pdRequestSent = false;
    pdInfoUnlock();
}

#if FUSB302_PD_AUTO_CC_RUNTIME
bool autoSelectCC() {
    if (s_pd.role() != fusb302::Role::Monitor) {
        return false;
    }

    uint8_t currentCC = 0;
    bool capsValid = false;
    pdInfoLock();
    currentCC = s_pdInfo.cc;
    capsValid = s_pdInfo.caps_valid;
    pdInfoUnlock();

    if (capsValid || currentCC == 0) {
        return false;
    }

    // Measuring the other CC briefly moves the receiver off the active pin, so
    // only do it when the active CC shows no Rp at all. While a Source drives
    // the active CC the orientation is already correct and the receiver must
    // stay untouched, otherwise the Source_Capabilities burst can be missed.
    uint8_t activeStatus0 = 0;
    if (!s_pd.readRegister(fusb302::REG_STATUS0, activeStatus0)) {
        return false;
    }
    uint8_t activeLevel = (uint8_t)(activeStatus0 & fusb302::ST0_BC_LVL);
    if (activeLevel != 0 && (activeStatus0 & fusb302::ST0_COMP) == 0) {
        return false;
    }

    uint8_t bestCC = 0;
    uint8_t bestLevel = 0;
    for (uint8_t cc = 1; cc <= 2; ++cc) {
        if (cc == currentCC) {
            continue;
        }
        uint8_t status0 = 0;
        if (!s_pd.readCCStatus(cc, status0)) {
            continue;
        }
        uint8_t level = (uint8_t)(status0 & fusb302::ST0_BC_LVL);
        bool active = level != 0 && (status0 & fusb302::ST0_COMP) == 0;
        if (active && level > bestLevel) {
            bestLevel = level;
            bestCC = cc;
        }
    }

    if (bestCC == 0 || bestCC == currentCC) {
        return false;
    }
    if (!s_pd.setActiveCC(bestCC)) {
        return false;
    }

    pdInfoLock();
    s_pdInfo.cc = bestCC;
    s_pdInfo.updated_ms = millis();
    pdInfoUnlock();
    PD_DEBUG_LOG("PD: auto CC%u", (unsigned)bestCC);
    return true;
}
#endif

} // namespace

void HAL::PD_Init() {
#if FUSB302_PD_DEBUG
    uint32_t pdStart = millis();
#endif

    if (s_pdMutex == nullptr) {
        s_pdMutex = xSemaphoreCreateMutex();
    }
    if (s_pdOpMutex == nullptr) {
        s_pdOpMutex = xSemaphoreCreateMutex();
    }

    memset(&s_pdInfo, 0, sizeof(s_pdInfo));
    memset(&s_emarkInfo, 0, sizeof(s_emarkInfo));
    s_emarkInfo.state = HAL::EMARK_IDLE;
    s_emarkRequested = false;
    s_pdReady = false;
    s_pdRequestSent = false;
    s_pdMissCount = 0;
    s_pdLastAutoCCMs = millis();
    s_pdLastStatsMs = millis();
    s_pdManualCCMs = 0;
    s_pdAutoCCFirst = true;
    s_pdInitMs = millis();
    s_pdResyncTries = 0;
    s_pdResyncLastMs = 0;
#if FUSB302_PD_MONITOR
    s_pdInfo.monitor = true;
    s_pdInfo.cc = FUSB302_PD_MONITOR_CC;
#else
    s_pdInfo.monitor = false;
#endif

    s_pd.setI2CLock(pdBusLock, pdBusUnlock);

    if (!s_pd.begin()) {
#if FUSB302_PD_DEBUG
        HAL::LOG_INFO("PD: FUSB302 not available (%s, %lu ms)",
                      s_pd.lastErrorString(),
                      (unsigned long)(millis() - pdStart));
#endif
        return;
    }
#if FUSB302_PD_MONITOR
    if (!s_pd.beginMonitor(FUSB302_PD_MONITOR_CC)) {
#if FUSB302_PD_DEBUG
        HAL::LOG_INFO("PD: monitor setup failed (%s, %lu ms)",
                      s_pd.lastErrorString(),
                      (unsigned long)(millis() - pdStart));
#endif
        return;
    }
#else
    if (!s_pd.beginSink()) {
#if FUSB302_PD_DEBUG
        HAL::LOG_INFO("PD: sink setup failed (%s, %lu ms)",
                      s_pd.lastErrorString(),
                      (unsigned long)(millis() - pdStart));
#endif
        return;
    }
#endif

#if FUSB302_PD_MONITOR && FUSB302_PD_AUTO_CC
    // Passive CC orientation probe: no Rd is applied, so the existing sink on
    // the link still sets the CC voltage. Pick the pin that shows an Rp.
    uint8_t ccStatus[2] = { 0, 0 };
    uint8_t bestCC = 0;
    uint8_t bestLevel = 0;
    for (uint8_t cc = 1; cc <= 2; ++cc) {
        if (!s_pd.setActiveCC(cc)) {
            continue;
        }
        delay(10);
        fusb302::Status st;
        if (!s_pd.readStatus(st)) {
            continue;
        }
        ccStatus[cc - 1] = st.status0;
        uint8_t level = (uint8_t)(st.status0 & fusb302::ST0_BC_LVL);
        bool active = level != 0 && (st.status0 & fusb302::ST0_COMP) == 0;
        if (active && level > bestLevel) {
            bestLevel = level;
            bestCC = cc;
        }
    }
    uint8_t selectedCC = (bestCC != 0) ? bestCC : (uint8_t)FUSB302_PD_MONITOR_CC;
    s_pd.setActiveCC(selectedCC);
#if FUSB302_PD_DEBUG
    HAL::LOG_INFO("PD: CC1 S0=0x%02X CC2 S0=0x%02X -> CC%u",
                  (unsigned)ccStatus[0], (unsigned)ccStatus[1],
                  (unsigned)s_pd.activeCC());
#endif
#endif

    s_pdReady = true;
    s_pdInfo.available = true;
    s_pdInfo.monitor = (s_pd.role() == fusb302::Role::Monitor);
    s_pdInfo.cc = s_pd.activeCC();
    s_pdInfo.connected = s_pdInfo.monitor ? false : s_pd.isConnected();
    s_pdInfo.revision = s_pd.peerRevision();
    s_pdInfo.updated_ms = millis();
    s_pdLastConnectMs = millis();

#if FUSB302_PD_MONITOR
#if FUSB302_PD_DEBUG
    HAL::LOG_INFO("PD: FUSB302 ready in monitor mode (id=0x%02X, CC=%u, %lu ms)",
                  (unsigned)s_pd.deviceId(), (unsigned)s_pd.activeCC(),
                  (unsigned long)(millis() - pdStart));
#endif
#if FUSB302_PD_CFG_LOG
    fusb302::RegisterSnapshot snap;
    if (s_pd.readRegisters(snap)) {
        HAL::LOG_INFO("PD cfg: SW0=%02X SW1=%02X C0=%02X C1=%02X C2=%02X C3=%02X PWR=%02X MEA=%02X S0=%02X S1=%02X",
                      (unsigned)snap.switches0, (unsigned)snap.switches1,
                      (unsigned)snap.control0, (unsigned)snap.control1,
                      (unsigned)snap.control2, (unsigned)snap.control3,
                      (unsigned)snap.power, (unsigned)snap.measure,
                      (unsigned)snap.status0, (unsigned)snap.status1);
    }
#endif
#else
#if FUSB302_PD_DEBUG
    HAL::LOG_INFO("PD: FUSB302 ready (id=0x%02X, CC=%u, attached=%d, %lu ms)",
                  (unsigned)s_pd.deviceId(), (unsigned)s_pd.activeCC(),
                  s_pdInfo.connected ? 1 : 0,
                  (unsigned long)(millis() - pdStart));
#endif
#endif
}

void HAL::PD_Task() {
    if (!s_pdReady || s_pdOpMutex == nullptr) {
        return;
    }
    xSemaphoreTake(s_pdOpMutex, portMAX_DELAY);

    uint32_t now = millis();

    if (s_emarkRequested) {
        s_emarkRequested = false;
        emarkRead();
    }

#if FUSB302_PD_AUTO_CC_RUNTIME
    bool manualHold = (s_pdManualCCMs != 0) &&
                      ((uint32_t)(now - s_pdManualCCMs) < 5000);
    if (!manualHold &&
        (s_pdAutoCCFirst ||
         (uint32_t)(now - s_pdLastAutoCCMs) >= FUSB302_PD_AUTO_CC_INTERVAL_MS)) {
        s_pdLastAutoCCMs = now;
        s_pdAutoCCFirst = false;
        autoSelectCC();
    }
#endif

    // STATUS/INTERRUPT snapshots only feed the UI and the diagnostics log, so
    // refresh them at a lower rate than the FIFO drain below.
    bool refreshStats = (uint32_t)(now - s_pdLastStatsMs) >= 20;
    if (refreshStats) {
        s_pdLastStatsMs = now;
    }

    // Drain RX FIFO. A full PD handshake fits in a few milliseconds, which is
    // shorter than the FUSB302 RX FIFO can buffer it, so the caller runs this
    // task every couple of milliseconds.
    s_pd.poll(refreshStats);
    bool parsedPdu = false;
    fusb302::PDMessage message;
    while (s_pd.available() && s_pd.readMessage(message)) {
#if FUSB302_PD_DEBUG
        if (message.crcValid && s_pdLoggedFrames < 32) {
            ++s_pdLoggedFrames;
            PD_DEBUG_LOG("PD RX sop=%u role=%u ctl=%u type=0x%02X id=%u len=%u",
                         (unsigned)message.sop, message.portPowerRole() ? 1u : 0u,
                         message.isControl() ? 1u : 0u,
                         (unsigned)message.type(), (unsigned)message.messageId(),
                         (unsigned)message.payloadLength);
        }
#endif
        if (!message.crcValid || message.sop != fusb302::Sop::Partner) {
            continue;
        }

        if (message.isControl() &&
            message.type() == (uint8_t)fusb302::ControlType::SoftReset) {
            // A sink must answer Soft_Reset with Accept (MessageID 0).
            if (s_pd.role() == fusb302::Role::Sink) {
                s_pd.sendControl(fusb302::ControlType::Accept, 0);
            }
            clearCapabilities();
            continue;
        }

        if (message.isData() &&
            message.type() == (uint8_t)fusb302::DataType::SourceCapabilities) {
            storeCapabilities(message);
            parsedPdu = true;
        } else if (message.isData() &&
                   message.type() == (uint8_t)fusb302::DataType::Request) {
            storeRequest(message);
            parsedPdu = true;
        }
    }

    fusb302::Status status;
    bool haveStatus = refreshStats && s_pd.readStatus(status);
    uint32_t rx = s_pd.rxFrameCount();
    uint32_t errors = s_pd.invalidFrameCount();
    uint32_t overflow = s_pd.overflowCount();
    bool newRx = (rx != s_pdLastRxCount);
    s_pdLastRxCount = rx;

    pdInfoLock();
    s_pdInfo.rx_frames = rx;
    s_pdInfo.rx_errors = errors;
    s_pdInfo.rx_overflow = overflow;
    if (newRx) {
        s_pdInfo.rx_last_ms = now;
    }
    if (haveStatus) {
        s_pdInfo.status0 = status.status0;
        s_pdInfo.status1 = status.status1;
    }
    s_pdInfo.irq = s_pd.lastInterrupts().irq;
    uint8_t status0 = s_pdInfo.status0;
    uint8_t status1 = s_pdInfo.status1;
    uint8_t irq = s_pdInfo.irq;
    uint8_t activeCC = s_pdInfo.cc;
    bool capsValid = s_pdInfo.caps_valid;
    pdInfoUnlock();

#if FUSB302_PD_SOFT_RESET_ON_MISS
    if (parsedPdu) {
        // A decoded Source_Capabilities/Request proves the nudge worked and the
        // link is watchable; re-arm for a later replug.
        s_pdResyncTries = 0;
    }
    if (pd_soft_reset && s_pd.role() == fusb302::Role::Monitor && !capsValid &&
        s_pdResyncTries < FUSB302_PD_SOFT_RESET_ATTEMPTS &&
        INA.voltage > 4.0f &&
        (uint32_t)(now - s_pdInitMs) >= FUSB302_PD_SOFT_RESET_DELAY_MS &&
        (s_pdResyncTries == 0 ||
         (uint32_t)(now - s_pdResyncLastMs) >= FUSB302_PD_SOFT_RESET_RETRY_MS)) {
        ++s_pdResyncTries;
        s_pdResyncLastMs = now;
        bool sent = s_pd.nudgeSourceCapabilities();
        PD_DEBUG_LOG("PD: soft reset %s (try %u)", sent ? "sent" : "failed",
                     (unsigned)s_pdResyncTries);
    }
#endif

#if FUSB302_PD_DEBUG
    if (newRx || (uint32_t)(now - s_pdLastStatsLog) >= 5000) {
        s_pdLastStatsLog = now;
        PD_DEBUG_LOG("PD: CC%u S0=0x%02X S1=0x%02X IRQ=0x%02X RX=%lu ERR=%lu",
                     (unsigned)activeCC, (unsigned)status0, (unsigned)status1,
                     (unsigned)irq, (unsigned long)rx, (unsigned long)errors);
    }
#endif

    // Connection / revision refresh. A passive monitor has no CC termination,
    // so a quiet line does not mean the Source was disconnected; keep the last
    // parsed capabilities until a reset or a new advertisement.
    if (s_pd.role() != fusb302::Role::Monitor) {
        if ((uint32_t)(now - s_pdLastConnectMs) >= 500) {
            s_pdLastConnectMs = now;
            bool connected = s_pd.isConnected();
            uint8_t revision = s_pd.peerRevision();

            pdInfoLock();
            if (connected) {
                if (!s_pdInfo.connected) {
                    s_pdInfo.updated_ms = now;
                }
                s_pdMissCount = 0;
                s_pdInfo.connected = true;
                if (revision == 1 || revision == 2) {
                    s_pdInfo.revision = revision;
                }
            } else if (s_pdMissCount < 3) {
                // Do not erase the PDO list on a single transient CC sample.
                ++s_pdMissCount;
                if (s_pdMissCount >= 3) {
                    s_pdInfo.connected = false;
                    s_pdInfo.caps_valid = false;
                    s_pdInfo.pdo_count = 0;
                    s_pdInfo.selected = 0;
                    s_pdInfo.updated_ms = now;
                }
            }
            pdInfoUnlock();

            if (!connected && s_pdMissCount >= 3 && s_pd.activeCC() != 0) {
                // Re-arm orientation detection so a replug on the other CC is seen.
                uint8_t cc = 0;
                s_pd.detectOrientation(cc);
            }
        }
    }

    // Hard/Soft Reset restarts the advertisement; wait for fresh capabilities.
    if (s_pd.hardResetReceived() || s_pd.softResetReceived()) {
        s_pd.clearResetFlags();
        clearCapabilities();
    }

    xSemaphoreGive(s_pdOpMutex);
}

void HAL::PD_GetInfo(PD_Info& info) {
    if (s_pdMutex == nullptr) {
        memset(&info, 0, sizeof(info));
        return;
    }
    pdInfoLock();
    info = s_pdInfo;
    pdInfoUnlock();
}

void HAL::PD_SelectNext() {
    if (s_pdMutex == nullptr) return;
    pdInfoLock();
    // The profile list is fixed while monitoring: only a sink can request a
    // different PDO, so the cursor is disabled there.
    if (!s_pdInfo.monitor && s_pdInfo.pdo_count > 0) {
        s_pdInfo.selected = (uint8_t)((s_pdInfo.selected + 1) % s_pdInfo.pdo_count);
    }
    pdInfoUnlock();
}

void HAL::PD_SelectPrev() {
    if (s_pdMutex == nullptr) return;
    pdInfoLock();
    if (!s_pdInfo.monitor && s_pdInfo.pdo_count > 0) {
        s_pdInfo.selected =
            (uint8_t)((s_pdInfo.selected + s_pdInfo.pdo_count - 1) % s_pdInfo.pdo_count);
    }
    pdInfoUnlock();
}

void HAL::PD_ToggleCC() {
    if (!s_pdReady || s_pdOpMutex == nullptr ||
        s_pd.role() != fusb302::Role::Monitor) {
        return;
    }

    uint8_t next;
    pdInfoLock();
    next = (s_pdInfo.cc == 1) ? 2 : 1;
    pdInfoUnlock();

    s_pdManualCCMs = millis();
    xSemaphoreTake(s_pdOpMutex, portMAX_DELAY);
    bool ok = s_pd.setActiveCC(next);
    if (ok) {
        pdInfoLock();
        s_pdInfo.cc = next;
        s_pdInfo.caps_valid = false;
        s_pdInfo.pdo_count = 0;
        s_pdInfo.selected = 0;
        s_pdInfo.updated_ms = millis();
        pdInfoUnlock();
        s_pdLoggedFrames = 0;
    }
    xSemaphoreGive(s_pdOpMutex);

    HAL::ShowToast(ok ? (next == 1 ? "PD CC1" : "PD CC2") : "PD CC ERR");
}

void HAL::PD_AllowSoftReset(bool allow) {
    if (!allow) {
        return;
    }
    // Re-arm the attempts so enabling the option takes effect immediately
    // instead of after the next reboot.
    s_pdResyncTries = 0;
    s_pdResyncLastMs = 0;
    s_pdInitMs = millis();
}

void HAL::EMark_RequestRead() {
    if (!s_pdReady || s_pdMutex == nullptr) {
        return;
    }
    pdInfoLock();
    if (s_emarkInfo.state == EMARK_READING) {
        pdInfoUnlock();
        return;
    }
    s_emarkInfo.state = EMARK_READING;
    s_emarkInfo.valid = false;
    s_emarkInfo.updated_ms = millis();
    pdInfoUnlock();
    s_emarkRequested = true;
}

void HAL::EMark_GetInfo(EMark_Info& info) {
    if (s_pdMutex == nullptr) {
        memset(&info, 0, sizeof(info));
        info.state = EMARK_IDLE;
        return;
    }
    pdInfoLock();
    info = s_emarkInfo;
    pdInfoUnlock();
}
