#include <ESPressio_Persistence.hpp>
#include <ESPressio_Persistence_Serializable_Security.hpp>

#include <cassert>
#include <cstdint>
#include <string>

using namespace ESPressio;
using namespace ESPressio::Persistence;

class ProtectedConfiguration final : public Serializable::Serializable<ProtectedConfiguration> {
    ESPRESSIO_SERIALIZABLE_TYPE(ProtectedConfiguration)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
private:
    std::string _ssid;
    std::string _password;
    uint8_t _channel = 1;
public:
    ProtectedConfiguration() = default;
    ProtectedConfiguration(std::string ssid, std::string password, uint8_t channel)
        : _ssid(std::move(ssid)), _password(std::move(password)), _channel(channel) {}
    const std::string& SSID() const { return _ssid; }
    const std::string& Password() const { return _password; }
    uint8_t Channel() const { return _channel; }
    ESPRESSIO_SERIALIZABLE_PROPERTIES(
        ESPRESSIO_PROPERTY("ssid", _ssid),
        ESPRESSIO_PROPERTY("password", _password),
        ESPRESSIO_PROPERTY("channel", _channel)
    )
};

class PurposeProtector final : public Security::IDataProtector {
public:
    Security::SecurityResult Protect(
        const uint8_t* data,
        std::size_t size,
        Security::SecurityBuffer& out,
        const Security::DataProtectionContext& context = {}
    ) override {
        if (data == nullptr && size != 0) {
            return Security::SecurityResult::Fail(Security::SecurityError::InvalidArgument, "bad");
        }
        out.clear();
        out.push_back(Context(context));
        for (std::size_t i = 0; i < size; ++i) {
            out.push_back(static_cast<uint8_t>(data[i] ^ 0x5Au));
        }
        return Security::SecurityResult::Ok(true);
    }

    Security::SecurityResult Unprotect(
        const uint8_t* data,
        std::size_t size,
        Security::SecurityBuffer& out,
        const Security::DataProtectionContext& context = {}
    ) override {
        out.clear();
        if (data == nullptr || size == 0 || data[0] != Context(context)) {
            return Security::SecurityResult::Fail(
                Security::SecurityError::AuthenticationFailed,
                "context/auth failure"
            );
        }
        for (std::size_t i = 1; i < size; ++i) {
            out.push_back(static_cast<uint8_t>(data[i] ^ 0x5Au));
        }
        return Security::SecurityResult::Ok(true);
    }

private:
    static uint8_t Context(const Security::DataProtectionContext& c) {
        uint8_t v = 0;
        for (std::size_t i = 0; i < c.Size; ++i) {
            v = static_cast<uint8_t>((v * 31u) ^ c.Data[i]);
        }
        return v;
    }
};

static void AssertValue(const ProtectedConfiguration& value) {
    assert(value.SSID() == "ESPressio-Lab");
    assert(value.Password() == "secret");
    assert(value.Channel() == 6);
}

int main() {
    PurposeProtector protector;
    Serializable::SerializationProtectionConfig protection(protector, "ESPressio.WiFi.Configuration");
    ProtectedConfiguration source("ESPressio-Lab", "secret", 6);

    MemoryFileStorage files; assert(files.Initialize() == StorageStatus::Success);
    auto fileSave = SaveSerializable(files, "/wifi.bin", source, protection);
    assert(fileSave.Success());
    bool exists=false; assert(files.Exists("/wifi.bin",exists)==StorageStatus::Success && exists);
    assert(files.Exists("/wifi.bin.tmp",exists)==StorageStatus::Success && !exists);
    assert(files.Exists("/wifi.bin.bak",exists)==StorageStatus::Success && !exists);
    ProtectedConfiguration fromFile;
    auto fileLoad = LoadSerializable(files, "/wifi.bin", fromFile, protection);
    assert(fileLoad.Success()); AssertValue(fromFile);

    MemoryKeyValueStorage values; assert(values.Initialize() == StorageStatus::Success);
    auto kvSave = SaveSerializable(values, "wifi", source, protection);
    assert(kvSave.Success());
    ProtectedConfiguration fromValue;
    auto kvLoad = LoadSerializable(values, "wifi", fromValue, protection);
    assert(kvLoad.Success()); AssertValue(fromValue);

    Serializable::SerializationProtectionConfig wrong(protector, "Wrong.Context");
    ProtectedConfiguration rejected;
    auto rejectedLoad = LoadSerializable(files, "/wifi.bin", rejected, wrong);
    assert(!rejectedLoad.Success());
    assert(rejectedLoad.Storage == StorageStatus::Success);
    assert(rejectedLoad.Serialization.Status == Serializable::ProtectedSerializationStatus::UnprotectionFailed);
    assert(rejectedLoad.Serialization.SecurityResult.Error == Security::SecurityError::AuthenticationFailed);

    assert(SaveSerializable(files, "/plain.bin", source).Success());
    ProtectedConfiguration plain;
    assert(LoadSerializable(files, "/plain.bin", plain).Success());
    AssertValue(plain);

    return 0;
}
