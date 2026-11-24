// Detect USB-C attach, CC orientation and the source's Rp current capability.
// The meter's FUSB302 is configured as a sink (Rd on both CCs).
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

    Serial.print("FUSB302 device ID: 0x");
    Serial.println(pd.deviceId(), HEX);

    if (!pd.beginSink()) {
        Serial.print("sink setup failed: ");
        Serial.println(pd.lastErrorString());
    }
    Serial.println("CC detector ready");
}

void loop() {
    uint8_t cc = 0;
    bool attached = pd.detectOrientation(cc);

    if (attached) {
        Serial.print("attached on CC");
        Serial.print(cc);
        Serial.print(", Rp = ");
        Serial.print(pd.sourceCurrent_mA());
        Serial.print(" mA, VBUS = ");
        Serial.println(pd.isConnected() ? "present" : "absent");
    } else {
        Serial.print("no source (");
        Serial.print(pd.lastErrorString());
        Serial.println(")");
    }

    delay(1000);
}
