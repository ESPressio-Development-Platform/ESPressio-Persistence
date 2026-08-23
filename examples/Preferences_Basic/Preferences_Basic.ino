#include <Arduino.h>
#include <ESPressio_ESP32Persistence.hpp>

using namespace ESPressio::Persistence;
PreferencesStorage settings("demo");

void setup() {
    Serial.begin(115200);
    if (settings.Initialize() != StorageStatus::Success) return;

    const uint32_t bootMarker = 0x45535052;
    settings.Write(
        "marker",
        reinterpret_cast<const uint8_t*>(&bootMarker),
        sizeof(bootMarker)
    );

    uint32_t loaded = 0;
    std::size_t bytesRead = 0;
    if (settings.Read(
        "marker",
        reinterpret_cast<uint8_t*>(&loaded),
        sizeof(loaded),
        bytesRead
    ) == StorageStatus::Success) {
        Serial.printf("marker=0x%08lX\n", static_cast<unsigned long>(loaded));
    }
}

void loop() {}
