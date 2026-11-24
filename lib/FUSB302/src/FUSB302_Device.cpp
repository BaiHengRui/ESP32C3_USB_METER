/**
 * FUSB302 USB Type-C / USB PD controller.
 *
 * Licensed under the MIT License. See LICENSE.
 */
#include "FUSB302_Device.h"

#include <string.h>

// Enabling SOP'/SOP'' in monitor mode allows e-Marker traffic to be captured,
// but some FUSB302 boards then fill the RX FIFO with cable traffic. SOP-only is
// the reliable default for Source_Capabilities monitoring.
#ifndef FUSB302_MONITOR_SOP_PRIME
#define FUSB302_MONITOR_SOP_PRIME 0
#endif

using namespace fusb302;

namespace {

Sop sopFromToken(uint8_t token) {
    switch (token & RX_TOKEN_MASK) {
        case RX_TOKEN_SOP:        return Sop::Partner;
        case RX_TOKEN_SOP_PRIME:  return Sop::Cable;
        case RX_TOKEN_SOP_DPRIME: return Sop::CableDoublePrime;
        case RX_TOKEN_SOP_DEBUG:  return Sop::Debug;
        case RX_TOKEN_SOP_DEBUG2: return Sop::DebugDoublePrime;
        default:                  return Sop::Invalid;
    }
}

bool elapsed(uint32_t start, uint32_t timeoutMs) {
    return (uint32_t)(millis() - start) >= timeoutMs;
}

uint32_t readLe32(const uint8_t* data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

} // namespace

/* ------------------------------------------------------------------------- */
/* Construction                                                              */
/* ------------------------------------------------------------------------- */

FUSB302::FUSB302()
    : _wire(nullptr),
      _address(DEFAULT_I2C_ADDRESS),
      _ready(false),
      _role(Role::None),
      _cc(0),
      _messageId(0),
      _lastOrientationMs(0),
      _peerRevision(2),
      _error(Error::None),
      _rxFrames(0),
      _txFrames(0),
      _invalidFrames(0),
      _droppedFrames(0),
      _overflowEvents(0),
      _hardResetReceived(false),
      _softResetReceived(false),
      _lockFn(nullptr),
      _unlockFn(nullptr),
      _qHead(0),
      _qTail(0),
      _qCount(0),
      _emulationObjectCount(0),
      _emulationMessageId(0),
      _emulationEnabled(false),
      _emulationDue(false),
      _inEmulationReply(false),
      _emulationRequestHeader(0),
      _emulationRequestVdm(0),
      _cableInspection(false) {
    _irq = InterruptFlags();
    _status = Status();
    memset(_emulationObjects, 0, sizeof(_emulationObjects));
    _emulation = CableEmulationConfig();
    _emulationObjectCount = buildCableIdentityObjects(_emulation, _emulationObjects, 5);
}

FUSB302::FUSB302(TwoWire& wire, uint8_t address)
    : FUSB302() {
    _wire = &wire;
    _address = address;
}

/* ------------------------------------------------------------------------- */
/* Setup                                                                     */
/* ------------------------------------------------------------------------- */

bool FUSB302::begin() {
    if (_wire == nullptr) {
        _wire = &Wire;
    }
    return begin(*_wire, _address);
}

bool FUSB302::begin(TwoWire& wire, uint8_t address) {
    _wire = &wire;
    _address = address;
    _error = Error::None;
    _ready = false;

    uint8_t id = 0;
    if (!readRegister(REG_DEVICE_ID, id)) {
        fail(Error::BusError);
        return false;
    }
    if (id == 0x00 || id == 0xFF) {
        fail(Error::DeviceNotFound);
        return false;
    }

    if (!reset()) {
        return false;
    }

    // SW_RESET leaves Rd on both CCs. Release the terminations immediately so a
    // passive tap does not disturb an already-negotiated link while the caller
    // chooses a role.
    if (!writeRegister(REG_SWITCHES0, 0) ||
        !writeRegister(REG_SWITCHES1, 0) ||
        !writeRegister(REG_POWER, PWR_BANDGAP_WAKE)) {
        return false;
    }

    _ready = true;
    _role = Role::None;
    _cc = 0;
    _qHead = _qTail = _qCount = 0;
    _error = Error::None;
    return true;
}

bool FUSB302::begin(int sdaPin, int sclPin, uint32_t frequency) {
    if (_wire == nullptr) {
        _wire = &Wire;
    }
    _wire->begin(sdaPin, sclPin, frequency);
    return begin(*_wire, _address);
}

void FUSB302::end() {
    if (_ready && _wire != nullptr) {
        writeRegister(REG_SWITCHES0, 0);
        writeRegister(REG_SWITCHES1, 0);
        writeRegister(REG_POWER, 0);
    }
    _ready = false;
    _role = Role::None;
    _cc = 0;
    _qHead = _qTail = _qCount = 0;
}

/* ------------------------------------------------------------------------- */
/* Low level I2C                                                             */
/* ------------------------------------------------------------------------- */

void FUSB302::lockBus() {
    if (_lockFn != nullptr) {
        _lockFn();
    }
}

void FUSB302::unlockBus() {
    if (_unlockFn != nullptr) {
        _unlockFn();
    }
}

void FUSB302::setI2CLock(void (*lock)(), void (*unlock)()) {
    _lockFn = lock;
    _unlockFn = unlock;
}

void FUSB302::fail(Error error) {
    _error = error;
}

bool FUSB302::i2cWrite(const uint8_t* data, uint8_t length) {
    if (_wire == nullptr || data == nullptr || length == 0) {
        fail(Error::NotInitialized);
        return false;
    }

    lockBus();
    _wire->beginTransmission(_address);
    _wire->write(data, length);
    uint8_t result = _wire->endTransmission(true);
    unlockBus();

    if (result != 0) {
        fail(Error::BusError);
        return false;
    }
    return true;
}

bool FUSB302::i2cReadAfterWrite(const uint8_t* writeData, uint8_t writeLength,
                                uint8_t* readData, uint8_t readLength) {
    if (_wire == nullptr || writeData == nullptr || readData == nullptr ||
        writeLength == 0 || readLength == 0) {
        fail(Error::NotInitialized);
        return false;
    }

    bool ok = false;
    lockBus();
    _wire->beginTransmission(_address);
    _wire->write(writeData, writeLength);
    uint8_t result = _wire->endTransmission(false);
    if (result == 0) {
        uint8_t received = (uint8_t)_wire->requestFrom((uint16_t)_address, readLength, (bool)true);
        if (received == readLength) {
            for (uint8_t i = 0; i < readLength; ++i) {
                readData[i] = (uint8_t)_wire->read();
            }
            ok = true;
        }
    }
    unlockBus();

    if (!ok) {
        fail(Error::BusError);
    }
    return ok;
}

bool FUSB302::readRegister(uint8_t reg, uint8_t& value) {
    value = 0;
    return i2cReadAfterWrite(&reg, 1, &value, 1);
}

bool FUSB302::writeRegister(uint8_t reg, uint8_t value) {
    uint8_t data[2] = { reg, value };
    return i2cWrite(data, 2);
}

bool FUSB302::readBlock(uint8_t reg, uint8_t* data, uint8_t length) {
    return i2cReadAfterWrite(&reg, 1, data, length);
}

bool FUSB302::writeBlock(uint8_t reg, const uint8_t* data, uint8_t length) {
    if (_wire == nullptr || data == nullptr || length == 0) {
        fail(Error::NotInitialized);
        return false;
    }

    lockBus();
    _wire->beginTransmission(_address);
    _wire->write(reg);
    _wire->write(data, length);
    uint8_t result = _wire->endTransmission(true);
    unlockBus();

    if (result != 0) {
        fail(Error::BusError);
        return false;
    }
    return true;
}

bool FUSB302::updateRegister(uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t current = 0;
    if (!readRegister(reg, current)) {
        return false;
    }
    current = (uint8_t)((current & (uint8_t)~mask) | (value & mask));
    return writeRegister(reg, current);
}

uint8_t FUSB302::deviceId() {
    uint8_t id = 0;
    if (!readRegister(REG_DEVICE_ID, id)) {
        return 0;
    }
    return id;
}

bool FUSB302::reset() {
    if (_wire == nullptr) {
        fail(Error::NotInitialized);
        return false;
    }

    if (!writeRegister(REG_RESET, RESET_SW_RESET)) {
        return false;
    }
    delay(5);
    if (!writeRegister(REG_RESET, RESET_PD_RESET)) {
        return false;
    }
    delay(2);

    _messageId = 0;
    _cc = 0;
    _peerRevision = 2;
    _lastOrientationMs = millis();
    _qHead = _qTail = _qCount = 0;
    _hardResetReceived = false;
    _softResetReceived = false;
    _status = Status();
    _irq = InterruptFlags();
    return true;
}

bool FUSB302::readStatus(Status& status) {
    if (!readRegister(REG_STATUS0, status.status0) ||
        !readRegister(REG_STATUS1, status.status1) ||
        !readRegister(REG_STATUS1A, status.status1a)) {
        return false;
    }
    _status = status;
    return true;
}

bool FUSB302::readCCStatus(uint8_t cc, uint8_t& status0) {
    status0 = 0;
    if (!_ready) {
        fail(Error::NotInitialized);
        return false;
    }
    if (cc != 1 && cc != 2) {
        fail(Error::InvalidArgument);
        return false;
    }

    uint8_t saved = 0;
    if (!readRegister(REG_SWITCHES0, saved)) {
        return false;
    }
    uint8_t measurement = (cc == 1) ? SW0_MEAS_CC1 : SW0_MEAS_CC2;
    if (!writeRegister(REG_SWITCHES0, measurement)) {
        writeRegister(REG_SWITCHES0, saved);
        return false;
    }
    delay(2);
    bool ok = readRegister(REG_STATUS0, status0);
    writeRegister(REG_SWITCHES0, saved);
    return ok;
}

bool FUSB302::readRegisters(RegisterSnapshot& snapshot) {
    RegisterSnapshot s;
    if (!readRegister(REG_DEVICE_ID, s.deviceId) ||
        !readRegister(REG_SWITCHES0, s.switches0) ||
        !readRegister(REG_SWITCHES1, s.switches1) ||
        !readRegister(REG_MEASURE, s.measure) ||
        !readRegister(REG_CONTROL0, s.control0) ||
        !readRegister(REG_CONTROL1, s.control1) ||
        !readRegister(REG_CONTROL2, s.control2) ||
        !readRegister(REG_CONTROL3, s.control3) ||
        !readRegister(REG_POWER, s.power) ||
        !readRegister(REG_MASK, s.mask) ||
        !readRegister(REG_MASKA, s.maska) ||
        !readRegister(REG_MASKB, s.maskb) ||
        !readRegister(REG_STATUS0, s.status0) ||
        !readRegister(REG_STATUS1, s.status1)) {
        return false;
    }
    snapshot = s;
    return true;
}

bool FUSB302::clearInterrupts() {
    uint8_t a = 0;
    uint8_t b = 0;
    uint8_t irq = 0;
    if (!readRegister(REG_INTERRUPTA, a) ||
        !readRegister(REG_INTERRUPTB, b) ||
        !readRegister(REG_INTERRUPT, irq)) {
        return false;
    }
    _irq = InterruptFlags();
    return true;
}

bool FUSB302::flushRx() {
    uint8_t control1 = 0;
    if (!readRegister(REG_CONTROL1, control1)) {
        return false;
    }
    // RX_FLUSH self-clears; keep ENSOP1/ENSOP2/ENSOP1DB/ENSOP2DB as-is.
    return writeRegister(REG_CONTROL1, (uint8_t)(control1 | C1_RX_FLUSH));
}

/* ------------------------------------------------------------------------- */
/* Roles / CC                                                                */
/* ------------------------------------------------------------------------- */

bool FUSB302::configureSinkCC(uint8_t cc) {
    if (cc != 1 && cc != 2) {
        fail(Error::InvalidArgument);
        return false;
    }

    uint8_t switches0 = (uint8_t)(SW0_PDWN_CC1 | SW0_PDWN_CC2 |
                                  ((cc == 1) ? SW0_MEAS_CC1 : SW0_MEAS_CC2));
    uint8_t switches1 = (uint8_t)(SW1_SPECREV_PD2 | SW1_AUTO_CRC |
                                  ((cc == 1) ? SW1_TXCC1_EN : SW1_TXCC2_EN));
    uint8_t control1 = _emulationEnabled ? (uint8_t)(C1_ENSOP1 | C1_ENSOP2) : 0;

    if (!writeRegister(REG_SWITCHES1, switches1) ||
        !writeRegister(REG_SWITCHES0, switches0) ||
        !writeRegister(REG_MEASURE, MEASURE_DEFAULT) ||
        !writeRegister(REG_CONTROL1, control1)) {
        return false;
    }
    flushRx();
    if (!writeRegister(REG_POWER, PWR_ALL)) {
        return false;
    }
    delay(1);
    clearInterrupts();

    _cc = cc;
    return true;
}

bool FUSB302::configureMonitorCC(uint8_t cc) {
    if (cc != 1 && cc != 2) {
        fail(Error::InvalidArgument);
        return false;
    }
    uint8_t switches0 = (uint8_t)((cc == 1) ? SW0_MEAS_CC1 : SW0_MEAS_CC2);
    uint8_t control1 = 0;
#if FUSB302_MONITOR_SOP_PRIME
    control1 = (uint8_t)(C1_ENSOP1 | C1_ENSOP2);
#endif

    // Receive-only baseline: Type-C state machine in DRP with toggle off,
    // PD 2.0 spec revision, no AUTO_CRC and both TX drivers off. This matches
    // the KWS-X1 passive sniffer, which is known to receive on real hardware.
    if (!writeRegister(REG_CONTROL0, (uint8_t)(C0_INT_MASK | C0_HOST_CUR_DEF)) ||
        !writeRegister(REG_CONTROL2, C2_MODE_DRP) ||
        !writeRegister(REG_CONTROL3, (uint8_t)C3_N_RETRIES_3) ||
        !writeRegister(REG_MEASURE, MEASURE_DEFAULT) ||
        !writeRegister(REG_MASK, 0) ||
        !writeRegister(REG_MASKA, 0) ||
        !writeRegister(REG_MASKB, 0) ||
        !writeRegister(REG_SWITCHES1, SW1_SPECREV_PD2) ||
        !writeRegister(REG_SWITCHES0, switches0) ||
        !writeRegister(REG_CONTROL1, control1) ||
        !writeRegister(REG_POWER, (uint8_t)(PWR_OSCILLATOR | PWR_BANDGAP_WAKE))) {
        return false;
    }

    // Flush with the receiver/references off, then turn them on. Flushing while
    // the receiver is live can drop the first bytes of an incoming frame.
    flushRx();
    delay(1);
    clearInterrupts();
    if (!writeRegister(REG_POWER,
                       (uint8_t)(PWR_BANDGAP_WAKE | PWR_RECEIVER | PWR_MEASURE))) {
        return false;
    }
    _cc = cc;
    return true;
}

bool FUSB302::beginSink() {
    if (!_ready) {
        fail(Error::NotInitialized);
        return false;
    }
    _error = Error::None;

    if (!reset()) {
        return false;
    }
    _role = Role::Sink;

    if (!writeRegister(REG_CONTROL0, (uint8_t)(C0_INT_MASK | C0_HOST_CUR_DEF)) ||
        !writeRegister(REG_CONTROL2, C2_MODE_UFP) ||
        !writeRegister(REG_CONTROL3, (uint8_t)(C3_N_RETRIES_3 | C3_AUTO_RETRY)) ||
        !writeRegister(REG_MEASURE, MEASURE_DEFAULT) ||
        !writeRegister(REG_MASK, 0) ||
        !writeRegister(REG_MASKA, 0) ||
        !writeRegister(REG_MASKB, 0) ||
        !writeRegister(REG_SWITCHES0, (uint8_t)(SW0_PDWN_CC1 | SW0_PDWN_CC2)) ||
        !writeRegister(REG_POWER, PWR_ALL)) {
        return false;
    }
    delay(10);

    uint8_t cc = 0;
    bool attached = detectOrientation(cc);
    if (!attached && _error != Error::NotConnected) {
        return false;
    }
    if (!attached) {
        // No source yet. Keep the transmitter routed, but remember that the
        // orientation is still unknown so isConnected() can retry.
        if (!configureSinkCC(1)) {
            return false;
        }
        _cc = 0;
    }

    clearInterrupts();
    _error = Error::None;
    return true;
}

bool FUSB302::beginMonitor(uint8_t cc) {
    if (!_ready) {
        fail(Error::NotInitialized);
        return false;
    }
    if (cc != 1 && cc != 2) {
        fail(Error::InvalidArgument);
        return false;
    }
    _error = Error::None;

    // begin() already reset the chip; avoid a second reset (which briefly
    // applies Rd on both CCs) when entering monitor mode from a fresh begin().
    if (_role != Role::None && !reset()) {
        return false;
    }
    _role = Role::Monitor;
    _emulationEnabled = false;
    _emulationDue = false;

    if (!configureMonitorCC(cc)) {
        return false;
    }
    _error = Error::None;
    return true;
}

bool FUSB302::detectOrientation(uint8_t& cc, uint16_t settleMs) {
    cc = 0;
    if (!_ready) {
        fail(Error::NotInitialized);
        return false;
    }
    _lastOrientationMs = millis();

    bool monitor = (_role == Role::Monitor);
    if (_role == Role::Sink) {
        // Do not acknowledge (or transmit) during the measurement; otherwise a
        // Source_Capabilities received while switching CC could be consumed by
        // the RX flush after orientation detection.
        writeRegister(REG_SWITCHES1, 0);
    }
    if (_role == Role::None) {
        writeRegister(REG_CONTROL0, (uint8_t)(C0_INT_MASK | C0_HOST_CUR_DEF));
        writeRegister(REG_MEASURE, MEASURE_DEFAULT);
        writeRegister(REG_POWER, PWR_ALL);
        delay(2);
    }
    uint8_t status[2] = { 0, 0 };

    for (uint8_t pin = 0; pin < 2; ++pin) {
        uint8_t measurement = (pin == 0) ? SW0_MEAS_CC1 : SW0_MEAS_CC2;
        uint8_t switches0 = monitor ? measurement
                                    : (uint8_t)(SW0_PDWN_CC1 | SW0_PDWN_CC2 | measurement);
        if (!writeRegister(REG_SWITCHES0, switches0)) {
            return false;
        }
        delay(settleMs);
        if (!readRegister(REG_STATUS0, status[pin])) {
            return false;
        }
    }

    bool active1 = (status[0] & ST0_BC_LVL) != 0 && (status[0] & ST0_COMP) == 0;
    bool active2 = (status[1] & ST0_BC_LVL) != 0 && (status[1] & ST0_COMP) == 0;

    if (active1 && !active2) {
        cc = 1;
    } else if (!active1 && active2) {
        cc = 2;
    } else if (active1 && active2) {
        // Unusual, but pick the stronger Rp level.
        cc = ((status[0] & ST0_BC_LVL) >= (status[1] & ST0_BC_LVL)) ? 1 : 2;
    }

    if (_role == Role::Sink) {
        if (cc != 0) {
            configureSinkCC(cc);
        } else {
            // Restore Rd on both CCs after measuring.
            writeRegister(REG_SWITCHES0, (uint8_t)(SW0_PDWN_CC1 | SW0_PDWN_CC2));
        }
    } else if (_role == Role::Monitor) {
        configureMonitorCC(cc != 0 ? cc : ((_cc != 0) ? _cc : 1));
    } else {
        // Standalone orientation probe: do not leave a pull-down behind.
        writeRegister(REG_SWITCHES0, 0);
    }

    if (cc == 0) {
        _cc = 0;
        _error = Error::NotConnected;
        return false;
    }
    _cc = cc;
    _error = Error::None;
    return true;
}

bool FUSB302::setActiveCC(uint8_t cc) {
    if (!_ready) {
        fail(Error::NotInitialized);
        return false;
    }
    if (_role == Role::Sink) {
        return configureSinkCC(cc);
    }
    if (_role == Role::Monitor) {
        return configureMonitorCC(cc);
    }
    fail(Error::NotReady);
    return false;
}

bool FUSB302::isConnected() {
    if (_role == Role::Sink && _cc == 0) {
        if ((uint32_t)(millis() - _lastOrientationMs) >= 500) {
            uint8_t cc = 0;
            detectOrientation(cc);
        }
        if (_cc == 0) {
            return false;
        }
    }

    Status status;
    if (!readStatus(status)) {
        return false;
    }
    if (_role == Role::Monitor) {
        return status.activity() || !status.rxEmpty();
    }
    return status.bcLevel() != 0;
}

uint16_t FUSB302::sourceCurrent_mA() {
    Status status;
    if (!readStatus(status)) {
        return 0;
    }
    switch (status.bcLevel()) {
        case 1: return 500;
        case 2: return 1500;
        case 3: return 3000;
        default: return 0;
    }
}

/* ------------------------------------------------------------------------- */
/* RX                                                                        */
/* ------------------------------------------------------------------------- */

bool FUSB302::readFifo(uint8_t* data, uint8_t length) {
    return readBlock(REG_FIFOS, data, length);
}

bool FUSB302::readFrame(PDMessage& message) {
    message.clear();

    uint8_t token = 0;
    if (!readFifo(&token, 1)) {
        return false;
    }
    Sop sop = sopFromToken(token);
    if (sop == Sop::Invalid) {
        fail(Error::RxError);
        return false;
    }

    uint8_t headerBytes[2];
    if (!readFifo(headerBytes, 2)) {
        return false;
    }
    uint16_t header = (uint16_t)headerBytes[0] | ((uint16_t)headerBytes[1] << 8);
    bool extended = (header & 0x8000u) != 0;
    uint8_t count = (uint8_t)((header >> 12) & 0x07);

    uint8_t payload[PDMessage::MAX_PAYLOAD];
    memset(payload, 0, sizeof(payload));
    payload[0] = headerBytes[0];
    payload[1] = headerBytes[1];
    uint8_t payloadLength = 2;

    if (extended) {
        uint8_t ext[2];
        if (!readFifo(ext, 2)) {
            return false;
        }
        payload[2] = ext[0];
        payload[3] = ext[1];
        payloadLength = 4;

        if (count == 0) {
            fail(Error::RxError);
            return false;
        }
        // Extended messages still send NDO complete 4-byte objects. The
        // Extended Header's Data Size excludes padding.
        uint16_t bodyBytes = (uint16_t)(count * 4);
        if ((uint16_t)(2 + bodyBytes) > PDMessage::MAX_PAYLOAD) {
            fail(Error::RxError);
            return false;
        }
        payloadLength = (uint8_t)(2 + bodyBytes);
        uint8_t remaining = (uint8_t)(bodyBytes - 2);
        if (remaining > 0 && !readFifo(&payload[4], remaining)) {
            return false;
        }
    } else {
        uint8_t dataBytes = (uint8_t)(count * 4);
        payloadLength = (uint8_t)(2 + dataBytes);
        if (payloadLength > PDMessage::MAX_PAYLOAD) {
            fail(Error::RxError);
            return false;
        }
        if (dataBytes > 0 && !readFifo(&payload[2], dataBytes)) {
            return false;
        }
    }

    uint8_t crcBytes[4];
    if (!readFifo(crcBytes, 4)) {
        return false;
    }

    uint32_t expected = readLe32(crcBytes);
    uint32_t actual = crc32(payload, payloadLength);

    message.sop = sop;
    message.cc = _cc;
    message.payloadLength = payloadLength;
    message.crcValid = (expected == actual);
    message.timestamp = millis();
    memcpy(message.payload, payload, payloadLength);
    return true;
}

bool FUSB302::pushMessage(const PDMessage& message) {
    if (_qCount >= FUSB302_RX_QUEUE_SIZE) {
        return false;
    }
    _queue[_qTail] = message;
    _qTail = (uint8_t)((_qTail + 1) % FUSB302_RX_QUEUE_SIZE);
    ++_qCount;
    return true;
}

bool FUSB302::popMessage(PDMessage& message) {
    if (_qCount == 0) {
        return false;
    }
    message = _queue[_qHead];
    _qHead = (uint8_t)((_qHead + 1) % FUSB302_RX_QUEUE_SIZE);
    --_qCount;
    return true;
}

uint8_t FUSB302::pumpFrames(uint8_t maxFrames, bool forceIrq) {
    if (!_ready) {
        return 0;
    }

    // A single register read tells us whether anything is waiting. A PD
    // handshake delivers several frames in a few hundred microseconds, so the
    // monitor has to be polled far faster than the FIFO can absorb; keep the
    // idle path at one I2C transaction.
    uint8_t preStatus1 = 0;
    if (!readRegister(REG_STATUS1, preStatus1)) {
        return 0;
    }
    _status.status1 = preStatus1;
    bool rxEmpty = (preStatus1 & ST1_RX_EMPTY) != 0;
    if (rxEmpty && !forceIrq) {
        return 0;
    }

    InterruptFlags flags;
    if (!readRegister(REG_INTERRUPTA, flags.a) ||
        !readRegister(REG_INTERRUPTB, flags.b) ||
        !readRegister(REG_INTERRUPT, flags.irq)) {
        return 0;
    }
    _irq = flags;

    if (flags.a & IRQA_HARDRST) {
        _hardResetReceived = true;
        _messageId = 0;
    }
    if (flags.a & IRQA_SOFTRST) {
        _softResetReceived = true;
        _messageId = 0;
    }

    if (rxEmpty) {
        return 0;
    }

    uint8_t added = 0;
    // RX_EMPTY only means "not empty": the FIFO may still be receiving a frame.
    // CRC_CHK (or a full FIFO) confirms a complete frame. Once draining, keep
    // reading until the FIFO is empty: the CRC_CHK bit is coalesced, so several
    // frames may share one flag.
    bool draining = (flags.irq & IRQ_CRC_CHK) != 0;
    for (uint8_t i = 0; i < maxFrames; ++i) {
        uint8_t status1 = 0;
        if (!readRegister(REG_STATUS1, status1)) {
            break;
        }
        _status.status1 = status1;
        if (status1 & ST1_RX_EMPTY) {
            draining = false;
            break;
        }

        if (!draining) {
            if (status1 & ST1_RX_FULL) {
                // The FIFO filled before we got here: the tail of the burst is
                // already gone. Count it so a lost handshake is distinguishable
                // from a silent link.
                ++_overflowEvents;
                draining = true;
            } else {
                uint8_t irq = 0;
                if (!readRegister(REG_INTERRUPT, irq)) {
                    break;
                }
                draining = (irq & IRQ_CRC_CHK) != 0;
                if (!draining) {
                    break;
                }
            }
        }

        PDMessage message;
        if (!readFrame(message)) {
            flushRx();
            ++_invalidFrames;
            break;
        }

        ++_rxFrames;
        if (!message.crcValid) {
            ++_invalidFrames;
        } else if (message.sop == Sop::Partner &&
                   !(message.isControl() &&
                     message.type() == (uint8_t)ControlType::GoodCRC)) {
            uint8_t revision = message.revision();
            if (revision == 1 || revision == 2) {
                _peerRevision = revision;
            }
        }
        if (_emulationEnabled && !_cableInspection) {
            prepareEmulationReply(message);
        }
        if (!pushMessage(message)) {
            ++_droppedFrames;
            break;
        }
        ++added;
    }
    return added;
}

uint8_t FUSB302::poll(bool forceIrq) {
    uint8_t added = pumpFrames(4, forceIrq);
    if (_emulationDue && !_inEmulationReply) {
        sendEmulationReply();
    }
    return added;
}

bool FUSB302::readMessage(PDMessage& message) {
    if (_qCount == 0) {
        poll();
    }
    return popMessage(message);
}

bool FUSB302::waitForMessage(PDMessage& message, uint32_t timeoutMs,
                             Sop sop, int type) {
    uint32_t start = millis();
    do {
        poll();
        while (popMessage(message)) {
            if ((sop == Sop::Invalid || message.sop == sop) &&
                (type < 0 || (int)message.type() == type)) {
                return true;
            }
        }
        delay(1);
    } while (!elapsed(start, timeoutMs));
    return false;
}

/* ------------------------------------------------------------------------- */
/* TX                                                                        */
/* ------------------------------------------------------------------------- */

uint8_t FUSB302::nextMessageId() {
    uint8_t id = _messageId;
    _messageId = (uint8_t)((_messageId + 1) & 0x07);
    return id;
}

bool FUSB302::goodCrcQueued(Sop sop, uint8_t messageId) {
    for (uint8_t i = 0; i < _qCount; ++i) {
        uint8_t index = (uint8_t)((_qHead + i) % FUSB302_RX_QUEUE_SIZE);
        const PDMessage& m = _queue[index];
        if (m.sop == sop && !m.extended() && m.objectCount() == 0 &&
            m.type() == (uint8_t)ControlType::GoodCRC &&
            m.messageId() == messageId) {
            return true;
        }
    }
    return false;
}

void FUSB302::dropGoodCrc() {
    uint8_t kept = 0;
    for (uint8_t i = 0; i < _qCount; ++i) {
        uint8_t index = (uint8_t)((_qHead + i) % FUSB302_RX_QUEUE_SIZE);
        const PDMessage& m = _queue[index];
        bool isGoodCrc = (!m.extended() && m.objectCount() == 0 &&
                          m.type() == (uint8_t)ControlType::GoodCRC);
        if (!isGoodCrc) {
            _queue[(uint8_t)((_qHead + kept) % FUSB302_RX_QUEUE_SIZE)] = m;
            ++kept;
        }
    }
    _qCount = kept;
    _qTail = (uint8_t)((_qHead + kept) % FUSB302_RX_QUEUE_SIZE);
}

bool FUSB302::waitTxCompletion(uint32_t timeoutMs, Sop sop, uint8_t messageId) {
    uint32_t start = millis();
    do {
        pumpFrames(2, true);
        if (_irq.a & IRQA_RETRYFAIL) {
            fail(Error::TxFailed);
            return false;
        }
        if ((_irq.a & IRQA_TXSENT) || goodCrcQueued(sop, messageId)) {
            _error = Error::None;
            return true;
        }
        delay(1);
    } while (!elapsed(start, timeoutMs));

    fail(Error::TxTimeout);
    return false;
}

bool FUSB302::sendMessage(const PDMessage& message, uint32_t timeoutMs,
                          bool waitGoodCrc) {
    if (!_ready) {
        fail(Error::NotInitialized);
        return false;
    }
    if (_role != Role::Sink) {
        // A passive monitor must not acknowledge or transmit.
        fail(Error::NotReady);
        return false;
    }
    return sendMessageRaw(message, timeoutMs, waitGoodCrc);
}

bool FUSB302::sendMessageRaw(const PDMessage& message, uint32_t timeoutMs,
                             bool waitGoodCrc) {
    uint8_t length = message.payloadLength;
    if (length < 2 || length > PDMessage::MAX_PAYLOAD) {
        fail(Error::InvalidArgument);
        return false;
    }
    if (!message.extended() &&
        length < (uint8_t)(2 + 4 * message.objectCount())) {
        fail(Error::InvalidArgument);
        return false;
    }

    Sop sop = (message.sop == Sop::Invalid) ? Sop::Partner : message.sop;

    // Make sure the TX FIFO is empty.
    uint8_t status1 = 0;
    if (!readRegister(REG_STATUS1, status1)) {
        return false;
    }
    if (!(status1 & ST1_TX_EMPTY)) {
        uint8_t control0 = 0;
        if (!readRegister(REG_CONTROL0, control0)) {
            return false;
        }
        writeRegister(REG_CONTROL0, (uint8_t)(control0 | C0_TX_FLUSH));
        delay(1);
    }
    dropGoodCrc();

    uint8_t sopSymbols[4];
    switch (sop) {
        case Sop::Cable:
            sopSymbols[0] = TX_TOKEN_SYNC1;
            sopSymbols[1] = TX_TOKEN_SYNC1;
            sopSymbols[2] = TX_TOKEN_SYNC3;
            sopSymbols[3] = TX_TOKEN_SYNC3;
            break;
        case Sop::CableDoublePrime:
            sopSymbols[0] = TX_TOKEN_SYNC1;
            sopSymbols[1] = TX_TOKEN_SYNC3;
            sopSymbols[2] = TX_TOKEN_SYNC1;
            sopSymbols[3] = TX_TOKEN_SYNC3;
            break;
        case Sop::Debug:
            sopSymbols[0] = TX_TOKEN_SYNC1;
            sopSymbols[1] = TX_TOKEN_RST2;
            sopSymbols[2] = TX_TOKEN_RST2;
            sopSymbols[3] = TX_TOKEN_SYNC3;
            break;
        case Sop::DebugDoublePrime:
            sopSymbols[0] = TX_TOKEN_SYNC1;
            sopSymbols[1] = TX_TOKEN_RST2;
            sopSymbols[2] = TX_TOKEN_SYNC3;
            sopSymbols[3] = TX_TOKEN_SYNC2;
            break;
        case Sop::Partner:
        default:
            sopSymbols[0] = TX_TOKEN_SYNC1;
            sopSymbols[1] = TX_TOKEN_SYNC1;
            sopSymbols[2] = TX_TOKEN_SYNC1;
            sopSymbols[3] = TX_TOKEN_SYNC2;
            break;
    }

    uint8_t fifo[RX_FIFO_SIZE];
    uint8_t index = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        fifo[index++] = sopSymbols[i];
    }
    fifo[index++] = (uint8_t)(TX_TOKEN_PACKSYM | (length & 0x1F));
    memcpy(&fifo[index], message.payload, length);
    index = (uint8_t)(index + length);
    fifo[index++] = TX_TOKEN_JAMCRC;
    fifo[index++] = TX_TOKEN_EOP;
    if (sop == Sop::Partner) {
        // TXOFF must precede TXON for SOP; cable/debug frames keep it simple.
        fifo[index++] = TX_TOKEN_TXOFF;
    }
    fifo[index++] = TX_TOKEN_TXON;

