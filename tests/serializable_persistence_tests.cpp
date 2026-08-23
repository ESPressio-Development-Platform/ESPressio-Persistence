#include <ESPressio_Persistence.hpp>
#include <ESPressio_Persistence_Serializable.hpp>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

using namespace ESPressio;
using namespace ESPressio::Persistence;

class DeviceConfiguration final
    : public Serializable::Serializable<DeviceConfiguration> {
    ESPRESSIO_SERIALIZABLE_TYPE(DeviceConfiguration)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(2)

private:
    uint32_t _sampleRate = 1000;
    std::string _name = "default";
    bool _enabled = false;

public:
    ESPRESSIO_SERIALIZABLE_PROPERTIES(
        ESPRESSIO_PROPERTY("sampleRate", _sampleRate),
        ESPRESSIO_PROPERTY("name", _name),
        ESPRESSIO_PROPERTY("enabled", _enabled)
    )

    void Set(uint32_t sampleRate, std::string name, bool enabled) {
        _sampleRate = sampleRate;
        _name = std::move(name);
        _enabled = enabled;
    }

    uint32_t SampleRate() const { return _sampleRate; }
    const std::string& Name() const { return _name; }
    bool Enabled() const { return _enabled; }
};

static void AssertConfiguration(
    const DeviceConfiguration& value,
    uint32_t sampleRate,
    const char* name,
    bool enabled
) {
    assert(value.SampleRate() == sampleRate);
    assert(value.Name() == name);
    assert(value.Enabled() == enabled);
}

static void TestFileRoundTripAndAtomicCleanup() {
    MemoryFileStorage storage;
    assert(storage.Initialize() == StorageStatus::Success);

    DeviceConfiguration source;
    source.Set(48000, "camera-a", true);

    const auto saved = SaveSerializable(storage, "/device.bin", source);
    assert(saved.Success());
    assert(saved.PayloadBytes > 5);

    bool exists = false;
    assert(storage.Exists("/device.bin", exists) == StorageStatus::Success && exists);
    assert(storage.Exists("/device.bin.tmp", exists) == StorageStatus::Success && !exists);
    assert(storage.Exists("/device.bin.bak", exists) == StorageStatus::Success && !exists);

    DeviceConfiguration restored;
    restored.Set(1, "wrong", false);
    const auto loaded = LoadSerializable(storage, "/device.bin", restored);
    assert(loaded.Success());
    assert(loaded.PayloadBytes == saved.PayloadBytes);
    AssertConfiguration(restored, 48000, "camera-a", true);
}

static void TestKeyValueRoundTrip() {
    MemoryKeyValueStorage storage;
    assert(storage.Initialize() == StorageStatus::Success);

    DeviceConfiguration source;
    source.Set(96000, "recorder", true);

    const auto saved = SaveSerializable(storage, "device", source);
    assert(saved.Success());

    DeviceConfiguration restored;
    const auto loaded = LoadSerializable(storage, "device", restored);
    assert(loaded.Success());
    AssertConfiguration(restored, 96000, "recorder", true);
}

static void TestMissingValuesPreserveStorageStatus() {
    MemoryFileStorage files;
    MemoryKeyValueStorage values;
    assert(files.Initialize() == StorageStatus::Success);
    assert(values.Initialize() == StorageStatus::Success);

    DeviceConfiguration object;

    const auto file = LoadSerializable(files, "/missing.bin", object);
    assert(!file.Success());
    assert(file.Status == SerializablePersistenceStatus::StorageError);
    assert(file.Storage == StorageStatus::NotFound);

    const auto key = LoadSerializable(values, "missing", object);
    assert(!key.Success());
    assert(key.Status == SerializablePersistenceStatus::StorageError);
    assert(key.Storage == StorageStatus::NotFound);
}

static void TestMalformedPayloadDetection() {
    MemoryFileStorage storage;
    assert(storage.Initialize() == StorageStatus::Success);

    const uint8_t invalid[] = {0x01, 0x02, 0x03, 0x04};
    assert(
        storage.Write("/bad.bin", invalid, sizeof(invalid), WriteMode::Replace) ==
        StorageStatus::Success
    );

    DeviceConfiguration object;
    const auto result = LoadSerializable(storage, "/bad.bin", object);
    assert(!result.Success());
    assert(result.Status == SerializablePersistenceStatus::MalformedPayload);
}

