#pragma once

#include <ESPressio_IFileStorage.hpp>
#include <string>

namespace ESPressio::Persistence {

class AtomicFileStore final {
public:
    explicit AtomicFileStore(IFileStorage& storage) : _storage(storage) {}

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

        const std::string target(path);
        const std::string temporary = target + ".tmp";
        const std::string backup = target + ".bak";

        (void)_storage.Remove(temporary.c_str());
        (void)_storage.Remove(backup.c_str());

        StorageStatus status = _storage.Write(
            temporary.c_str(), data, size, WriteMode::Replace
        );
        if (status != StorageStatus::Success) {
            return status;
        }

        bool targetExists = false;
        status = _storage.Exists(target.c_str(), targetExists);
        if (status != StorageStatus::Success) {
            (void)_storage.Remove(temporary.c_str());
            return status;
        }

        if (targetExists) {
            status = _storage.Rename(target.c_str(), backup.c_str());
            if (status != StorageStatus::Success) {
                (void)_storage.Remove(temporary.c_str());
                return status;
            }
        }

        status = _storage.Rename(temporary.c_str(), target.c_str());
        if (status != StorageStatus::Success) {
            if (targetExists) {
                (void)_storage.Rename(backup.c_str(), target.c_str());
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
