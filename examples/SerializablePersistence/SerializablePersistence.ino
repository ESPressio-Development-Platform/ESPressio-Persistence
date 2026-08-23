#include <Arduino.h>
#include <ESPressio_ESP32Persistence.hpp>
#include <ESPressio_Persistence_Serializable.hpp>

using namespace ESPressio;
using namespace ESPressio::Persistence;

class DeviceConfiguration final
    : public Serializable::Serializable<DeviceConfiguration> {
    ESPRESSIO_SERIALIZABLE_TYPE(DeviceConfiguration)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)

private:
    uint32_t _sampleRate = 48000;
    std::string _deviceName = "camera-a";
    bool _enabled = true;

public:
    ESPRESSIO_SERIALIZABLE_PROPERTIES(
        ESPRESSIO_PROPERTY("sampleRate", _sampleRate),
        ESPRESSIO_PROPERTY("deviceName", _deviceName),
        ESPRESSIO_PROPERTY("enabled", _enabled)
    )

    uint32_t GetSampleRate() const { return _sampleRate; }
    const std::string& GetDeviceName() const { return _deviceName; }
    bool IsEnabled() const { return _enabled; }
};

LittleFSStorage files(false);
PreferencesStorage values("typed-demo");

void setup() {
    Serial.begin(115200);

    if (
        files.Initialize() != StorageStatus::Success ||
        values.Initialize() != StorageStatus::Success
    ) {
        Serial.println("Persistence initialization failed");
        return;
    }

    DeviceConfiguration configuration;

    auto result = SaveSerializable(
        files,
        "/device-config.espb",
        configuration
    );
    Serial.printf(
        "LittleFS save: %s (%u bytes)\n",
        SerializablePersistenceStatusName(result.Status),
        static_cast<unsigned>(result.PayloadBytes)
    );

    DeviceConfiguration fromFile;
    result = LoadSerializable(
        files,
        "/device-config.espb",
        fromFile
    );
    Serial.printf(
        "LittleFS load: %s device=%s rate=%lu enabled=%s\n",
        SerializablePersistenceStatusName(result.Status),
        fromFile.GetDeviceName().c_str(),
        static_cast<unsigned long>(fromFile.GetSampleRate()),
        fromFile.IsEnabled() ? "true" : "false"
    );

    // The same Serializable type and API work with Preferences/NVS.
    result = SaveSerializable(values, "device", configuration);
    Serial.printf(
        "NVS save: %s (%u bytes)\n",
        SerializablePersistenceStatusName(result.Status),
        static_cast<unsigned>(result.PayloadBytes)
    );

    DeviceConfiguration fromNvs;
    result = LoadSerializable(values, "device", fromNvs);
    Serial.printf(
        "NVS load: %s device=%s rate=%lu enabled=%s\n",
        SerializablePersistenceStatusName(result.Status),
        fromNvs.GetDeviceName().c_str(),
        static_cast<unsigned long>(fromNvs.GetSampleRate()),
        fromNvs.IsEnabled() ? "true" : "false"
    );
}

void loop() {}
