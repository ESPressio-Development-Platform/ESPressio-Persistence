#pragma once

#include <ESPressio_IStorageBackend.hpp>

namespace ESPressio::Persistence {

class IKeyValueStorage : public IStorageBackend {
public:
    ~IKeyValueStorage() override = default;

    virtual StorageStatus Contains(const char* key, bool& exists) const = 0;
    virtual StorageStatus GetSize(const char* key, std::size_t& size) const = 0;
    virtual StorageStatus Read(
        const char* key,
        uint8_t* buffer,
        std::size_t capacity,
        std::size_t& bytesRead
    ) const = 0;
    virtual StorageStatus Write(
        const char* key,
        const uint8_t* data,
        std::size_t size
    ) = 0;
    virtual StorageStatus Remove(const char* key) = 0;
    virtual StorageStatus Clear() = 0;
};

} // namespace ESPressio::Persistence
