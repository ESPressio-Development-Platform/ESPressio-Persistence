#include <Arduino.h>
#include <ESPressio_ESP32Persistence.hpp>

using namespace ESPressio::Persistence;
SDMMCStorage storage(true); // one-bit mode is often easier to wire on prototypes

void setup() {
    Serial.begin(115200);
    Serial.printf("SD_MMC mount: %s\n", StorageStatusName(storage.Initialize()));
}

void loop() {}
