/**
 * FUSB302 USB Type-C / USB Power Delivery controller for ESP32 Arduino.
 *
 * Features:
 *  - Role: sink (Rd + automatic GoodCRC) or passive protocol monitor
 *  - CC orientation detection and Rp current detection
 *  - RX FIFO framing for SOP / SOP' / SOP'' (header, objects, CRC check)
 *  - TX FIFO framing with automatic retries and GoodCRC tracking
 *  - Fixed / PPS / AVS Request generation
 *  - SOP' e-Marker discovery (Soft_Reset + Discover Identity)
 *  - e-Marker emulation (answer Discover Identity as a cable plug)
 *
 * Licensed under the MIT License. See LICENSE.
 */
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

#include "FUSB302_Registers.h"
#include "FUSB302_Message.h"
#include "FUSB302_Cable.h"

#ifndef FUSB302_RX_QUEUE_SIZE
#define FUSB302_RX_QUEUE_SIZE 8
#endif

namespace fusb302 {

enum class Role : uint8_t {
    None = 0,
    Sink,
    Monitor
};

enum class Error : uint8_t {
    None = 0,
    InvalidArgument,
    BusError,
    DeviceNotFound,
    NotInitialized,
    NotConnected,
    NotReady,
    Busy,
    TxTimeout,
    TxFailed,
    RxError,
    QueueFull,
    Unsupported
};

struct InterruptFlags {
    uint8_t a = 0;    ///< INTERRUPTA (read-clear)
    uint8_t b = 0;    ///< INTERRUPTB (read-clear)
    uint8_t irq = 0;  ///< INTERRUPT (read-clear)

    bool hardReset() const { return (a & IRQA_HARDRST) != 0; }
    bool softReset() const { return (a & IRQA_SOFTRST) != 0; }
    bool txSent() const { return (a & IRQA_TXSENT) != 0; }
    bool retryFailed() const { return (a & IRQA_RETRYFAIL) != 0; }
    bool goodCrcSent() const { return (b & IRQB_GCRCSENT) != 0; }
    bool activity() const { return (irq & IRQ_ACTIVITY) != 0; }
    bool crcChecked() const { return (irq & IRQ_CRC_CHK) != 0; }
    bool collision() const { return (irq & IRQ_COLLISION) != 0; }
};

struct Status {
    uint8_t status0 = 0;   ///< 0x40
    uint8_t status1 = 0;   ///< 0x41
    uint8_t status1a = 0;  ///< 0x3D

    bool vbusOk() const { return (status0 & ST0_VBUSOK) != 0; }
    bool activity() const { return (status0 & ST0_ACTIVITY) != 0; }
    uint8_t bcLevel() const { return (uint8_t)(status0 & ST0_BC_LVL); }
    bool rxEmpty() const { return (status1 & ST1_RX_EMPTY) != 0; }
    bool rxFull() const { return (status1 & ST1_RX_FULL) != 0; }
    bool txEmpty() const { return (status1 & ST1_TX_EMPTY) != 0; }
    bool txFull() const { return (status1 & ST1_TX_FULL) != 0; }
};

struct RegisterSnapshot {
    uint8_t deviceId = 0;
    uint8_t switches0 = 0;
    uint8_t switches1 = 0;
    uint8_t measure = 0;
    uint8_t control0 = 0;
    uint8_t control1 = 0;
    uint8_t control2 = 0;
    uint8_t control3 = 0;
    uint8_t power = 0;
    uint8_t mask = 0;
    uint8_t maska = 0;
    uint8_t maskb = 0;
    uint8_t status0 = 0;
    uint8_t status1 = 0;
};

} // namespace fusb302

/// FUSB302 controller. One instance per physical chip / I2C bus.
class FUSB302 {
public:
    FUSB302();
    explicit FUSB302(TwoWire& wire, uint8_t address = fusb302::DEFAULT_I2C_ADDRESS);

    /* ---------------------------------------------------------------- */
    /* Setup                                                            */
    /* ---------------------------------------------------------------- */

    /// Verifies the chip and resets the PD logic. Assumes Wire is already
    /// initialized. The default instance uses the global `Wire` object.
    bool begin();

    /// Selects an I2C bus (does not call Wire.begin()) and verifies the chip.
    bool begin(TwoWire& wire, uint8_t address = fusb302::DEFAULT_I2C_ADDRESS);

    /// Configures the ESP32 I2C pins then verifies the chip.
    bool begin(int sdaPin, int sclPin, uint32_t frequency = 400000);

    void end();
    bool isReady() const { return _ready; }
    uint8_t address() const { return _address; }

    /* ---------------------------------------------------------------- */
    /* Roles and CC                                                     */
    /* ---------------------------------------------------------------- */