    // Automatic retries: retry three times after the first attempt.
    if (!writeRegister(REG_CONTROL3, (uint8_t)(C3_N_RETRIES_3 | C3_AUTO_RETRY))) {
        return false;
    }

    clearInterrupts();
    if (!writeBlock(REG_FIFOS, fifo, index)) {
        return false;
    }

    ++_txFrames;

    if (!waitGoodCrc) {
        _error = Error::None;
        return true;
    }

    return waitTxCompletion(timeoutMs, sop, message.messageId());
}

bool FUSB302::sendControl(ControlType type, uint8_t messageId, uint32_t timeoutMs) {
    PDMessage message;
    SpecRevision revision = (_peerRevision == 1) ? SpecRevision::PD2_0 : SpecRevision::PD3_0;
    message.initControl(type, revision);
    message.setMessageId(messageId == 0xFF ? nextMessageId() : (uint8_t)(messageId & 0x07));
    return sendMessage(message, timeoutMs);
}

bool FUSB302::nudgeSourceCapabilities(uint8_t attempts, uint32_t timeoutMs) {
    if (!_ready) {
        fail(Error::NotInitialized);
        return false;
    }
    if (_role != Role::Monitor || _cc == 0) {
        fail(Error::NotReady);
        return false;
    }

    uint8_t savedSwitches1 = 0;
    uint8_t savedControl3 = 0;
    uint8_t savedPower = 0;
    if (!readRegister(REG_SWITCHES1, savedSwitches1)) {
        return false;
    }
    readRegister(REG_CONTROL3, savedControl3);
    readRegister(REG_POWER, savedPower);

    // AUTO_CRC lets the hardware append the CRC and check the GoodCRC; the
    // monitor normally runs without it and with both drivers off. The
    // oscillator (off in the RX-only configuration) is needed to transmit.
    uint8_t txSwitches1 = (uint8_t)(SW1_SPECREV_PD2 | SW1_AUTO_CRC |
                                    ((_cc == 1) ? SW1_TXCC1_EN : SW1_TXCC2_EN));
    bool sent = false;
    if (writeRegister(REG_POWER, PWR_ALL) &&
        writeRegister(REG_SWITCHES1, txSwitches1)) {
        delay(1);
        for (uint8_t i = 0; i < attempts && !sent; ++i) {
            PDMessage message;
            message.initControl(ControlType::SoftReset, SpecRevision::PD2_0);
            message.setMessageId(0);
            sent = sendMessageRaw(message, timeoutMs, true);
            if (!sent) {
                delay(1);
            }
        }
    }

    // Back to passive listening. Do not flush here: the Source answers the
    // Soft_Reset with Source_Capabilities and that frame must survive.
    writeRegister(REG_SWITCHES1, savedSwitches1);
    writeRegister(REG_CONTROL3, savedControl3);
    if (savedPower != 0) {
        writeRegister(REG_POWER, savedPower);
    }

    if (sent) {
        _error = Error::None;
    } else if (_error == Error::None) {
        fail(Error::TxFailed);
    }
    return sent;
}

