// Passive USB PD protocol analyzer.
//
// The FUSB302 only listens (no Rd, no GoodCRC, no transmission). Connect its
// CC1 or CC2 to the CC line being observed and share ground with the link.
// Select the CC with ANALYZER_CC below.
#include <FUSB302.h>

static const int PIN_SDA = 7;
static const int PIN_SCL = 6;
static const uint8_t ANALYZER_CC = 1;

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

    if (!pd.beginMonitor(ANALYZER_CC)) {
        Serial.print("monitor setup failed: ");
        Serial.println(pd.lastErrorString());
        while (true) {
            delay(1000);
        }
    }

    Serial.print("PD analyzer listening on CC");
    Serial.println(ANALYZER_CC);
}

void loop() {
    pd.poll();

    fusb302::PDMessage message;
    while (pd.readMessage(message)) {
        fusb302::ProtocolAnalyzer::printMessage(Serial, message, true);
    }

    if (pd.hardResetReceived()) {
        Serial.println("--- Hard Reset ---");
        pd.clearResetFlags();
    }
}
