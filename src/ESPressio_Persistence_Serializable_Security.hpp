#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "ESPressio_Persistence_Serializable.hpp"
#include <ESPressio_Serializable_Security.hpp>

namespace ESPressio::Persistence {

struct ProtectedSerializablePersistenceResult {
    StorageStatus Storage = StorageStatus::Success;
    std::size_t PayloadBytes = 0;
    Serializable::ProtectedSerializationResult Serialization{};

    bool Success() const {
        return Storage == StorageStatus::Success && Serialization.Success();
    }

    explicit operator bool() const { return Success(); }
};

namespace Detail {

inline ProtectedSerializablePersistenceResult MakeProtectedStorageFailure(
    StorageStatus status
) {
    ProtectedSerializablePersistenceResult result;
    result.Storage = status;
    return result;
}

} // namespace Detail

template<typename TObject>
ProtectedSerializablePersistenceResult SaveSerializable(
    IFileStorage& storage,
    const char* path,
    const TObject& object,
    const Serializable::SerializationProtectionConfig& protection,
    bool preferAtomicFileReplace = true
) {
    if (!Detail::ValidLocator(path)) {
        return Detail::MakeProtectedStorageFailure(StorageStatus::InvalidArgument);
    }
    if (!storage.IsReady()) {
        return Detail::MakeProtectedStorageFailure(StorageStatus::NotInitialized);
    }

    ProtectedSerializablePersistenceResult result;
    Serializable::SerializationBuffer<uint8_t> bytes;
    result.Serialization = Serializable::SerializeProtectedBinary(
        object,
        bytes,
        protection
    );
    result.PayloadBytes = bytes.size();
    if (!result.Serialization) return result;

    if (
        preferAtomicFileReplace &&
        HasCapability(storage.GetCapabilities(), StorageCapability::Rename)
    ) {
        AtomicFileStore atomic(storage);
        result.Storage = atomic.Replace(path, bytes.data(), bytes.size());
    } else {
        result.Storage = storage.Write(
            path,
            bytes.data(),
            bytes.size(),
            WriteMode::Replace
        );
    }
    return result;
}

template<typename TObject>
ProtectedSerializablePersistenceResult LoadSerializable(
    IFileStorage& storage,
    const char* path,
    TObject& object,
    const Serializable::SerializationProtectionConfig& protection
) {
    if (!Detail::ValidLocator(path)) {
        return Detail::MakeProtectedStorageFailure(StorageStatus::InvalidArgument);
    }
    if (!storage.IsReady()) {
        return Detail::MakeProtectedStorageFailure(StorageStatus::NotInitialized);
    }

    StorageEntry entry{};
    StorageStatus status = storage.Stat(path, entry);
    if (status != StorageStatus::Success) return Detail::MakeProtectedStorageFailure(status);
    if (entry.isDirectory) return Detail::MakeProtectedStorageFailure(StorageStatus::InvalidArgument);
    if (entry.size > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
        ProtectedSerializablePersistenceResult result;
        result.Storage = StorageStatus::CorruptData;
        return result;
    }

    Serializable::SerializationBuffer<uint8_t> bytes(static_cast<std::size_t>(entry.size));
    std::size_t bytesRead = 0;
    status = storage.Read(path, 0, bytes.data(), bytes.size(), bytesRead);
    if (status != StorageStatus::Success) return Detail::MakeProtectedStorageFailure(status);
    if (bytesRead != bytes.size()) return Detail::MakeProtectedStorageFailure(StorageStatus::CorruptData);

    ProtectedSerializablePersistenceResult result;
    result.PayloadBytes = bytes.size();
    result.Serialization = Serializable::DeserializeProtectedBinary(
        bytes.data(),
        bytes.size(),
        object,
        protection
    );
    return result;
}

template<typename TObject>
ProtectedSerializablePersistenceResult SaveSerializable(
    IKeyValueStorage& storage,
    const char* key,
    const TObject& object,
    const Serializable::SerializationProtectionConfig& protection
) {
    if (!Detail::ValidLocator(key)) {
        return Detail::MakeProtectedStorageFailure(StorageStatus::InvalidArgument);
    }
    if (!storage.IsReady()) {
        return Detail::MakeProtectedStorageFailure(StorageStatus::NotInitialized);
    }

    ProtectedSerializablePersistenceResult result;
    Serializable::SerializationBuffer<uint8_t> bytes;
    result.Serialization = Serializable::SerializeProtectedBinary(
        object,
        bytes,
        protection
    );
    result.PayloadBytes = bytes.size();
    if (!result.Serialization) return result;

    result.Storage = storage.Write(key, bytes.data(), bytes.size());
    return result;
}

template<typename TObject>
ProtectedSerializablePersistenceResult LoadSerializable(
    IKeyValueStorage& storage,
    const char* key,
    TObject& object,
    const Serializable::SerializationProtectionConfig& protection
) {
    if (!Detail::ValidLocator(key)) {
        return Detail::MakeProtectedStorageFailure(StorageStatus::InvalidArgument);
    }
    if (!storage.IsReady()) {
        return Detail::MakeProtectedStorageFailure(StorageStatus::NotInitialized);
    }

    std::size_t size = 0;
    StorageStatus status = storage.GetSize(key, size);
    if (status != StorageStatus::Success) return Detail::MakeProtectedStorageFailure(status);

    Serializable::SerializationBuffer<uint8_t> bytes(size);
    std::size_t bytesRead = 0;
    status = storage.Read(key, bytes.data(), bytes.size(), bytesRead);
    if (status != StorageStatus::Success) return Detail::MakeProtectedStorageFailure(status);
    if (bytesRead != bytes.size()) return Detail::MakeProtectedStorageFailure(StorageStatus::CorruptData);

    ProtectedSerializablePersistenceResult result;
    result.PayloadBytes = bytes.size();
    result.Serialization = Serializable::DeserializeProtectedBinary(
        bytes.data(),
        bytes.size(),
        object,
        protection
    );
    return result;
}

} // namespace ESPressio::Persistence