    /// Configures Rd on both CCs, determines the orientation, enables the BMC
    /// transmitter and automatic GoodCRC. It is safe to call without a cable.
    bool beginSink();

    /// Passive monitor: no Rd, no Rp, no GoodCRC, no transmission.
    /// `cc` selects the CC line to listen on (1 or 2).
    bool beginMonitor(uint8_t cc = 1);

    fusb302::Role role() const { return _role; }
    uint8_t activeCC() const { return _cc; }

    /// Measures both CC pins and stores the orientation. Returns true if a
    /// source was detected. `settleMs` is the measurement settling time.
    bool detectOrientation(uint8_t& cc, uint16_t settleMs = 30);

    /// Re-applies the CC routing for sink or monitor mode.
    bool setActiveCC(uint8_t cc);

    /// Reads STATUS0 and reports whether a source is driving the active CC
    /// (BC_LVL). In monitor mode it reports BMC activity / pending RX.
    bool isConnected();

    /// Rp current capability decoded from BC_LVL (0, 500, 1500 or 3000 mA).
    uint16_t sourceCurrent_mA();

    bool readStatus(fusb302::Status& status);

    /// Measures one CC pin without changing the active role/termination.
    /// Used for runtime orientation selection; restores SWITCHES0 afterwards.
    bool readCCStatus(uint8_t cc, uint8_t& status0);

    /* ---------------------------------------------------------------- */
    /* Register access / diagnostics                                    */
    /* ---------------------------------------------------------------- */

    uint8_t deviceId();
    bool reset();
    bool readRegisters(fusb302::RegisterSnapshot& snapshot);

    bool readRegister(uint8_t reg, uint8_t& value);
    bool writeRegister(uint8_t reg, uint8_t value);
    bool updateRegister(uint8_t reg, uint8_t mask, uint8_t value);
    bool readBlock(uint8_t reg, uint8_t* data, uint8_t length);
    bool writeBlock(uint8_t reg, const uint8_t* data, uint8_t length);

    fusb302::Error lastError() const { return _error; }
    const char* lastErrorString() const;

    uint32_t rxFrameCount() const { return _rxFrames; }
    uint32_t txFrameCount() const { return _txFrames; }
    uint32_t invalidFrameCount() const { return _invalidFrames; }
    uint32_t droppedFrameCount() const { return _droppedFrames; }

    /// Number of times the RX FIFO was found full before it could be drained,
    /// i.e. the tail of a burst (typically a PD handshake) was lost.
    uint32_t overflowCount() const { return _overflowEvents; }

    bool hardResetReceived() const { return _hardResetReceived; }
    bool softResetReceived() const { return _softResetReceived; }
    void clearResetFlags();

    const fusb302::InterruptFlags& lastInterrupts() const { return _irq; }
    const fusb302::Status& lastStatus() const { return _status; }

    /// Optional bus lock callbacks (e.g. a FreeRTOS mutex around Wire).
    void setI2CLock(void (*lock)(), void (*unlock)());

    /* ---------------------------------------------------------------- */
    /* Message RX / TX                                                  */
    /* ---------------------------------------------------------------- */

    /// Reads interrupts and drains up to four complete RX FIFO frames into
    /// the internal queue. Returns the number of frames added.
    /// `forceIrq` also refreshes the interrupt snapshot when the FIFO is empty;
    /// the default path costs a single register read while idle.
    uint8_t poll(bool forceIrq = false);

    bool available() const { return _qCount > 0; }
    bool readMessage(fusb302::PDMessage& message);

    /// Polls until a matching message is taken from the queue. Non-matching
    /// messages are consumed. `type` is a raw 5-bit Header Type (-1 = any).
    bool waitForMessage(fusb302::PDMessage& message, uint32_t timeoutMs,
                        fusb302::Sop sop = fusb302::Sop::Invalid, int type = -1);

    /// Transmits one message and optionally waits for its GoodCRC.
    bool sendMessage(const fusb302::PDMessage& message, uint32_t timeoutMs = 20,
                     bool waitGoodCrc = true);

    /// Monitor-mode nudge: routes the transmitter to the monitored CC for a
    /// moment and sends a Soft_Reset so the Source re-advertises
    /// Source_Capabilities. Use it when the link was already negotiated before
    /// the monitor was ready, i.e. when no frame was ever received.
    /// The live PD link is renegotiated by this call.
    bool nudgeSourceCapabilities(uint8_t attempts = 3, uint32_t timeoutMs = 20);

    bool sendControl(fusb302::ControlType type, uint8_t messageId = 0xFF,
                     uint32_t timeoutMs = 20);
    bool sendData(fusb302::DataType type, const uint32_t* objects, uint8_t count,
                  uint8_t messageId = 0xFF, uint32_t timeoutMs = 20);

