#pragma once

#include <ESPressio_IFileStorage.hpp>
#include <ESPressio_Memory.hpp>

namespace ESPressio::Persistence {

/// <summary>Provides failure-resilient file replacement using temporary and backup renames on an <c>IFileStorage</c> backend.</summary>
/// <remarks>The underlying backend must be ready and support the <c>Rename</c> capability. Transient derived paths prefer external memory so atomic file operations do not consume scarce internal DRAM.</remarks>
class AtomicFileStore final {
public:
    /// <summary>Creates an atomic replacement helper over the supplied file-storage backend.</summary>
    explicit AtomicFileStore(IFileStorage& storage) : _storage(storage) {}

    /// <summary>Replaces a target file through temporary-file write, backup rename, and final rename operations.</summary>
    /// <returns>The first storage failure encountered, or <c>StorageStatus::Success</c> when replacement completes.</returns>
    StorageStatus Replace(
        const char* path,
        const uint8_t* data,
        std::size_t size
    ) {
        if (path == nullptr || *path == '\0' || (data == nullptr && size != 0)) {
            return StorageStatus::InvalidArgument;
        }
        if (!_storage.IsReady()) {
            return StorageStatus::NotInitialized;
        }
        if (!HasCapability(_storage.GetCapabilities(), StorageCapability::Rename)) {
            return StorageStatus::NotSupported;
        }

        // The caller already owns the canonical target path. Avoid duplicating
        // it solely to append suffixes, and place the two genuinely required
        // derived paths in external-preferred storage.
        using PathString = System::Memory::String<
            System::Memory::MemoryPolicy::ExternalPreferred
        >;
        PathString temporary(path);
        temporary += ".tmp";
        PathString backup(path);
        backup += ".bak";

        (void)_storage.Remove(temporary.c_str());
        (void)_storage.Remove(backup.c_str());

        StorageStatus status = _storage.Write(
            temporary.c_str(), data, size, WriteMode::Replace
        );
        if (status != StorageStatus::Success) {
            return status;
        }

        bool targetExists = false;
        status = _storage.Exists(path, targetExists);
        if (status != StorageStatus::Success) {
            (void)_storage.Remove(temporary.c_str());
            return status;
        }

        if (targetExists) {
            status = _storage.Rename(path, backup.c_str());
            if (status != StorageStatus::Success) {
                (void)_storage.Remove(temporary.c_str());
                return status;
            }
        }

        status = _storage.Rename(temporary.c_str(), path);
        if (status != StorageStatus::Success) {
            if (targetExists) {
                (void)_storage.Rename(backup.c_str(), path);
            }
            (void)_storage.Remove(temporary.c_str());
            return status;
        }

        if (targetExists) {
            (void)_storage.Remove(backup.c_str());
        }
        return StorageStatus::Success;
    }

private:
    IFileStorage& _storage;
};

} // namespace ESPressio::Persistence