bool FUSB302::sendData(DataType type, const uint32_t* objects, uint8_t count,
                       uint8_t messageId, uint32_t timeoutMs) {
    if (objects == nullptr || count == 0 || count > PDMessage::MAX_OBJECTS) {
        fail(Error::InvalidArgument);
        return false;
    }
    PDMessage message;
    SpecRevision revision = (_peerRevision == 1) ? SpecRevision::PD2_0 : SpecRevision::PD3_0;
    message.initData(type, count, revision);
    message.setMessageId(messageId == 0xFF ? nextMessageId() : (uint8_t)(messageId & 0x07));
    for (uint8_t i = 0; i < count; ++i) {
        message.setObject(i, objects[i]);
    }
    return sendMessage(message, timeoutMs);
}

bool FUSB302::requestFixed(uint8_t position, uint16_t current_mA,
                           uint8_t messageId, const RequestOptions& options) {
    uint32_t rdo = buildFixedRequest(position, current_mA, options);
    return sendData(DataType::Request, &rdo, 1, messageId);
}

bool FUSB302::requestPPS(uint8_t position, uint16_t voltage_mV, uint16_t current_mA,
                         uint8_t messageId, const RequestOptions& options) {
    uint32_t rdo = buildPPSRequest(position, voltage_mV, current_mA, options);
    return sendData(DataType::Request, &rdo, 1, messageId);
}