static void TestPayloadLimitOnSaveAndLoad() {
    MemoryFileStorage storage;
    assert(storage.Initialize() == StorageStatus::Success);

    DeviceConfiguration source;
    source.Set(44100, std::string(200, 'x'), true);

    SerializablePersistenceOptions tiny;
    tiny.MaximumPayloadBytes = 32;
    const auto saveResult = SaveSerializable(storage, "/large.bin", source, tiny);
    assert(!saveResult.Success());
    assert(saveResult.Status == SerializablePersistenceStatus::PayloadTooLarge);

    SerializablePersistenceOptions normal;
    normal.MaximumPayloadBytes = 4096;
    assert(SaveSerializable(storage, "/large.bin", source, normal).Success());

    DeviceConfiguration restored;
    const auto loadResult = LoadSerializable(storage, "/large.bin", restored, tiny);
    assert(!loadResult.Success());
    assert(loadResult.Status == SerializablePersistenceStatus::PayloadTooLarge);
}

static void TestInvalidLocatorAndInitialization() {
    MemoryFileStorage storage;
    DeviceConfiguration object;

    auto result = SaveSerializable(storage, "/value.bin", object);
    assert(result.Status == SerializablePersistenceStatus::StorageError);
    assert(result.Storage == StorageStatus::NotInitialized);

    assert(storage.Initialize() == StorageStatus::Success);
    result = SaveSerializable(storage, "", object);
    assert(result.Status == SerializablePersistenceStatus::InvalidArgument);
}

static void TestOrdinaryReplaceFallback() {
    class NonRenamingStorage final : public IFileStorage {
    public:
        StorageStatus Initialize() override { return _inner.Initialize(); }
        void Shutdown() override { _inner.Shutdown(); }
        bool IsReady() const override { return _inner.IsReady(); }
        const char* GetBackendName() const override { return "NonRenamingStorage"; }
        StorageCapability GetCapabilities() const override {
            return StorageCapability::Hierarchical;
        }
        StorageStatistics GetStatistics() const override { return _inner.GetStatistics(); }
        StorageStatus Exists(const char* p, bool& e) const override { return _inner.Exists(p, e); }
        StorageStatus Stat(const char* p, StorageEntry& e) const override { return _inner.Stat(p, e); }
        StorageStatus Read(const char* p, uint64_t o, uint8_t* b, std::size_t c, std::size_t& r) const override { return _inner.Read(p, o, b, c, r); }
        StorageStatus Write(const char* p, const uint8_t* d, std::size_t s, WriteMode m) override { return _inner.Write(p, d, s, m); }
        StorageStatus Remove(const char* p) override { return _inner.Remove(p); }
        StorageStatus Rename(const char*, const char*) override { return StorageStatus::NotSupported; }
        StorageStatus CreateDirectory(const char* p) override { return _inner.CreateDirectory(p); }
        StorageStatus RemoveDirectory(const char* p) override { return _inner.RemoveDirectory(p); }
        StorageStatus List(const char* p, StorageListCallback c, void* x) const override { return _inner.List(p, c, x); }
    private:
        MemoryFileStorage _inner;
    } storage;

    assert(storage.Initialize() == StorageStatus::Success);
    DeviceConfiguration source;
    source.Set(22050, "fallback", true);
    assert(SaveSerializable(storage, "/fallback.bin", source).Success());

    DeviceConfiguration restored;
    assert(LoadSerializable(storage, "/fallback.bin", restored).Success());
    AssertConfiguration(restored, 22050, "fallback", true);
}

int main() {
    TestFileRoundTripAndAtomicCleanup();
    TestKeyValueRoundTrip();
    TestMissingValuesPreserveStorageStatus();
    TestMalformedPayloadDetection();
    TestPayloadLimitOnSaveAndLoad();
    TestInvalidLocatorAndInitialization();
    TestOrdinaryReplaceFallback();
    return 0;
}
