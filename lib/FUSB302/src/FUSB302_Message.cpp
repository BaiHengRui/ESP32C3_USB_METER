/**
 * USB Power Delivery message model, PDO parsing and RDO construction.
 *
 * Licensed under the MIT License. See LICENSE.
 */
#include "FUSB302_Message.h"

#include <string.h>

namespace fusb302 {

/* ------------------------------------------------------------------------- */
/* CRC-32                                                                    */
/* ------------------------------------------------------------------------- */

// Nibble-wise table for the reflected CRC-32 polynomial 0xEDB88320.
static const uint32_t CRC32_NIBBLE[16] = {
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
    0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
    0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C
};

uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc = CRC32_NIBBLE[(crc ^ data[i]) & 0x0F] ^ (crc >> 4);
        crc = CRC32_NIBBLE[(crc ^ (data[i] >> 4)) & 0x0F] ^ (crc >> 4);
    }
    return ~crc;
}

/* ------------------------------------------------------------------------- */
/* PDMessage                                                                 */
/* ------------------------------------------------------------------------- */

void PDMessage::clear() {
    sop = Sop::Invalid;
    cc = 0;
    memset(payload, 0, sizeof(payload));
    payloadLength = 0;
    crcValid = false;
    timestamp = 0;
}

uint16_t PDMessage::header() const {
    if (payloadLength < 2) {
        return 0;
    }
    return (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
}

void PDMessage::setHeader(uint16_t value) {
    if (payloadLength < 2) {
        payloadLength = 2;
    }
    payload[0] = (uint8_t)(value & 0xFF);
    payload[1] = (uint8_t)(value >> 8);
}

uint8_t PDMessage::type() const {
    return (uint8_t)(header() & 0x1F);
}

bool PDMessage::extended() const {
    return (header() & 0x8000u) != 0;
}

bool PDMessage::isControl() const {
    return !extended() && objectCount() == 0;
}

bool PDMessage::isData() const {
    return !extended() && objectCount() > 0;
}

uint8_t PDMessage::objectCount() const {
    return (uint8_t)((header() >> 12) & 0x07);
}

uint8_t PDMessage::messageId() const {
    return (uint8_t)((header() >> 9) & 0x07);
}

void PDMessage::setMessageId(uint8_t id) {
    uint16_t h = header();
    h = (uint16_t)((h & ~0x0E00u) | (((uint16_t)id & 0x07) << 9));
    setHeader(h);
}

uint8_t PDMessage::revisionCode() const {
    return (uint8_t)((header() >> 6) & 0x03);
}

uint8_t PDMessage::revision() const {
    uint8_t rev = revisionCode();
    return (rev == 0) ? 1 : rev;
}

bool PDMessage::portPowerRole() const {
    return (header() & 0x0100u) != 0;
}

void PDMessage::setPortPowerRole(bool sourceOrCablePlug) {
    uint16_t h = header();
    if (sourceOrCablePlug) {
        h |= 0x0100u;
    } else {
        h &= (uint16_t)~0x0100u;
    }
    setHeader(h);
}

bool PDMessage::dataRoleUfp() const {
    return (header() & 0x0020u) != 0;
}

void PDMessage::setDataRoleUfp(bool ufp) {
    uint16_t h = header();
    if (ufp) {
        h |= 0x0020u;
    } else {
        h &= (uint16_t)~0x0020u;
    }
    setHeader(h);
}

uint32_t PDMessage::object(uint8_t index) const {
    if (index >= objectCount()) {
        return 0;
    }
    uint16_t offset = (uint16_t)(2 + 4 * index);
    if ((uint16_t)(offset + 4) > payloadLength) {
        return 0;
    }
    return (uint32_t)payload[offset] |
           ((uint32_t)payload[offset + 1] << 8) |
           ((uint32_t)payload[offset + 2] << 16) |
           ((uint32_t)payload[offset + 3] << 24);
}

bool PDMessage::setObject(uint8_t index, uint32_t value) {
    uint8_t count = objectCount();
    if (index >= count || index >= MAX_OBJECTS) {
        return false;
    }
    uint16_t offset = (uint16_t)(2 + 4 * index);
    if (payloadLength < (uint8_t)(offset + 4)) {
        return false;
    }
    payload[offset] = (uint8_t)(value & 0xFF);
    payload[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    payload[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    payload[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
    return true;
}

static uint16_t makeHeader(uint8_t numObjects, uint8_t type, SpecRevision rev,
                           bool extended, bool cablePlug, bool ufp) {
    uint16_t h = (uint16_t)(((uint16_t)(numObjects & 0x07)) << 12);
    h |= (uint16_t)(((uint16_t)rev & 0x03) << 6);
    h |= (uint16_t)(type & 0x1F);
    if (extended) {
        h |= 0x8000u;
    }
    if (cablePlug) {
        h |= 0x0100u;
    }
    if (ufp) {
        h |= 0x0020u;
    }
    return h;
}

void PDMessage::initControl(ControlType type, SpecRevision rev) {
    memset(payload, 0, sizeof(payload));
    payloadLength = 2;
    crcValid = true;
    timestamp = 0;
    setHeader(makeHeader(0, (uint8_t)type, rev, false, false, true));
}

void PDMessage::initData(DataType type, uint8_t numObjects, SpecRevision rev) {
    if (numObjects > MAX_OBJECTS) {
        numObjects = MAX_OBJECTS;
    }
    memset(payload, 0, sizeof(payload));
    payloadLength = 2;
    crcValid = true;
    timestamp = 0;
    setHeader(makeHeader(numObjects, (uint8_t)type, rev, false, false, true));
    payloadLength = (uint8_t)(2 + 4 * numObjects);
}

void PDMessage::initExtended(ExtendedType type, uint8_t numObjects, SpecRevision rev) {
    if (numObjects > MAX_OBJECTS) {
        numObjects = MAX_OBJECTS;
    }
    memset(payload, 0, sizeof(payload));
    payloadLength = 2;
    crcValid = true;
    timestamp = 0;
    setHeader(makeHeader(numObjects, (uint8_t)type, rev, true, false, true));
    payloadLength = (uint8_t)(2 + 4 * numObjects);
}

uint16_t PDMessage::extendedHeader() const {
    if (!extended() || payloadLength < 4) {
        return 0;
    }
    return (uint16_t)payload[2] | ((uint16_t)payload[3] << 8);
}

void PDMessage::setExtendedHeader(uint16_t value) {
    if (!extended()) {
        return;
    }
    if (payloadLength < 4) {
        payloadLength = 4;
    }
    payload[2] = (uint8_t)(value & 0xFF);
    payload[3] = (uint8_t)(value >> 8);
}

uint16_t PDMessage::extendedDataSize() const {
    return (uint16_t)(extendedHeader() & 0x01FF);
}

const char* PDMessage::typeName() const {
    if (extended()) {
        switch ((ExtendedType)type()) {
            case ExtendedType::EprSourceCapabilities:    return "EPR_Source_Capabilities";
            case ExtendedType::Status:                   return "Status";
            case ExtendedType::GetBatteryCap:            return "Get_Battery_Cap";
            case ExtendedType::GetBatteryStatus:         return "Get_Battery_Status";
            case ExtendedType::BatteryCapabilities:      return "Battery_Capabilities";
            case ExtendedType::GetManufacturerInfo:      return "Get_Manufacturer_Info";
            case ExtendedType::ManufacturerInfo:         return "Manufacturer_Info";
            case ExtendedType::SecurityRequest:          return "Security_Request";
            case ExtendedType::SecurityResponse:         return "Security_Response";
            case ExtendedType::FirmwareUpdateRequest:    return "Firmware_Update_Request";
            case ExtendedType::FirmwareUpdateResponse:   return "Firmware_Update_Response";
            case ExtendedType::PpsStatus:                return "PPS_Status";
            case ExtendedType::CountryInfo:              return "Country_Info";
            case ExtendedType::CountryCodes:             return "Country_Codes";
            case ExtendedType::SinkCapabilitiesExtended: return "Sink_Capabilities_Extended";
            case ExtendedType::ExtendedControl:          return "Extended_Control";
            default:                                     return "Extended";
        }
    }

    if (objectCount() > 0) {
        switch ((DataType)type()) {
            case DataType::SourceCapabilities: return "Source_Capabilities";
            case DataType::Request:            return "Request";
            case DataType::Bist:               return "BIST";
            case DataType::SinkCapabilities:   return "Sink_Capabilities";
            case DataType::BatteryStatus:      return "Battery_Status";
            case DataType::Alert:              return "Alert";
            case DataType::GetCountryInfo:     return "Get_Country_Info";
            case DataType::EnterUsb:           return "Enter_USB";
            case DataType::EprRequest:         return "EPR_Request";
            case DataType::EprMode:            return "EPR_Mode";
            case DataType::SourceInfo:         return "Source_Info";
            case DataType::Revision:           return "Revision";
            case DataType::VendorDefined:      return "Vendor_Defined";
            default:                           return "Data";
        }
    }

    switch ((ControlType)type()) {
        case ControlType::GoodCRC:              return "GoodCRC";
        case ControlType::GotoMin:              return "GotoMin";
        case ControlType::Accept:               return "Accept";
        case ControlType::Reject:               return "Reject";
        case ControlType::Ping:                 return "Ping";
        case ControlType::PsReady:              return "PS_RDY";
        case ControlType::GetSourceCap:         return "Get_Source_Cap";
        case ControlType::GetSinkCap:           return "Get_Sink_Cap";
        case ControlType::DrSwap:               return "DR_Swap";
        case ControlType::PrSwap:               return "PR_Swap";
        case ControlType::VconnSwap:            return "VCONN_Swap";
        case ControlType::Wait:                 return "Wait";
        case ControlType::SoftReset:            return "Soft_Reset";
        case ControlType::DataReset:            return "Data_Reset";
        case ControlType::DataResetComplete:    return "Data_Reset_Complete";
        case ControlType::NotSupported:         return "Not_Supported";
        case ControlType::GetSourceCapExtended: return "Get_Source_Cap_Extended";
        case ControlType::GetStatus:            return "Get_Status";
        case ControlType::FrSwap:               return "FR_Swap";
        case ControlType::GetPpsStatus:         return "Get_PPS_Status";
        case ControlType::GetCountryCodes:      return "Get_Country_Codes";
        case ControlType::GetSinkCapExtended:   return "Get_Sink_Cap_Extended";
        default:                                return "Control";
    }
}

/* ------------------------------------------------------------------------- */
/* Source capabilities                                                       */
/* ------------------------------------------------------------------------- */

bool parseSourceCapabilities(const PDMessage& message, PDAdvertisements& out) {
    out.count = 0;
    if (message.sop != Sop::Partner || message.extended()) {
        return false;
    }
    if (!message.isData() || message.type() != (uint8_t)DataType::SourceCapabilities) {
        return false;
    }

    uint8_t count = message.objectCount();
    if (count > PDMessage::MAX_OBJECTS) {
        count = PDMessage::MAX_OBJECTS;
    }

    for (uint8_t i = 0; i < count; ++i) {
        uint32_t raw = message.object(i);
        PDSupply& p = out.pdo[i];
        p = PDSupply();
        p.raw = raw;
        p.position = (uint8_t)(i + 1);

        uint8_t kind = (uint8_t)((raw >> 30) & 0x03);
        if (kind == 0) {
            p.type = SupplyType::Fixed;
            p.maxVoltage_mV = (uint16_t)(((raw >> 10) & 0x3FF) * 50);
            p.minVoltage_mV = p.maxVoltage_mV;
            p.maxCurrent_mA = (uint16_t)((raw & 0x3FF) * 10);
            p.maxPower_mW = (uint32_t)p.maxVoltage_mV * p.maxCurrent_mA / 1000u;
            p.dualRolePower = (raw & (1u << 29)) != 0;
            p.usbSuspend = (raw & (1u << 28)) != 0;
            p.unconstrainedPower = (raw & (1u << 27)) != 0;
            p.usbCommunications = (raw & (1u << 26)) != 0;
            p.dualRoleData = (raw & (1u << 25)) != 0;
        } else if (kind == 1) {
            p.type = SupplyType::Battery;
            p.maxVoltage_mV = (uint16_t)(((raw >> 20) & 0x3FF) * 50);
            p.minVoltage_mV = (uint16_t)(((raw >> 10) & 0x3FF) * 50);
            p.maxPower_mW = (uint32_t)(raw & 0x3FF) * 250u;
        } else if (kind == 2) {
            p.type = SupplyType::Variable;
            p.maxVoltage_mV = (uint16_t)(((raw >> 20) & 0x3FF) * 50);
            p.minVoltage_mV = (uint16_t)(((raw >> 10) & 0x3FF) * 50);
            p.maxCurrent_mA = (uint16_t)((raw & 0x3FF) * 10);
            p.maxPower_mW = (uint32_t)p.maxVoltage_mV * p.maxCurrent_mA / 1000u;
        } else {
            uint32_t apdo = raw & 0xF0000000u;
            if (apdo == 0xC0000000u) {
                // SPR PPS APDO (Table 6.13)
                p.type = SupplyType::PPS;
                p.maxCurrent_mA = (uint16_t)((raw & 0x7F) * 50);
                p.minVoltage_mV = (uint16_t)(((raw >> 8) & 0xFF) * 100);
                p.maxVoltage_mV = (uint16_t)(((raw >> 17) & 0xFF) * 100);
                p.maxPower_mW = (uint32_t)p.maxVoltage_mV * p.maxCurrent_mA / 1000u;
            } else if (apdo == 0xE0000000u) {
                // SPR AVS APDO (Table 6.15): 9..15 V and 15..20 V current fields.
                p.type = SupplyType::AVS;
                uint16_t lowCurrent = (uint16_t)(((raw >> 10) & 0x3FF) * 10);
                uint16_t highCurrent = (uint16_t)((raw & 0x3FF) * 10);
                p.minVoltage_mV = 9000;
                p.maxVoltage_mV = (highCurrent != 0) ? 20000 : 15000;
                p.maxCurrent_mA = (highCurrent > lowCurrent) ? highCurrent : lowCurrent;
                p.maxPower_mW = (uint32_t)p.maxVoltage_mV * p.maxCurrent_mA / 1000u;
            } else {
                p.type = SupplyType::Unknown;
            }
        }
        ++out.count;
    }
    return out.count > 0;
}

/* ------------------------------------------------------------------------- */
/* Request data objects                                                      */
/* ------------------------------------------------------------------------- */

static uint32_t rdoFlags(const RequestOptions& options) {
    uint32_t rdo = 0;
    if (options.giveBack) {
        rdo |= 1u << 27;
    }
    if (options.capabilityMismatch) {
        rdo |= 1u << 26;
    }
    if (options.usbCommunications) {
        rdo |= 1u << 25;
    }
    if (options.noUsbSuspend) {
        rdo |= 1u << 24;
    }
    if (options.eprCapable) {
        rdo |= 1u << 23;
    }
    return rdo;
}

uint32_t buildFixedRequest(uint8_t position, uint16_t operatingCurrent_mA,
                           const RequestOptions& options) {
    uint32_t current10 = ((uint32_t)operatingCurrent_mA + 5u) / 10u;
    if (current10 > 0x3FFu) {
        current10 = 0x3FFu;
    }
    uint32_t rdo = ((uint32_t)position & 0x07u) << 28;
    rdo |= rdoFlags(options);
    rdo |= (current10 & 0x3FFu) << 10;
    rdo |= (current10 & 0x3FFu);
    return rdo;
}

uint32_t buildPPSRequest(uint8_t position, uint16_t voltage_mV, uint16_t current_mA,
                         const RequestOptions& options) {
    uint32_t voltage20 = ((uint32_t)voltage_mV + 10u) / 20u;
    if (voltage20 > 0x7FFu) {
        voltage20 = 0x7FFu;
    }
    uint32_t current50 = ((uint32_t)current_mA + 25u) / 50u;
    if (current50 > 0x7Fu) {
        current50 = 0x7Fu;
    }
    uint32_t rdo = ((uint32_t)position & 0x07u) << 28;
    rdo |= rdoFlags(options);
    rdo |= (voltage20 & 0x7FFu) << 9;
    rdo |= (current50 & 0x7Fu);
    return rdo;
}

uint32_t buildAVSRequest(uint8_t position, uint16_t voltage_mV, uint16_t current_mA,
                         const RequestOptions& options) {
    uint32_t voltage25 = ((uint32_t)voltage_mV + 12u) / 25u;
    if (voltage25 > 0x7FFu) {
        voltage25 = 0x7FFu;
    }
    uint32_t current50 = ((uint32_t)current_mA + 25u) / 50u;
    if (current50 > 0x7Fu) {
        current50 = 0x7Fu;
    }
    uint32_t rdo = ((uint32_t)position & 0x07u) << 28;
    rdo |= rdoFlags(options);
    rdo |= (voltage25 & 0x7FFu) << 9;
    rdo |= (current50 & 0x7Fu);
    return rdo;
}

/* ------------------------------------------------------------------------- */
/* Text output                                                               */
/* ------------------------------------------------------------------------- */

const char* ProtocolAnalyzer::sopName(Sop sop) {
    switch (sop) {
        case Sop::Partner:          return "SOP";
        case Sop::Cable:            return "SOP'";
        case Sop::CableDoublePrime: return "SOP''";
        case Sop::Debug:            return "SOP'D";
        case Sop::DebugDoublePrime: return "SOP''D";
        default:                    return "INV";
    }
}

const char* ProtocolAnalyzer::senderName(const PDMessage& message) {
    if (message.sop == Sop::Partner) {
        return message.portPowerRole() ? "Source" : "Sink";
    }
    if (message.sop == Sop::Cable || message.sop == Sop::CableDoublePrime ||
        message.sop == Sop::Debug || message.sop == Sop::DebugDoublePrime) {
        return message.portPowerRole() ? "Cable" : "Port";
    }
    return "";
}

const char* ProtocolAnalyzer::messageName(const PDMessage& message) {
    return message.typeName();
}

static void printHex(Print& out, uint32_t value, uint8_t digits) {
    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    char buffer[9];
    if (digits > 8) {
        digits = 8;
    }
    for (uint8_t i = 0; i < digits; ++i) {
        buffer[digits - 1 - i] = HEX_DIGITS[(value >> (4 * i)) & 0x0F];
    }
    buffer[digits] = '\0';
    out.print(buffer);
}

void ProtocolAnalyzer::printSupply(Print& out, const PDSupply& p) {
    switch (p.type) {
        case SupplyType::Fixed:
            out.print("Fixed ");
            out.print(p.maxVoltage_mV);
            out.print("mV ");
            out.print(p.maxCurrent_mA);
            out.print("mA");
            break;
        case SupplyType::Battery:
            out.print("Battery ");
            out.print(p.minVoltage_mV);
            out.print("-");
            out.print(p.maxVoltage_mV);
            out.print("mV ");
            out.print(p.maxPower_mW);
            out.print("mW");
            break;
        case SupplyType::Variable:
            out.print("Variable ");
            out.print(p.minVoltage_mV);
            out.print("-");
            out.print(p.maxVoltage_mV);
            out.print("mV ");
            out.print(p.maxCurrent_mA);
            out.print("mA");
            break;
        case SupplyType::PPS:
            out.print("PPS ");
            out.print(p.minVoltage_mV);
            out.print("-");
            out.print(p.maxVoltage_mV);
            out.print("mV ");
            out.print(p.maxCurrent_mA);
            out.print("mA");
            break;
        case SupplyType::AVS:
            out.print("AVS ");
            out.print(p.minVoltage_mV);
            out.print("-");
            out.print(p.maxVoltage_mV);
            out.print("mV ");
            out.print(p.maxCurrent_mA);
            out.print("mA");
            break;
        default:
            out.print("Unknown");
            break;
    }
}

void ProtocolAnalyzer::printSourceCapabilities(Print& out, const PDMessage& message) {
    PDAdvertisements ads;
    if (!parseSourceCapabilities(message, ads)) {
        out.println("  (not a Source_Capabilities message)");
        return;
    }
    for (uint8_t i = 0; i < ads.count; ++i) {
        out.print("  PDO");
        out.print(ads.pdo[i].position);
        out.print(": ");
        printSupply(out, ads.pdo[i]);
        out.print("  raw=");
        printHex(out, ads.pdo[i].raw, 8);
        out.println();
    }
}

static void printRequestDetails(Print& out, const PDMessage& message) {
    uint32_t rdo = message.object(0);
    uint8_t position = (uint8_t)((rdo >> 28) & 0x07);
    out.print("  RDO: pos=");
    out.print(position);
    out.print(" flags=0x");
    printHex(out, (rdo >> 23) & 0x1F, 2);
    out.print(" op=");
    out.print((unsigned long)((rdo >> 10) & 0x3FF) * 10ul);
    out.print("mA max=");
    out.print((unsigned long)(rdo & 0x3FF) * 10ul);
    out.print("mA");
    if (rdo & (1u << 27)) {
        out.print(" giveback");
    }
    out.println();
}

void ProtocolAnalyzer::printMessage(Print& out, const PDMessage& message, bool details) {
    out.print((unsigned long)message.timestamp);
    out.print("ms RX CC");
    out.print(message.cc);
    out.print(" ");
    out.print(sopName(message.sop));
    out.print(" ");
    out.print(senderName(message));
    out.print(" ");
    out.print(messageName(message));
    out.print(" id=");
    out.print(message.messageId());
    out.print(" hdr=0x");
    printHex(out, message.header(), 4);
    if (message.extended()) {
        out.print(" ext=0x");
        printHex(out, message.extendedHeader(), 4);
    }
    for (uint8_t i = 0; i < message.objectCount(); ++i) {
        out.print(" ");
        printHex(out, message.object(i), 8);
    }
    if (!message.crcValid) {
        out.print(" BAD-CRC");
    }
    out.println();

    if (!details) {
        return;
    }
    if (message.isData() && message.type() == (uint8_t)DataType::SourceCapabilities) {
        printSourceCapabilities(out, message);
    } else if (message.isData() && message.type() == (uint8_t)DataType::Request) {
        printRequestDetails(out, message);
    }
}

} // namespace fusb302