bool FUSB302::requestAVS(uint8_t position, uint16_t voltage_mV, uint16_t current_mA,
                         uint8_t messageId, const RequestOptions& options) {
    uint32_t rdo = buildAVSRequest(position, voltage_mV, current_mA, options);
    return sendData(DataType::Request, &rdo, 1, messageId);
}

bool FUSB302::softReset(uint32_t timeoutMs) {
    PDMessage message;
    SpecRevision revision = (_peerRevision == 1) ? SpecRevision::PD2_0 : SpecRevision::PD3_0;
    message.initControl(ControlType::SoftReset, revision);
    message.setMessageId(0); // Soft_Reset always uses MessageID 0
    bool ok = sendMessage(message, timeoutMs);
    if (ok) {
        _messageId = 0;
    }
    return ok;
}

bool FUSB302::hardReset(uint32_t timeoutMs) {
    if (!_ready) {
        fail(Error::NotInitialized);
        return false;
    }
    if (_role != Role::Sink) {
        fail(Error::NotReady);
        return false;
    }

    clearInterrupts();
    uint8_t control3 = 0;
    if (!readRegister(REG_CONTROL3, control3)) {
        return false;
    }
    if (!writeRegister(REG_CONTROL3, (uint8_t)(control3 | C3_SEND_HARDRESET))) {
        return false;
    }

    uint32_t start = millis();
    do {
        pumpFrames(1);
        if (_irq.a & IRQA_HARDSENT) {
            _messageId = 0;
            _error = Error::None;
            return true;
        }
        delay(1);
    } while (!elapsed(start, timeoutMs));

    fail(Error::TxTimeout);
    return false;
}