    bool requestFixed(uint8_t position, uint16_t current_mA,
                      uint8_t messageId = 0xFF,
                      const fusb302::RequestOptions& options = fusb302::RequestOptions());
    bool requestPPS(uint8_t position, uint16_t voltage_mV, uint16_t current_mA,
                    uint8_t messageId = 0xFF,
                    const fusb302::RequestOptions& options = fusb302::RequestOptions());
    bool requestAVS(uint8_t position, uint16_t voltage_mV, uint16_t current_mA,
                    uint8_t messageId = 0xFF,
                    const fusb302::RequestOptions& options = fusb302::RequestOptions());

    bool softReset(uint32_t timeoutMs = 20);
    bool hardReset(uint32_t timeoutMs = 20);

    void setMessageId(uint8_t id) { _messageId = (uint8_t)(id & 0x07); }
    uint8_t messageId() const { return _messageId; }
    uint8_t nextMessageId();

    /// Specification revision negotiated with the port partner (1 = PD2,
    /// 2 = PD3). Defaults to 2 until a partner message is received.
    uint8_t peerRevision() const { return _peerRevision; }
    void setPeerRevision(uint8_t revision) { _peerRevision = (revision == 1) ? 1 : 2; }

    /* ---------------------------------------------------------------- */
    /* Cable e-Marker                                                   */
    /* ---------------------------------------------------------------- */

    /// Powers the cable e-Marker through VCONN, synchronizes SOP' with a
    /// Soft_Reset, sends Discover Identity and decodes the ACK.
    bool readCableIdentity(fusb302::CableIdentity& identity, uint32_t timeoutMs = 300);
    bool readCableIdentity(uint8_t activeCC, fusb302::CableIdentity& identity,
                           uint32_t timeoutMs = 300);

    /// Enables automatic answers to SOP' Discover Identity requests.
    /// The port should already be configured with beginSink().
    void enableCableEmulation(bool enable);
    bool cableEmulationEnabled() const { return _emulationEnabled; }
    void setCableEmulation(const fusb302::CableEmulationConfig& config);
    const fusb302::CableEmulationConfig& cableEmulationConfig() const { return _emulation; }
    uint8_t emulatedCableMessageId() const { return _emulationMessageId; }

private:
    TwoWire* _wire;
    uint8_t _address;
    bool _ready;
    fusb302::Role _role;
    uint8_t _cc;
    uint8_t _messageId;
    uint32_t _lastOrientationMs;
    uint8_t _peerRevision;   ///< 1 = PD2, 2 = PD3 (last SOP partner message)

    fusb302::Error _error;
    fusb302::InterruptFlags _irq;
    fusb302::Status _status;

    uint32_t _rxFrames;
    uint32_t _txFrames;
    uint32_t _invalidFrames;
    uint32_t _droppedFrames;
    uint32_t _overflowEvents;

    bool _hardResetReceived;
    bool _softResetReceived;

    void (*_lockFn)();
    void (*_unlockFn)();

    fusb302::PDMessage _queue[FUSB302_RX_QUEUE_SIZE];
    uint8_t _qHead;
    uint8_t _qTail;
    uint8_t _qCount;

    fusb302::CableEmulationConfig _emulation;
    uint32_t _emulationObjects[5];
    uint8_t _emulationObjectCount;
    uint8_t _emulationMessageId;
    bool _emulationEnabled;
    bool _emulationDue;
    bool _inEmulationReply;
    uint16_t _emulationRequestHeader;
    uint32_t _emulationRequestVdm;

    bool _cableInspection;

    void lockBus();
    void unlockBus();
    void fail(fusb302::Error error);

    bool i2cWrite(const uint8_t* data, uint8_t length);
    bool i2cReadAfterWrite(const uint8_t* writeData, uint8_t writeLength,
                           uint8_t* readData, uint8_t readLength);
    bool readFifo(uint8_t* data, uint8_t length);
    bool flushRx();
    bool clearInterrupts();

    bool configureSinkCC(uint8_t cc);
    bool configureMonitorCC(uint8_t cc);
    bool readFrame(fusb302::PDMessage& message);
    uint8_t pumpFrames(uint8_t maxFrames, bool forceIrq = false);

    bool pushMessage(const fusb302::PDMessage& message);
    bool popMessage(fusb302::PDMessage& message);
    bool goodCrcQueued(fusb302::Sop sop, uint8_t messageId);
    void dropGoodCrc();

    bool sendMessageRaw(const fusb302::PDMessage& message, uint32_t timeoutMs,
                        bool waitGoodCrc);
    bool waitTxCompletion(uint32_t timeoutMs, fusb302::Sop sop, uint8_t messageId);

    void prepareEmulationReply(const fusb302::PDMessage& request);
    void sendEmulationReply();
};
