// USB PD sink example: reads Source_Capabilities and requests a voltage.
//
// This is a minimal policy: it prefers a fixed 9 V object, then a PPS object
// that can reach 9 V. Adapt the target and current to your hardware.
#include <FUSB302.h>

static const int PIN_SDA = 7;
static const int PIN_SCL = 6;

static const uint16_t TARGET_MV = 9000;
static const uint16_t TARGET_MA = 2000;

FUSB302 pd;
bool requested = false;

static void handleSourceCapabilities(const fusb302::PDMessage& message) {
    fusb302::PDAdvertisements ads;
    if (!fusb302::parseSourceCapabilities(message, ads)) {
        return;
    }

    Serial.println("Source capabilities:");
    for (uint8_t i = 0; i < ads.count; ++i) {
        Serial.print("  PDO");
        Serial.print(ads.pdo[i].position);
        Serial.print(": ");
        fusb302::ProtocolAnalyzer::printSupply(Serial, ads.pdo[i]);
        Serial.println();
    }

    if (requested) {
        return;
    }

    for (uint8_t i = 0; i < ads.count; ++i) {
        const fusb302::PDSupply& pdo = ads.pdo[i];
        if (pdo.type == fusb302::SupplyType::Fixed &&
            pdo.maxVoltage_mV == TARGET_MV &&
            pdo.maxCurrent_mA >= 500) {
            uint16_t current = pdo.maxCurrent_mA < TARGET_MA ? pdo.maxCurrent_mA : TARGET_MA;
            Serial.print("Requesting fixed ");
            Serial.print(TARGET_MV);
            Serial.print(" mV / ");
            Serial.print(current);
            Serial.println(" mA");
            requested = pd.requestFixed(pdo.position, current);
            return;
        }
    }

    for (uint8_t i = 0; i < ads.count; ++i) {
        const fusb302::PDSupply& pdo = ads.pdo[i];
        if (pdo.type == fusb302::SupplyType::PPS &&
            TARGET_MV >= pdo.minVoltage_mV && TARGET_MV <= pdo.maxVoltage_mV &&
            pdo.maxCurrent_mA >= 500) {
            uint16_t current = pdo.maxCurrent_mA < TARGET_MA ? pdo.maxCurrent_mA : TARGET_MA;
            Serial.print("Requesting PPS ");
            Serial.print(TARGET_MV);
            Serial.print(" mV / ");
            Serial.print(current);
            Serial.println(" mA");
            requested = pd.requestPPS(pdo.position, TARGET_MV, current);
            return;
        }
    }

    Serial.println("No matching fixed or PPS capability");
}

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
    Serial.println("PD sink ready");
}

void loop() {
    pd.poll();

    fusb302::PDMessage message;
    while (pd.readMessage(message)) {
        fusb302::ProtocolAnalyzer::printMessage(Serial, message, false);

        if (message.sop != fusb302::Sop::Partner) {
            continue;
        }
        if (message.isData() &&
            message.type() == (uint8_t)fusb302::DataType::SourceCapabilities) {
            handleSourceCapabilities(message);
        } else if (message.isControl() &&
                   message.type() == (uint8_t)fusb302::ControlType::Reject) {
            Serial.println("Request rejected");
            requested = false;
        }
    }

    if (!pd.isConnected()) {
        requested = false;
    }
}
