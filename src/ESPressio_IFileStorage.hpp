#pragma once

#include <ESPressio_IStorageBackend.hpp>

namespace ESPressio::Persistence {

/// <summary>Abstract filesystem-like persistence backend supporting files, directories, metadata, and enumeration.</summary>
class IFileStorage : public IStorageBackend {
public:
    ~IFileStorage() override = default;

    /// <summary>Determines whether a storage entry exists at a path.</summary>
    virtual StorageStatus Exists(const char* path, bool& exists) const = 0;
    /// <summary>Reads metadata for a storage entry.</summary>
    virtual StorageStatus Stat(const char* path, StorageEntry& entry) const = 0;
    /// <summary>Reads bytes from a file starting at the supplied offset.</summary>
    /// <param name="bytesRead">Receives the number of bytes copied to the output buffer.</param>
    virtual StorageStatus Read(
        const char* path,
        uint64_t offset,
        uint8_t* buffer,
        std::size_t capacity,
        std::size_t& bytesRead
    ) const = 0;
    /// <summary>Writes bytes to a file using the requested replacement or append mode.</summary>
    virtual StorageStatus Write(
        const char* path,
        const uint8_t* data,
        std::size_t size,
        WriteMode mode = WriteMode::Replace
    ) = 0;
    /// <summary>Removes a file.</summary>
    virtual StorageStatus Remove(const char* path) = 0;
    /// <summary>Renames or moves a storage entry within the backend namespace.</summary>
    virtual StorageStatus Rename(const char* from, const char* to) = 0;
    /// <summary>Creates a directory.</summary>
    virtual StorageStatus CreateDirectory(const char* path) = 0;
    /// <summary>Removes a directory.</summary>
    virtual StorageStatus RemoveDirectory(const char* path) = 0;
    /// <summary>Enumerates entries beneath a path through the supplied callback.</summary>
    virtual StorageStatus List(
        const char* path,
        StorageListCallback callback,
        void* context = nullptr
    ) const = 0;
};

} // namespace ESPressio::Persistence