/* ------------------------------------------------------------------------- */
/* Cable e-Marker                                                            */
/* ------------------------------------------------------------------------- */

bool FUSB302::readCableIdentity(CableIdentity& identity, uint32_t timeoutMs) {
    if (_cc == 0) {
        uint8_t cc = 0;
        if (!detectOrientation(cc)) {
            fail(Error::NotConnected);
            return false;
        }
    }
    return readCableIdentity(_cc, identity, timeoutMs);
}

bool FUSB302::readCableIdentity(uint8_t activeCC, CableIdentity& identity,
                                uint32_t timeoutMs) {
    identity = CableIdentity();

    if (!_ready) {
        fail(Error::NotInitialized);
        return false;
    }
    if (activeCC != 1 && activeCC != 2) {
        fail(Error::InvalidArgument);
        return false;
    }
    if (_role != Role::Sink && _role != Role::Monitor) {
        fail(Error::NotReady);
        return false;
    }
    bool monitor = (_role == Role::Monitor);

    uint8_t savedSwitches0 = 0;
    uint8_t savedSwitches1 = 0;
    uint8_t savedControl1 = 0;
    uint8_t savedPower = 0;
    if (!readRegister(REG_SWITCHES0, savedSwitches0) ||
        !readRegister(REG_SWITCHES1, savedSwitches1) ||
        !readRegister(REG_CONTROL1, savedControl1) ||
        !readRegister(REG_POWER, savedPower)) {
        return false;
    }

    _cableInspection = true;
    _emulationDue = false;

    bool ok = false;
    uint32_t responseObjects[PDMessage::MAX_OBJECTS];
    uint8_t responseCount = 0;
    uint8_t responseRevision = 2;

    // The e-Marker is powered on the CC line not used for SOP traffic.
    uint8_t vconn = (activeCC == 1) ? SW0_VCONN_CC2 : SW0_VCONN_CC1;
    uint8_t measure = (activeCC == 1) ? SW0_MEAS_CC1 : SW0_MEAS_CC2;
    uint8_t txcc = (activeCC == 1) ? SW1_TXCC1_EN : SW1_TXCC2_EN;

    // A passive monitor has no TX driver/oscillator configured. Temporarily
    // take the Sink role so sendMessage() is allowed, but keep Rd off (VCONN on
    // the inactive CC + measure on the active CC) and enable only the TXCC that
    // carries the SOP' probe.
    _role = Role::Sink;

    do {
        uint8_t switches1 = monitor
            ? (uint8_t)(SW1_SPECREV_PD2 | txcc)
            : (uint8_t)((savedSwitches1 & ~(SW1_TXCC1_EN | SW1_TXCC2_EN)) | txcc);
        if (!writeRegister(REG_SWITCHES0, (uint8_t)(vconn | measure)) ||
            !writeRegister(REG_SWITCHES1, switches1) ||
            !writeRegister(REG_CONTROL1, (uint8_t)(C1_ENSOP1 | C1_ENSOP2)) ||
            !writeRegister(REG_POWER, PWR_ALL)) {
            break;
        }
        flushRx();
        clearInterrupts();
        delay(50); // tVCONNStable

        // Do not start an SOP' probe while normal SOP traffic is active.
        uint32_t idleStart = millis();
        while (!elapsed(idleStart, 10)) {
            Status status;
            if (readStatus(status) && !status.activity() && status.rxEmpty()) {
                break;
            }
            delay(1);
        }

        // Synchronize the cable's SOP' MessageID.
        PDMessage softResetMessage;
        softResetMessage.sop = Sop::Cable;
        softResetMessage.initControl(ControlType::SoftReset, SpecRevision::PD3_0);
        softResetMessage.setPortPowerRole(false); // request from the port, not the cable plug
        softResetMessage.setDataRoleUfp(false);
        softResetMessage.setMessageId(0);
        sendMessage(softResetMessage, 20, false);
        delay(5);

        // Discover Identity (USB-IF SVID 0xFF00, structured VDM, command 1).
        PDMessage discover;
        discover.sop = Sop::Cable;
        discover.initData(DataType::VendorDefined, 1, SpecRevision::PD3_0);
        discover.setPortPowerRole(false);
        discover.setDataRoleUfp(false);
        discover.setMessageId(0);
        discover.setObject(0, 0xFF008001u);

        _error = Error::None;
        if (!sendMessage(discover, 20, false)) {
            break;
        }

        // The cable answers within a few milliseconds; allow extra margin.
        uint32_t start = millis();
        bool received = false;
        do {
            poll();
            PDMessage response;
            while (readMessage(response)) {
                uint32_t objects[PDMessage::MAX_OBJECTS];
                uint8_t count = 0;
                if (!findCableIdentityObjects(response, objects, count)) {
                    continue;
                }
                uint32_t vdm = objects[0];
                uint8_t commandType = (uint8_t)((vdm >> 6) & 0x03);
                if (commandType == 0) {
                    continue; // request, not a response
                }
                if ((vdm >> 16) != 0xFF00u || (vdm & 0x8000u) == 0 ||
                    (vdm & 0x1Fu) != 1u) {
                    continue; // not a Discover Identity response
                }
                memcpy(responseObjects, objects, sizeof(uint32_t) * count);
                responseCount = count;
                responseRevision = response.revision();
                received = true;
                break;
            }
            if (received) {
                break;
            }
            delay(1);
        } while (!elapsed(start, timeoutMs));

        if (!received) {
            fail(Error::TxTimeout);
            break;
        }

        CableResult result = decodeCableIdentity(responseObjects, responseCount,
                                                responseRevision, identity);
        if (result == CableResult::Ok || result == CableResult::Nak ||
            result == CableResult::Busy) {
            _error = Error::None;
            ok = true;
        } else {
            fail(Error::RxError);
        }
    } while (false);

    // Restore the routing used for normal PD traffic.
    writeRegister(REG_CONTROL1, savedControl1);
    writeRegister(REG_SWITCHES0, savedSwitches0);
    writeRegister(REG_SWITCHES1, savedSwitches1);
    writeRegister(REG_POWER, savedPower);
    flushRx();
    clearInterrupts();
    _cableInspection = false;

    if (monitor) {
        _role = Role::Monitor;
        configureMonitorCC(activeCC);
    } else {
        _role = Role::Sink;
    }

    return ok;
}

