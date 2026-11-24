/**
 * Cable e-Marker (SOP' Discover Identity) decoding and emulation.
 *
 * References: USB PD 3.2 V1.2 tables 6.33..6.44 and the decode rules used by
 * kws-x1-standalone-rs (pd-protocol/src/pd_cable.rs).
 *
 * Licensed under the MIT License. See LICENSE.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "FUSB302_Message.h"

namespace fusb302 {

enum class CableKind : uint8_t {
    Passive = 0,
    Active = 1
};

enum class CableLayout : uint8_t {
    PD2 = 0,
    PassiveV10,
    ActiveV13,
    Unknown
};

enum class CableResult : uint8_t {
    Ok = 0,
    Nak,
    Busy,
    InvalidHeader,
    UnsupportedVersion,
    NotCable,
    Length
};

/// Decoded SOP' Discover Identity response.
struct CableIdentity {
    bool valid = false;
    bool nak = false;
    bool busy = false;

    CableKind kind = CableKind::Passive;
    CableLayout layout = CableLayout::Unknown;

    uint16_t vid = 0;
    uint16_t pid = 0;
    uint16_t bcdDevice = 0;
    uint32_t xid = 0;

    uint16_t maxCurrent_mA = 0;
    uint16_t maxVoltage_mV = 0;
    bool currentAssumed = false;   ///< Passive v1.0 reserved current code -> 3 A
    bool eprCapable = false;
    bool sop2Present = false;
    bool vbusThroughCable = false;
    uint8_t speedCode = 0;
    uint8_t cableVdoVersion = 0;

    uint8_t svdmVersionMajor = 0;
    uint8_t svdmVersionMinor = 0;
    uint8_t revision = 0;          ///< 1 = PD2, 2 = PD3

    uint32_t raw[PDMessage::MAX_OBJECTS] = {0};
    uint8_t count = 0;
};

/// Data used to emulate a passive/active cable plug.
struct CableEmulationConfig {
    CableKind kind = CableKind::Passive;
    uint16_t vid = 0xFFFF;         ///< Unassigned VID (USB PD section 6.1.5)
    uint16_t pid = 0;
    uint16_t bcdDevice = 0;
    uint32_t xid = 0;

    uint16_t maxCurrent_mA = 5000; ///< 3000 or 5000
    uint16_t maxVoltage_mV = 20000;///< 20000 or 50000
    bool eprCapable = false;
    bool sop2Present = false;
    bool vbusThroughCable = false;
    uint8_t speedCode = 0;
    uint8_t latencyCode = 1;       ///< 1 = < 10 ns
    uint8_t connectorType = 2;     ///< Bits 19:18, 2 = USB Type-C receptacle/plug
    uint8_t cableVdoVersion = 0;   ///< 0 = passive v1.0, 3 = active v1.3
    uint32_t cableVdo2 = 0;        ///< Optional second Cable VDO (active v1.3)
    bool includeCableVdo2 = false;
};

/// Decodes a SOP' Discover_VDO response. `objects` includes the VDM header
/// (objects[0]) followed by ID Header, Cert Stat, Product and Cable VDO(s).
CableResult decodeCableIdentity(const uint32_t* objects, uint8_t count,
                                uint8_t revision, CableIdentity& out);

/// Extracts the data objects of a Vendor_Defined SOP' message.
bool findCableIdentityObjects(const PDMessage& message, uint32_t* objects,
                              uint8_t& count);

/// Builds the response objects after the VDM header. Returns object count
/// (4 normally, 5 when a second active Cable VDO is requested).
uint8_t buildCableIdentityObjects(const CableEmulationConfig& config,
                                  uint32_t* objects, uint8_t maxObjects);

/// Prints a human readable e-Marker summary.
void printCableIdentity(Print& out, const CableIdentity& identity);

} // namespace fusb302
