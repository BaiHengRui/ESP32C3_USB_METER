// Emulate a USB-C cable e-Marker.
//
// The chip answers SOP' Discover Identity requests from the Source as a
// passive 5 A / 20 V cable. This is the same trick used by inline USB-C
// meters to make a charger offer its 5 A PDOs.
//
// Electrical safety: only enable this if the board is actually wired as the
// cable/plug for the Source (VCONN and CC routing reviewed). Do not use it on
// a port that is also acting as a normal sink without understanding the
// resulting role advertisement.
#include <FUSB302.h>

static const int PIN_SDA = 7;
static const int PIN_SCL = 6;

FUSB302 pd;

void setup() {
    Serial.begin(115200);
    delay(500);

    if (!pd.begin(PIN_SDA, PIN_SCL)) {
        Serial.print("FUSB302 init failed: ");
        Serial.println(pd.lastErrorString());
        while (true) {
            delay(1000);
        }
    }
    if (!pd.beginSink()) {
        Serial.print("sink setup failed: ");
        Serial.println(pd.lastErrorString());
    }

    fusb302::CableEmulationConfig cable;
    cable.kind = fusb302::CableKind::Passive;
    cable.vid = 0xFFFF;          // unassigned USB-IF VID
    cable.pid = 0x0000;
    cable.bcdDevice = 0x0000;
    cable.xid = 0;
    cable.maxCurrent_mA = 5000;  // advertise 5 A
    cable.maxVoltage_mV = 20000;
    cable.eprCapable = false;
    cable.speedCode = 0;         // USB 2.0 only
    pd.setCableEmulation(cable);
    pd.enableCableEmulation(true);

    Serial.println("e-Marker emulation enabled (passive 5 A / 20 V)");
}

void loop() {
    pd.poll();

    fusb302::PDMessage message;
    while (pd.readMessage(message)) {
        fusb302::ProtocolAnalyzer::printMessage(Serial, message, false);
    }
}
