// Read and decode the cable e-Marker (SOP' Discover Identity).
//
// Hardware note: the e-Marker is powered through VCONN on the CC line that is
// not used for PD traffic. The board must provide/pass 5 V VCONN to the
// FUSB302 VCONN pin; this library only closes the internal VCONN switch.
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
    Serial.println("e-Marker reader ready");
}

void loop() {
    uint8_t cc = 0;
    if (!pd.detectOrientation(cc)) {
        Serial.println("no source attached");
        delay(2000);
        return;
    }

    fusb302::CableIdentity identity;
    if (pd.readCableIdentity(identity, 300)) {
        Serial.print("CC");
        Serial.print(cc);
        Serial.print(": ");
        fusb302::printCableIdentity(Serial, identity);
    } else {
        Serial.print("e-Marker read failed: ");
        Serial.print(pd.lastErrorString());
        Serial.println(" (cable may be passive/absent or VCONN unavailable)");
    }

    delay(5000);
}
