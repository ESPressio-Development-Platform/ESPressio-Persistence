#include <Arduino.h>
#include <array>
#include <ESPressio_ESP32Persistence.hpp>
#include <ESPressio_Persistence_Serializable_Security.hpp>
#include <ESPressio_Security.hpp>

using namespace ESPressio;
using namespace ESPressio::Persistence;

class DeviceConfiguration final : public Serializable::Serializable<DeviceConfiguration> {
    ESPRESSIO_SERIALIZABLE_TYPE(DeviceConfiguration)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
private:
    std::string _ssid = "ESPressio-Lab";
    uint8_t _channel = 6;
public:
    ESPRESSIO_SERIALIZABLE_PROPERTIES(
        ESPRESSIO_PROPERTY("ssid", _ssid),
        ESPRESSIO_PROPERTY("channel", _channel)
    )
    const std::string& SSID() const { return _ssid; }
};

LittleFSStorage files(false);
PreferencesStorage values("demo");
Security::AES256GCMCipher cipher;
Security::AeadCipherRegistry ciphers;
Security::StaticKeyProvider keys;
Security::ESP32RandomSource randomSource;

constexpr std::array<uint8_t,32> ApplicationKey = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
};

void setup() {
    Serial.begin(115200);

    ciphers.Register(cipher);
    keys.Add(1, Security::AeadAlgorithm::AES256GCM, ApplicationKey);
    Security::DataProtector protector(ciphers, keys, randomSource);
    Serializable::SerializationProtectionConfig protection(
        protector,
        "Example.DeviceConfiguration"
    );

    files.Initialize();
    values.Initialize();

    DeviceConfiguration source;

    auto fileSaved = SaveSerializable(files, "/device.esdp", source, protection);
    DeviceConfiguration fromFile;
    auto fileLoaded = LoadSerializable(files, "/device.esdp", fromFile, protection);

    auto valueSaved = SaveSerializable(values, "device", source, protection);
    DeviceConfiguration fromValue;
    auto valueLoaded = LoadSerializable(values, "device", fromValue, protection);

    Serial.printf("file save/load=%s/%s NVS save/load=%s/%s restored=%s\n",
        fileSaved ? "OK" : "FAIL",
        fileLoaded ? "OK" : "FAIL",
        valueSaved ? "OK" : "FAIL",
        valueLoaded ? "OK" : "FAIL",
        fromFile.SSID().c_str());
}

void loop() {}
