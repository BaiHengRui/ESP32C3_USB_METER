/**
 * Cable e-Marker decoding and emulation.
 *
 * Licensed under the MIT License. See LICENSE.
 */
#include "FUSB302_Cable.h"

#include <string.h>

namespace fusb302 {

enum {
    USB_IF_SVID = 0xFF00,
    STRUCTURED_VDM = 0x8000,
    VDM_COMMAND_MASK = 0x1F,
    DISCOVER_IDENTITY = 1
};

enum {
    PRODUCT_TYPE_PASSIVE_CABLE = 3,
    PRODUCT_TYPE_ACTIVE_CABLE = 4
};

static bool decodeSvdmVersion(uint32_t header, uint8_t& major, uint8_t& minor) {
    major = (uint8_t)((header >> 13) & 0x03);
    minor = (uint8_t)((header >> 11) & 0x03);
    if (major == 0) {
        // SVDM 1.0 has a clear version field.
        return true;
    }
    return major == 1; // 2.0 / 2.1 (major = 1)
}

CableResult decodeCableIdentity(const uint32_t* objects, uint8_t count,
                                uint8_t revision, CableIdentity& out) {
    out = CableIdentity();

    if (count == 0 || count > PDMessage::MAX_OBJECTS) {
        return CableResult::Length;
    }

    out.count = count;
    out.revision = revision;
    for (uint8_t i = 0; i < count; ++i) {
        out.raw[i] = objects[i];
    }

    uint32_t header = objects[0];

    // USB-IF SVID, Structured VDM, Discover Identity command.
    if ((header >> 16) != USB_IF_SVID ||
        (header & STRUCTURED_VDM) == 0 ||
        (header & VDM_COMMAND_MASK) != DISCOVER_IDENTITY) {
        return CableResult::InvalidHeader;
    }

    uint8_t svdmMajor = 0;
    uint8_t svdmMinor = 0;
    if (!decodeSvdmVersion(header, svdmMajor, svdmMinor)) {
        return CableResult::UnsupportedVersion;
    }
    out.svdmVersionMajor = svdmMajor;
    out.svdmVersionMinor = svdmMinor;

    if (revision == 1) {
        if (svdmMajor != 0) {
            return CableResult::UnsupportedVersion;
        }
    } else if (revision != 2) {
        return CableResult::UnsupportedVersion;
    }

    uint8_t commandType = (uint8_t)((header >> 6) & 0x03);
    if (commandType == 2 || commandType == 3) {
        if (count != 1) {
            return CableResult::Length;
        }
        out.nak = (commandType == 2);
        out.busy = (commandType == 3);
        out.valid = true;
        return (commandType == 2) ? CableResult::Nak : CableResult::Busy;
    }
    if (commandType != 1) {
        return CableResult::InvalidHeader;
    }

    if (count < 5) {
        return CableResult::Length;
    }

    uint8_t productType = (uint8_t)((objects[1] >> 27) & 0x07);
    if (productType == PRODUCT_TYPE_PASSIVE_CABLE) {
        out.kind = CableKind::Passive;
    } else if (productType == PRODUCT_TYPE_ACTIVE_CABLE) {
        out.kind = CableKind::Active;
    } else {
        return CableResult::NotCable;
    }

    out.vid = (uint16_t)(objects[1] & 0xFFFF);
    out.xid = objects[2];
    out.pid = (uint16_t)(objects[3] >> 16);
    out.bcdDevice = (uint16_t)(objects[3] & 0xFFFF);

    uint32_t cable = objects[4];
    uint8_t version = (uint8_t)((cable >> 21) & 0x07);
    out.cableVdoVersion = version;
    out.speedCode = (uint8_t)(cable & 0x07);
    out.vbusThroughCable = (cable & (1u << 4)) != 0;

    bool modern = false;
    if (revision == 1 && svdmMajor == 0) {
        out.layout = CableLayout::PD2;
    } else if (revision == 2 && svdmMajor == 1 &&
               out.kind == CableKind::Passive && version == 0) {
        out.layout = CableLayout::PassiveV10;
        modern = true;
    } else if (revision == 2 && svdmMajor == 1 &&
               out.kind == CableKind::Active && version == 3) {
        out.layout = CableLayout::ActiveV13;
        modern = true;
    } else {
        out.layout = CableLayout::Unknown;
    }

    if (out.layout == CableLayout::ActiveV13 && count < 6) {
        return CableResult::Length;
    }

    // Current handling. Passive v1.0 reserved codes 0/3 default to 3 A.
    if (out.layout != CableLayout::Unknown &&
        (out.kind == CableKind::Passive || out.vbusThroughCable)) {
        uint8_t code = (uint8_t)((cable >> 5) & 0x03);
        if (code == 1) {
            out.maxCurrent_mA = 3000;
        } else if (code == 2) {
            out.maxCurrent_mA = 5000;
        } else if (out.layout == CableLayout::PassiveV10) {
            out.maxCurrent_mA = 3000;
            out.currentAssumed = true;
        }
    }

    if (modern) {
        // Deprecated 30 V / 40 V encodings are received as 20 V.
        uint8_t voltageCode = (uint8_t)((cable >> 9) & 0x03);
        out.maxVoltage_mV = (voltageCode == 3) ? 50000 : 20000;
        out.eprCapable = (cable & (1u << 17)) != 0;
    }

    if (out.layout == CableLayout::PD2 || out.layout == CableLayout::ActiveV13) {
        out.sop2Present = (cable & (1u << 3)) != 0;
    }

    out.valid = true;
    return CableResult::Ok;
}

bool findCableIdentityObjects(const PDMessage& message, uint32_t* objects,
                              uint8_t& count) {
    count = 0;
    if (message.sop != Sop::Cable && message.sop != Sop::CableDoublePrime) {
        return false;
    }
    if (message.extended() || !message.isData() ||
        message.type() != (uint8_t)DataType::VendorDefined) {
        return false;
    }
    uint8_t n = message.objectCount();
    if (n == 0 || n > PDMessage::MAX_OBJECTS) {
        return false;
    }
    for (uint8_t i = 0; i < n; ++i) {
        objects[i] = message.object(i);
    }
    count = n;
    return true;
}

uint8_t buildCableIdentityObjects(const CableEmulationConfig& config,
                                  uint32_t* objects, uint8_t maxObjects) {
    uint8_t needed = config.includeCableVdo2 ? 5 : 4;
    if (maxObjects < needed) {
        needed = maxObjects;
    }
    if (needed < 4) {
        return 0;
    }

    uint8_t productType = (config.kind == CableKind::Active)
                              ? PRODUCT_TYPE_ACTIVE_CABLE
                              : PRODUCT_TYPE_PASSIVE_CABLE;

    // ID Header VDO: product type, USB Type-C plug, VID.
    objects[0] = ((uint32_t)productType << 27) |
                 (3ul << 21) |                 // USB Type-C plug
                 (uint32_t)(config.vid & 0xFFFF);

    // Cert Stat VDO: XID.
    objects[1] = config.xid;

    // Product VDO: PID and bcdDevice.
    objects[2] = ((uint32_t)config.pid << 16) |
                 (uint32_t)(config.bcdDevice & 0xFFFF);

    uint8_t version = config.cableVdoVersion;
    if (config.kind == CableKind::Active && version == 0) {
        version = 3;
    }

    uint8_t voltageCode = (config.maxVoltage_mV >= 50000) ? 3 : 0;
    uint8_t currentCode = 1;
    if (config.maxCurrent_mA >= 5000) {
        currentCode = 2;
    } else if (config.maxCurrent_mA < 3000) {
        currentCode = 1; // minimum legal cable is 3 A
    }

    uint32_t cable = ((uint32_t)(version & 0x07) << 21) |
                     ((uint32_t)(config.connectorType & 0x03) << 18) |
                     ((uint32_t)(config.latencyCode & 0x0F) << 13) |
                     ((uint32_t)(voltageCode & 0x03) << 9) |
                     ((uint32_t)(currentCode & 0x03) << 5) |
                     (uint32_t)(config.speedCode & 0x07);
    if (config.eprCapable) {
        cable |= 1ul << 17;
    }
    if (config.vbusThroughCable) {
        cable |= 1ul << 4;
    }
    if (config.sop2Present) {
        cable |= 1ul << 3;
    }
    objects[3] = cable;

    if (needed >= 5) {
        objects[4] = config.cableVdo2;
    }
    return needed;
}

void printCableIdentity(Print& out, const CableIdentity& id) {
    out.print("e-Marker: ");
    if (!id.valid) {
        out.println("invalid");
        return;
    }
    if (id.nak) {
        out.println("NAK");
        return;
    }
    if (id.busy) {
        out.println("BUSY");
        return;
    }

    out.print(id.kind == CableKind::Active ? "active" : "passive");
    out.print(" ");
    switch (id.layout) {
        case CableLayout::PD2:        out.print("PD2"); break;
        case CableLayout::PassiveV10: out.print("passive-v1.0"); break;
        case CableLayout::ActiveV13:  out.print("active-v1.3"); break;
        default:                      out.print("unknown-layout"); break;
    }
    out.println();

    out.print("  VID=0x");
    out.print(id.vid, HEX);
    out.print(" PID=0x");
    out.print(id.pid, HEX);
    out.print(" bcdDevice=0x");
    out.print(id.bcdDevice, HEX);
    out.print(" XID=0x");
    out.println(id.xid, HEX);

    out.print("  max ");
    out.print(id.maxVoltage_mV);
    out.print(" mV / ");
    out.print(id.maxCurrent_mA);
    out.print(" mA, EPR=");
    out.print(id.eprCapable ? "yes" : "no");
    out.print(", speed=");
    out.print(id.speedCode);
    out.print(", SVDM=");
    out.print(id.svdmVersionMajor);
    out.print(".");
    out.print(id.svdmVersionMinor);
    out.print(", SOP'' controller=");
    out.println(id.sop2Present ? "yes" : "no");

    out.print("  raw:");
    for (uint8_t i = 0; i < id.count; ++i) {
        out.print(" ");
        out.print(id.raw[i], HEX);
    }
    out.println();
}

} // namespace fusb302