void FUSB302::setCableEmulation(const CableEmulationConfig& config) {
    _emulation = config;
    _emulationObjectCount = buildCableIdentityObjects(config, _emulationObjects, 5);
    _emulationMessageId = 0;
    _emulationDue = false;
}

void FUSB302::enableCableEmulation(bool enable) {
    _emulationEnabled = enable;
    _emulationDue = false;

    if (_role == Role::Sink && _cc != 0) {
        configureSinkCC(_cc);
    } else if (!enable) {
        uint8_t control1 = 0;
        if (readRegister(REG_CONTROL1, control1)) {
            writeRegister(REG_CONTROL1, (uint8_t)(control1 & ~(C1_ENSOP1 | C1_ENSOP2)));
        }
    }
}

void FUSB302::prepareEmulationReply(const PDMessage& request) {
    if (!_emulationEnabled || _cableInspection || _inEmulationReply) {
        return;
    }
    if (request.sop != Sop::Cable) {
        return;
    }
    // SOP' messages from the Source address the cable plug (bit 8 = 0).
    if (request.portPowerRole()) {
        return;
    }

    uint16_t header = request.header();

    if (!request.extended() && request.objectCount() == 0 &&
        request.type() == (uint8_t)ControlType::SoftReset) {
        _emulationMessageId = 0;
        return;
    }

    if (!request.isData() || request.type() != (uint8_t)DataType::VendorDefined) {
        return;
    }
    if (request.objectCount() < 1) {
        return;
    }

    uint32_t vdm = request.object(0);
    if ((vdm >> 16) != 0xFF00u) {
        return;
    }
    if ((vdm & 0x8000u) == 0) {
        return;
    }
    if ((vdm & 0x1Fu) != 1u) {
        return; // only Discover Identity is answered
    }

    _emulationRequestHeader = header;
    _emulationRequestVdm = vdm;
    _emulationDue = true;
}

