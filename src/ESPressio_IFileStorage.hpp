#pragma once

#include <ESPressio_IStorageBackend.hpp>

namespace ESPressio::Persistence {

class IFileStorage : public IStorageBackend {
public:
    ~IFileStorage() override = default;

    virtual StorageStatus Exists(const char* path, bool& exists) const = 0;
    virtual StorageStatus Stat(const char* path, StorageEntry& entry) const = 0;
    virtual StorageStatus Read(
        const char* path,
        uint64_t offset,
        uint8_t* buffer,
        std::size_t capacity,
        std::size_t& bytesRead
    ) const = 0;
    virtual StorageStatus Write(
        const char* path,
        const uint8_t* data,
        std::size_t size,
        WriteMode mode = WriteMode::Replace
    ) = 0;
    virtual StorageStatus Remove(const char* path) = 0;
    virtual StorageStatus Rename(const char* from, const char* to) = 0;
    virtual StorageStatus CreateDirectory(const char* path) = 0;
    virtual StorageStatus RemoveDirectory(const char* path) = 0;
    virtual StorageStatus List(
        const char* path,
        StorageListCallback callback,
        void* context = nullptr
    ) const = 0;
};

} // namespace ESPressio::Persistence
