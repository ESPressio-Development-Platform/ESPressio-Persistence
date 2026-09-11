#pragma once

#include <ESPressio_IStorageBackend.hpp>
#include <ESPressio_PolymorphicMemory.hpp>

namespace ESPressio::Persistence {

/// <summary>Provides sequential access to one already-open file without reopening the underlying backend for every chunk.</summary>

class IFileReadStream {
public:
    virtual ~IFileReadStream() = default;

    /// <summary>Returns the total size of the opened file in bytes.</summary>
    virtual uint64_t Size() const noexcept = 0;

    /// <summary>Returns the current sequential read position in bytes.</summary>
    virtual uint64_t Position() const noexcept = 0;

    /// <summary>Reads up to <paramref name="capacity"/> bytes from the current position and advances the stream.</summary>
    /// <param name="buffer">Destination buffer; may be null only when capacity is zero.</param>
    /// <param name="capacity">Maximum number of bytes to read.</param>
    /// <param name="bytesRead">Receives the number of bytes copied into the destination.</param>
    virtual StorageStatus Read(
        uint8_t* buffer,
        std::size_t capacity,
        std::size_t& bytesRead
    ) = 0;
};

/// <summary>Policy-aware owning pointer for an implementation-specific sequential file stream.</summary>
using FileReadStreamPtr = System::Memory::PolymorphicUniquePtr<IFileReadStream>;

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

    /// <summary>Opens a file once for efficient sequential reads when the backend supports <c>StorageCapability::SequentialRead</c>.</summary>
    /// <param name="path">Backend-native path to the file.</param>
    /// <param name="stream">Receives ownership of the opened stream on success.</param>
    /// <returns><c>NotSupported</c> by default; capable backends return the status of opening the file.</returns>
    virtual StorageStatus OpenRead(
        const char* path,
        FileReadStreamPtr& stream
    ) const {
        (void)path;
        stream.reset();
        return StorageStatus::NotSupported;
    }

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
    /// <summary>Atomically publishes a complete prepared file over a target, including an existing target.</summary>
    /// <remarks>Success guarantees atomic namespace visibility. Crash recovery before the subsequent directory
    /// sync must yield either the old complete target or the prepared complete target, never a missing/torn mix.
    /// A generic Rename implementation does not establish this guarantee. Failure may have published and is ambiguous.</remarks>
    virtual StorageStatus ReplaceFileAtomically(const char* prepared,const char* target) {
        (void)prepared; (void)target; return StorageStatus::NotSupported;
    }
    /// <summary>Commits all file data/length metadata to durable media before namespace publication.</summary>
    virtual StorageStatus SyncFile(const char* path) { (void)path; return StorageStatus::NotSupported; }
    /// <summary>Commits namespace changes for a directory to durable media.</summary>
    virtual StorageStatus SyncDirectory(const char* path) { (void)path; return StorageStatus::NotSupported; }
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
