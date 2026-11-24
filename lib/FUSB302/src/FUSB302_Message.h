/**
 * USB Power Delivery message model, PDO parsing and request builders.
 *
 * The wire format follows USB PD 3.2 / 2.0:
 *   Header: [15] Extended | [14:12] NDO | [11:9] MessageID
 *           [8] PowerRole/CablePlug | [7:6] SpecRev | [5] DataRole | [4:0] Type
 *   Data objects are little-endian 32-bit words.
 *
 * Licensed under the MIT License. See LICENSE.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

#include "FUSB302_Registers.h"

namespace fusb302 {

/// Start-of-packet sequence (USB PD K-code addressing).
enum class Sop : uint8_t {
    Invalid = 0,
    Partner,          ///< SOP   (port partner)
    Cable,            ///< SOP'  (cable plug, first controller)
    CableDoublePrime, ///< SOP'' (cable plug, second controller)
    Debug,            ///< SOP' Debug
    DebugDoublePrime  ///< SOP'' Debug
};

/// Specification revision as encoded in the Message Header (bits 7:6).
enum class SpecRevision : uint8_t {
    PD2_0 = 1,
    PD3_0 = 2
};

/// Control message types (Header Type field, NDO == 0).
enum class ControlType : uint8_t {
    GoodCRC              = 0x01,
    GotoMin              = 0x02,
    Accept               = 0x03,
    Reject               = 0x04,
    Ping                 = 0x05,
    PsReady              = 0x06,
    GetSourceCap         = 0x07,
    GetSinkCap           = 0x08,
    DrSwap               = 0x09,
    PrSwap               = 0x0A,
    VconnSwap            = 0x0B,
    Wait                 = 0x0C,
    SoftReset            = 0x0D,
    DataReset            = 0x0E,
    DataResetComplete    = 0x0F,
    NotSupported         = 0x10,
    GetSourceCapExtended = 0x11,
    GetStatus            = 0x12,
    FrSwap               = 0x13,
    GetPpsStatus         = 0x14,
    GetCountryCodes      = 0x15,
    GetSinkCapExtended   = 0x16
};

/// Data message types (Header Type field, NDO != 0).
enum class DataType : uint8_t {
    SourceCapabilities = 0x01,
    Request            = 0x02,
    Bist               = 0x03,
    SinkCapabilities   = 0x04,
    BatteryStatus      = 0x05,
    Alert              = 0x06,
    GetCountryInfo     = 0x07,
    EnterUsb           = 0x08,
    EprRequest         = 0x09,
    EprMode            = 0x0A,
    SourceInfo         = 0x0B,
    Revision           = 0x0C,
    VendorDefined      = 0x0F
};

/// Extended message types (Header Type field with bit 15 set).
enum class ExtendedType : uint8_t {
    EprSourceCapabilities      = 0x01,
    Status                     = 0x02,
    GetBatteryCap              = 0x03,
    GetBatteryStatus           = 0x04,
    BatteryCapabilities        = 0x05,
    GetManufacturerInfo        = 0x06,
    ManufacturerInfo           = 0x07,
    SecurityRequest            = 0x08,
    SecurityResponse           = 0x09,
    FirmwareUpdateRequest      = 0x0A,
    FirmwareUpdateResponse     = 0x0B,
    PpsStatus                  = 0x0C,
    CountryInfo                = 0x0D,
    CountryCodes               = 0x0E,
    SinkCapabilitiesExtended   = 0x0F,
    ExtendedControl            = 0x10
};

/// Parsed power data object kind.
enum class SupplyType : uint8_t {
    Fixed = 0,
    Battery = 1,
    Variable = 2,
    PPS = 3,
    AVS = 4,
    Unknown = 0xFF
};

/// One decoded message. `payload` always starts with the 2-byte Message Header
/// and contains header + data (the CRC is validated and removed).
struct PDMessage {
    enum {
        MAX_OBJECTS = fusb302::MAX_DATA_OBJECTS,
        MAX_PAYLOAD = fusb302::MAX_MESSAGE_BYTES
    };

    Sop sop = Sop::Invalid;
    uint8_t cc = 0;
    uint8_t payload[MAX_PAYLOAD];
    uint8_t payloadLength = 0;
    bool crcValid = false;
    uint32_t timestamp = 0;  ///< millis() when the frame was read

    void clear();

    uint16_t header() const;
    void setHeader(uint16_t header);

    uint8_t type() const;              ///< Raw Type field (bits 4:0)
    bool extended() const;             ///< Extended bit (15)
    bool isControl() const;            ///< Control message (no data objects)
    bool isData() const;               ///< Data message (at least one object)
    uint8_t objectCount() const;       ///< NDO field (bits 14:12)
    uint8_t messageId() const;         ///< MessageID field (bits 11:9)
    void setMessageId(uint8_t id);
    uint8_t revisionCode() const;      ///< Raw SpecRev field (bits 7:6)
    uint8_t revision() const;          ///< 1 = PD2, 2 = PD3 (0 normalized to 1)
    bool portPowerRole() const;        ///< Bit 8: 1 = source / cable plug
    void setPortPowerRole(bool sourceOrCablePlug);
    bool dataRoleUfp() const;          ///< Bit 5: 1 = UFP
    void setDataRoleUfp(bool ufp);

    uint32_t object(uint8_t index) const;
    bool setObject(uint8_t index, uint32_t value);

    void initControl(ControlType type, SpecRevision rev = SpecRevision::PD3_0);
    void initData(DataType type, uint8_t numObjects, SpecRevision rev = SpecRevision::PD3_0);
    void initExtended(ExtendedType type, uint8_t numObjects, SpecRevision rev = SpecRevision::PD3_0);

    /// Extended Message Header (first 2 bytes of data, only if extended()).
    uint16_t extendedHeader() const;
    void setExtendedHeader(uint16_t value);
    /// Data Size field of the Extended Message Header (excludes padding).
    uint16_t extendedDataSize() const;

    /// Human readable message type, e.g. "Source_Capabilities".
    const char* typeName() const;
};

/// Decoded power data object (PDO/APDO).
struct PDSupply {
    SupplyType type = SupplyType::Unknown;
    uint8_t position = 0;          ///< 1-based object position in Source_Capabilities
    uint16_t minVoltage_mV = 0;
    uint16_t maxVoltage_mV = 0;
    uint16_t maxCurrent_mA = 0;
    uint32_t maxPower_mW = 0;
    uint32_t raw = 0;

    bool eprCapable = false;
    bool unconstrainedPower = false;
    bool dualRolePower = false;
    bool usbCommunications = false;
    bool dualRoleData = false;
    bool usbSuspend = false;

    bool isFixed() const { return type == SupplyType::Fixed; }
    bool isPPS() const { return type == SupplyType::PPS; }
    bool isAVS() const { return type == SupplyType::AVS; }
};

/// Parsed Source_Capabilities message.
struct PDAdvertisements {
    PDSupply pdo[PDMessage::MAX_OBJECTS];
    uint8_t count = 0;
};

/// Optional RDO flags shared by fixed, PPS and AVS requests.
struct RequestOptions {
    bool usbCommunications = false;   ///< RDO bit 25
    bool noUsbSuspend = true;         ///< RDO bit 24
    bool eprCapable = false;          ///< RDO bit 23
    bool giveBack = false;            ///< RDO bit 27
    bool capabilityMismatch = false;  ///< RDO bit 26
};

/// USB PD CRC-32 (poly 0x04C11DB7 reflected, init 0xFFFFFFFF, final XOR).
uint32_t crc32(const uint8_t* data, size_t length);

/// Parses a Source_Capabilities message into `out`.
bool parseSourceCapabilities(const PDMessage& message, PDAdvertisements& out);

/// Builds a Fixed/Variable RDO. Current is rounded up to 10 mA steps.
uint32_t buildFixedRequest(uint8_t position, uint16_t operatingCurrent_mA,
                           const RequestOptions& options = RequestOptions());

/// Builds a PPS RDO. Voltage is rounded to 20 mV, current to 50 mA.
uint32_t buildPPSRequest(uint8_t position, uint16_t voltage_mV, uint16_t current_mA,
                         const RequestOptions& options = RequestOptions());

/// Builds an SPR AVS RDO. Voltage is rounded to 25 mV, current to 50 mA.
uint32_t buildAVSRequest(uint8_t position, uint16_t voltage_mV, uint16_t current_mA,
                         const RequestOptions& options = RequestOptions());

/// Text formatting helpers for a protocol analyzer.
class ProtocolAnalyzer {
public:
    static const char* sopName(Sop sop);
    static const char* senderName(const PDMessage& message);
    static const char* messageName(const PDMessage& message);

    static void printMessage(Print& out, const PDMessage& message, bool details = true);
    static void printSourceCapabilities(Print& out, const PDMessage& message);
    static void printSupply(Print& out, const PDSupply& supply);
};

} // namespace fusb302
