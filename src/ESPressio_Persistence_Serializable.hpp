#pragma once

#include <ESPressio_IFileStorage.hpp>
#include <ESPressio_IKeyValueStorage.hpp>
#include <ESPressio_AtomicFileStore.hpp>
#include <ESPressio_Serializable_Binary.hpp>
#include <ESPressio_DirectBinaryArchive.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace ESPressio::Persistence {

/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum class SerializablePersistenceStatus : uint8_t {
    Success = 0,
    InvalidArgument,
    StorageError,
    SerializationFailed,
    PayloadTooLarge,
    MalformedPayload,
    DeserializationFailed
};

inline const char* SerializablePersistenceStatusName(SerializablePersistenceStatus status) {
    switch (status) {
        case SerializablePersistenceStatus::Success: return "Success";
        case SerializablePersistenceStatus::InvalidArgument: return "InvalidArgument";
        case SerializablePersistenceStatus::StorageError: return "StorageError";
        case SerializablePersistenceStatus::SerializationFailed: return "SerializationFailed";
        case SerializablePersistenceStatus::PayloadTooLarge: return "PayloadTooLarge";
        case SerializablePersistenceStatus::MalformedPayload: return "MalformedPayload";
        case SerializablePersistenceStatus::DeserializationFailed: return "DeserializationFailed";
        default: return "Unknown";
    }
}

