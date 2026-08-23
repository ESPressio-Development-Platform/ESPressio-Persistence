#pragma once

#include <ESPressio_IKeyValueStorage.hpp>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace ESPressio::Persistence {

class MemoryKeyValueStorage final : public IKeyValueStorage {
public:
    StorageStatus Initialize() override {
        if (_ready) return StorageStatus::AlreadyInitialized;
        _ready = true;
        return StorageStatus::Success;
    }
    void Shutdown() override { _ready = false; }
    bool IsReady() const override { return _ready; }
    const char* GetBackendName() const override { return "MemoryKeyValueStorage"; }
    StorageCapability GetCapabilities() const override {
        return StorageCapability::KeyValue | StorageCapability::AtomicReplace;
    }
    StorageStatistics GetStatistics() const override { return {}; }

    StorageStatus Contains(const char* key, bool& exists) const override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(key)) return StorageStatus::InvalidArgument;
        exists = _values.count(key) != 0;
        return StorageStatus::Success;
    }

    StorageStatus GetSize(const char* key, std::size_t& size) const override {
        size = 0;
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(key)) return StorageStatus::InvalidArgument;
        const auto it = _values.find(key);
        if (it == _values.end()) return StorageStatus::NotFound;
        size = it->second.size();
        return StorageStatus::Success;
    }

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

    StorageStatus Write(const char* key, const uint8_t* data, std::size_t size) override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(key) || (data == nullptr && size != 0)) return StorageStatus::InvalidArgument;
        auto& value = _values[key];
        value.assign(data, data + size);
        return StorageStatus::Success;
    }

    StorageStatus Remove(const char* key) override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(key)) return StorageStatus::InvalidArgument;
        return _values.erase(key) != 0 ? StorageStatus::Success : StorageStatus::NotFound;
    }

    StorageStatus Clear() override {
        if (!_ready) return StorageStatus::NotInitialized;
        _values.clear();
        return StorageStatus::Success;
    }

private:
    static bool Valid(const char* value) { return value != nullptr && *value != '\0'; }
    bool _ready = false;
    std::map<std::string, std::vector<uint8_t>> _values;
};

} // namespace ESPressio::Persistence
