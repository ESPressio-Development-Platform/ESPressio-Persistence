#include <Arduino.h>
#include <ESPressio_ESP32Persistence.hpp>

using namespace ESPressio::Persistence;

// Change this pin to match the CS connection on your external SD module.
SDStorage storage(5);

void setup() {
    Serial.begin(115200);
    const StorageStatus status = storage.Initialize();
    Serial.printf("SD mount: %s\n", StorageStatusName(status));
    if (status != StorageStatus::Success) return;

    const uint8_t sample[] = {1, 2, 3, 4};
    storage.Write("/sample.bin", sample, sizeof(sample));

    const StorageStatistics stats = storage.GetStatistics();
    if (stats.capacityKnown) {
        Serial.printf("SD total=%llu free=%llu\n",
            static_cast<unsigned long long>(stats.totalBytes),
            static_cast<unsigned long long>(stats.freeBytes));
    }
}

void loop() {}
