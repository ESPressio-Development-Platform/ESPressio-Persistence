#include <Arduino.h>
#include <ESPressio_ESP32Persistence.hpp>

using namespace ESPressio::Persistence;
LittleFSStorage storage(false);

void setup() {
    Serial.begin(115200);
    if (storage.Initialize() != StorageStatus::Success) {
        Serial.println("LittleFS mount failed");
        return;
    }

    const char message[] = "persistent hello";
    AtomicFileStore atomic(storage);
    const auto status = atomic.Replace(
        "/settings.txt",
        reinterpret_cast<const uint8_t*>(message),
        sizeof(message) - 1
    );
    Serial.printf("write: %s\n", StorageStatusName(status));

    uint8_t buffer[32] = {};
    std::size_t bytesRead = 0;
    if (storage.Read("/settings.txt", 0, buffer, sizeof(buffer) - 1, bytesRead) == StorageStatus::Success) {
        buffer[bytesRead] = 0;
        Serial.printf("read: %s\n", reinterpret_cast<char*>(buffer));
    }
}

void loop() {}