/**
 * ESPressio Memory Audit
 * Members:
 * - MaximumPayloadBytes (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - PreferAtomicFileReplace (bool): 1 bytes [0 bytes dynamic allocation]
 * - DecodeLimits (Serializable::BinaryArchiveDecodeLimits): 24 bytes [0 bytes dynamic allocation]
 * - Deserialization (Serializable::DeserializationOptions): 8 bytes [0 bytes dynamic allocation]
 * Total Memory: 40 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct SerializablePersistenceOptions {
    std::size_t MaximumPayloadBytes = 64u * 1024u;
    bool PreferAtomicFileReplace = true;
    Serializable::BinaryArchiveDecodeLimits DecodeLimits{};
    Serializable::DeserializationOptions Deserialization{};
};

/**
 * ESPressio Memory Audit
 * Members:
 * - Status (SerializablePersistenceStatus): 1 bytes [0 bytes dynamic allocation]
 * - Storage (StorageStatus): 1 bytes [0 bytes dynamic allocation]
 * - PayloadBytes (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - Deserialization (Serializable::DeserializationResult): 12 bytes [_issues: Capacity * (52 bytes) element storage; _issues: N live elements each: Path: Capacity + 1 bytes when capacity exceeds 15-byte SSO; _issues: N live elements each: Message: Capacity + 1 bytes when capacity exceeds 15-byte SSO]
 * Total Memory: 20 bytes [Deserialization: _issues: Capacity * (52 bytes) element storage; Deserialization: _issues: N live elements each: Path: Capacity + 1 bytes when capacity exceeds 15-byte SSO; Deserialization: _issues: N live elements each: Message: Capacity + 1 bytes when capacity exceeds 15-byte SSO]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
struct SerializablePersistenceResult {
    SerializablePersistenceStatus Status = SerializablePersistenceStatus::Success;
    StorageStatus Storage = StorageStatus::Success;
    std::size_t PayloadBytes = 0;
    Serializable::DeserializationResult Deserialization{};

    bool Success() const { return Status == SerializablePersistenceStatus::Success; }
    explicit operator bool() const { return Success(); }
};

namespace Detail {

using SerializableBytes = Serializable::SerializationBuffer<uint8_t>;

inline SerializablePersistenceResult MakeStorageFailure(StorageStatus status) {
    SerializablePersistenceResult result;
    result.Status = SerializablePersistenceStatus::StorageError;
    result.Storage = status;
    return result;
}

inline SerializablePersistenceResult MakeInvalidArgument() {
    SerializablePersistenceResult result;
    result.Status = SerializablePersistenceStatus::InvalidArgument;
    result.Storage = StorageStatus::InvalidArgument;
    return result;
}

template<typename TObject>
SerializablePersistenceResult EncodeSerializable(
    const TObject& object,
    const SerializablePersistenceOptions& options,
    SerializableBytes& bytes
) {
    SerializablePersistenceResult result;
    try {
        // DirectBinary emits the same ESPB v2 wire format as BinaryArchive but
        // writes properties straight into the caller's external-preferred byte
        // buffer. This avoids constructing an intermediate SerializationNode
        // tree solely to encode it immediately afterwards.
        if (!Serializable::SerializeDirectBinary(object, bytes)) {
            result.Status = SerializablePersistenceStatus::SerializationFailed;
            return result;
        }
    } catch (...) {
        result.Status = SerializablePersistenceStatus::SerializationFailed;
        return result;
    }

    result.PayloadBytes = bytes.size();
    if (bytes.size() > options.MaximumPayloadBytes) {
        result.Status = SerializablePersistenceStatus::PayloadTooLarge;
    }
    return result;
}

template<typename TObject>
SerializablePersistenceResult DecodeSerializable(
    const uint8_t* data,
    std::size_t size,
    TObject& object,
    const SerializablePersistenceOptions& options
) {
    SerializablePersistenceResult result;
    result.PayloadBytes = size;

    if (size > options.MaximumPayloadBytes) {
        result.Status = SerializablePersistenceStatus::PayloadTooLarge;
        return result;
    }

    // Keep the bounded tree decoder for reads until DirectBinary exposes the
    // same BinaryArchiveDecodeLimits contract. The direct writer is wire
    // compatible, so files written above remain readable here without format
    // or version changes.
    Serializable::BinaryArchive archive;
    if (!archive.Load(data, size, options.DecodeLimits)) {
        result.Status = SerializablePersistenceStatus::MalformedPayload;
        return result;
    }

    try {
        result.Deserialization = object.DeserializeDetailed(archive, options.Deserialization);
    } catch (...) {
        result.Status = SerializablePersistenceStatus::DeserializationFailed;
        return result;
    }

    if (!result.Deserialization.Success()) {
        result.Status = SerializablePersistenceStatus::DeserializationFailed;
    }
    return result;
}

inline bool ValidLocator(const char* locator) {
    return locator != nullptr && *locator != '\0';
}

} // namespace Detail

template<typename TObject>
SerializablePersistenceResult SaveSerializable(
    IFileStorage& storage,
    const char* path,
    const TObject& object,
    const SerializablePersistenceOptions& options = {}
) {
    if (!Detail::ValidLocator(path)) return Detail::MakeInvalidArgument();
    if (!storage.IsReady()) return Detail::MakeStorageFailure(StorageStatus::NotInitialized);

    Detail::SerializableBytes bytes;
    auto result = Detail::EncodeSerializable(object, options, bytes);
    if (!result) return result;

    StorageStatus status = StorageStatus::Success;
    if (options.PreferAtomicFileReplace && HasCapability(storage.GetCapabilities(), StorageCapability::Rename)) {
        AtomicFileStore atomic(storage);
        status = atomic.Replace(path, bytes.data(), bytes.size());
    } else {
        status = storage.Write(path, bytes.data(), bytes.size(), WriteMode::Replace);
    }

    if (status != StorageStatus::Success) {
        result.Status = SerializablePersistenceStatus::StorageError;
        result.Storage = status;
    }
    return result;
}

template<typename TObject>
SerializablePersistenceResult LoadSerializable(
    IFileStorage& storage,
    const char* path,
    TObject& object,
    const SerializablePersistenceOptions& options = {}
) {
    if (!Detail::ValidLocator(path)) return Detail::MakeInvalidArgument();
    if (!storage.IsReady()) return Detail::MakeStorageFailure(StorageStatus::NotInitialized);

    StorageEntry entry{};
    StorageStatus status = storage.Stat(path, entry);
    if (status != StorageStatus::Success) return Detail::MakeStorageFailure(status);
    if (entry.isDirectory) return Detail::MakeInvalidArgument();
    if (entry.size > options.MaximumPayloadBytes ||
        entry.size > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
        SerializablePersistenceResult result;
        result.Status = SerializablePersistenceStatus::PayloadTooLarge;
        result.PayloadBytes = entry.size > std::numeric_limits<std::size_t>::max()
            ? std::numeric_limits<std::size_t>::max()
            : static_cast<std::size_t>(entry.size);
        return result;
    }

    Detail::SerializableBytes bytes(static_cast<std::size_t>(entry.size));
    std::size_t bytesRead = 0;
    status = storage.Read(path, 0, bytes.data(), bytes.size(), bytesRead);
    if (status != StorageStatus::Success) return Detail::MakeStorageFailure(status);
    if (bytesRead != bytes.size()) return Detail::MakeStorageFailure(StorageStatus::CorruptData);

    return Detail::DecodeSerializable(bytes.data(), bytes.size(), object, options);
}

template<typename TObject>
SerializablePersistenceResult SaveSerializable(
    IKeyValueStorage& storage,
    const char* key,
    const TObject& object,
    const SerializablePersistenceOptions& options = {}
) {
    if (!Detail::ValidLocator(key)) return Detail::MakeInvalidArgument();
    if (!storage.IsReady()) return Detail::MakeStorageFailure(StorageStatus::NotInitialized);

    Detail::SerializableBytes bytes;
    auto result = Detail::EncodeSerializable(object, options, bytes);
    if (!result) return result;

    const StorageStatus status = storage.Write(key, bytes.data(), bytes.size());
    if (status != StorageStatus::Success) {
        result.Status = SerializablePersistenceStatus::StorageError;
        result.Storage = status;
    }
    return result;
}

template<typename TObject>
SerializablePersistenceResult LoadSerializable(
    IKeyValueStorage& storage,
    const char* key,
    TObject& object,
    const SerializablePersistenceOptions& options = {}
) {
    if (!Detail::ValidLocator(key)) return Detail::MakeInvalidArgument();
    if (!storage.IsReady()) return Detail::MakeStorageFailure(StorageStatus::NotInitialized);

    std::size_t size = 0;
    StorageStatus status = storage.GetSize(key, size);
    if (status != StorageStatus::Success) return Detail::MakeStorageFailure(status);
    if (size > options.MaximumPayloadBytes) {
        SerializablePersistenceResult result;
        result.Status = SerializablePersistenceStatus::PayloadTooLarge;
        result.PayloadBytes = size;
        return result;
    }

    Detail::SerializableBytes bytes(size);
    std::size_t bytesRead = 0;
    status = storage.Read(key, bytes.data(), bytes.size(), bytesRead);
    if (status != StorageStatus::Success) return Detail::MakeStorageFailure(status);
    if (bytesRead != bytes.size()) return Detail::MakeStorageFailure(StorageStatus::CorruptData);

    return Detail::DecodeSerializable(bytes.data(), bytes.size(), object, options);
}

} // namespace ESPressio::Persistence