void FUSB302::sendEmulationReply() {
    if (!_emulationDue || _inEmulationReply) {
        return;
    }
    _emulationDue = false;
    _inEmulationReply = true;

    uint8_t count = (uint8_t)(1 + _emulationObjectCount);
    if (count > PDMessage::MAX_OBJECTS) {
        count = PDMessage::MAX_OBJECTS;
    }

    uint8_t revisionCode = (uint8_t)((_emulationRequestHeader >> 6) & 0x03);
    SpecRevision revision = (revisionCode == 2) ? SpecRevision::PD3_0 : SpecRevision::PD2_0;

    PDMessage reply;
    reply.sop = Sop::Cable;
    reply.initData(DataType::VendorDefined, count, revision);
    reply.setPortPowerRole(true);   // cable plug
    reply.setDataRoleUfp(false);
    reply.setMessageId(_emulationMessageId);

    uint32_t ack = (_emulationRequestVdm & ~0x00C0u) | 0x0040u; // Command Type = ACK
    reply.setObject(0, ack);

    for (uint8_t i = 0; i < _emulationObjectCount && (uint8_t)(i + 1) < count; ++i) {
        reply.setObject((uint8_t)(i + 1), _emulationObjects[i]);
    }

    if (sendMessage(reply, 20, true)) {
        _emulationMessageId = (uint8_t)((_emulationMessageId + 1) & 0x07);
    }
    _inEmulationReply = false;
}

/* ------------------------------------------------------------------------- */
/* Diagnostics                                                               */
/* ------------------------------------------------------------------------- */

void FUSB302::clearResetFlags() {
    _hardResetReceived = false;
    _softResetReceived = false;
}

const char* FUSB302::lastErrorString() const {
    switch (_error) {
        case Error::None:           return "OK";
        case Error::InvalidArgument:return "invalid argument";
        case Error::BusError:       return "I2C bus error";
        case Error::DeviceNotFound: return "FUSB302 not found";
        case Error::NotInitialized: return "not initialized";
        case Error::NotConnected:   return "no source / no cable";
        case Error::NotReady:       return "wrong role for this operation";
        case Error::Busy:           return "busy";
        case Error::TxTimeout:      return "TX timeout (no GoodCRC)";
        case Error::TxFailed:       return "TX retries failed";
        case Error::RxError:        return "RX frame error";
        case Error::QueueFull:      return "RX queue full";
        case Error::Unsupported:    return "unsupported";
        default:                    return "unknown";
    }
}
