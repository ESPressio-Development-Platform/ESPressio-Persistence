#pragma once

#include <ESPressio_IKeyValueStorage.hpp>
#include <ESPressio_Memory.hpp>
#include <cstring>
#include <functional>

namespace ESPressio::Persistence {

/// <summary>In-memory <c>IKeyValueStorage</c> implementation intended for volatile storage, host use, and tests.</summary>
/// <remarks>Owned keys, values, and associative-container nodes use ESPressio System ExternalPreferred storage so the backend does not compete with capability-constrained internal RAM on PSRAM-capable targets.</remarks>
class MemoryKeyValueStorage final : public IKeyValueStorage {
private:
    static constexpr auto ExternalPreferred =
        System::Memory::MemoryPolicy::ExternalPreferred;
    using StorageString = System::Memory::String<ExternalPreferred>;
    using StorageBytes = System::Memory::ByteVector<ExternalPreferred>;
    using ValueStorage = System::Memory::Map<
        StorageString,
        StorageBytes,
        ExternalPreferred,
        std::less<>
    >;

public:
    /// <inheritdoc/>
    StorageStatus Initialize() override {
        if (_ready) return StorageStatus::AlreadyInitialized;
        _ready = true;
        return StorageStatus::Success;
    }
    /// <inheritdoc/>
    void Shutdown() override { _ready = false; }
    /// <inheritdoc/>
    bool IsReady() const override { return _ready; }
    /// <inheritdoc/>
    const char* GetBackendName() const override { return "MemoryKeyValueStorage"; }
    /// <inheritdoc/>
    StorageCapability GetCapabilities() const override {
        return StorageCapability::KeyValue | StorageCapability::AtomicReplace;
    }
    /// <inheritdoc/>
    StorageStatistics GetStatistics() const override { return {}; }

    /// <inheritdoc/>
    StorageStatus Contains(const char* key, bool& exists) const override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(key)) return StorageStatus::InvalidArgument;
        exists = _values.find(key) != _values.end();
        return StorageStatus::Success;
    }

    /// <inheritdoc/>
    StorageStatus GetSize(const char* key, std::size_t& size) const override {
        size = 0;
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(key)) return StorageStatus::InvalidArgument;
        const auto it = _values.find(key);
        if (it == _values.end()) return StorageStatus::NotFound;
        size = it->second.size();
        return StorageStatus::Success;
    }

    /// <inheritdoc/>
    StorageStatus Read(
        const char* key,
        uint8_t* buffer,
        std::size_t capacity,
        std::size_t& bytesRead
    ) const override {
        bytesRead = 0;
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(key) || (buffer == nullptr && capacity != 0)) {
            return StorageStatus::InvalidArgument;
        }
        const auto it = _values.find(key);
        if (it == _values.end()) return StorageStatus::NotFound;
        if (capacity < it->second.size()) return StorageStatus::NoSpace;
        bytesRead = it->second.size();
        if (bytesRead != 0) std::memcpy(buffer, it->second.data(), bytesRead);
        return StorageStatus::Success;
    }

    /// <inheritdoc/>
    StorageStatus Write(const char* key, const uint8_t* data, std::size_t size) override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(key) || (data == nullptr && size != 0)) return StorageStatus::InvalidArgument;

        auto it = _values.find(key);
        if (it == _values.end()) {
            try {
                it = _values.emplace(StorageString(key), StorageBytes{}).first;
            } catch (...) {
                return StorageStatus::NoSpace;
            }
        }

        try {
            if (size == 0) {
                it->second.clear();
            } else {
                it->second.assign(data, data + size);
            }
        } catch (...) {
            return StorageStatus::NoSpace;
        }
        return StorageStatus::Success;
    }

    /// <inheritdoc/>
    StorageStatus Remove(const char* key) override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(key)) return StorageStatus::InvalidArgument;
        const auto it = _values.find(key);
        if (it == _values.end()) return StorageStatus::NotFound;
        _values.erase(it);
        return StorageStatus::Success;
    }

    /// <inheritdoc/>
    StorageStatus Clear() override {
        if (!_ready) return StorageStatus::NotInitialized;
        _values.clear();
        return StorageStatus::Success;
    }

private:
    static bool Valid(const char* value) { return value != nullptr && *value != '\0'; }
    bool _ready = false;
    ValueStorage _values;
};

} // namespace ESPressio::Persistence
