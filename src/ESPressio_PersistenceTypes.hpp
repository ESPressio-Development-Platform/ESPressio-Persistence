#pragma once

#include <cstddef>
#include <cstdint>

namespace ESPressio::Persistence {

/// <summary>Result status returned by persistence backend operations.</summary>
enum class StorageStatus : uint8_t {
    Success = 0,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    NotSupported,
    PermissionDenied,
    NoSpace,
    CorruptData,
    Busy,
    IoError,
    PartialWrite,
    UnknownError
};

/// <summary>Controls whether file writes replace existing content or append to it.</summary>
enum class WriteMode : uint8_t {
    Replace = 0,
    Append
};

/// <summary>Bit flags describing optional features exposed by a storage backend.</summary>
enum class StorageCapability : uint32_t {
    None              = 0,
    Hierarchical      = 1u << 0,
    KeyValue          = 1u << 1,
    Directories       = 1u << 2,
    Rename            = 1u << 3,
    Append            = 1u << 4,
    Removable         = 1u << 5,
    CapacityReporting = 1u << 6,
    AtomicReplace     = 1u << 7,
    SequentialRead    = 1u << 8
};

constexpr StorageCapability operator|(StorageCapability lhs, StorageCapability rhs) {
    return static_cast<StorageCapability>(
        static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs)
    );
}

constexpr StorageCapability operator&(StorageCapability lhs, StorageCapability rhs) {
    return static_cast<StorageCapability>(
        static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs)
    );
}

/// <summary>Determines whether a capability set contains a requested storage capability.</summary>
constexpr bool HasCapability(StorageCapability value, StorageCapability capability) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(capability)) != 0;
}

/// <summary>Capacity statistics reported by a storage backend.</summary>
struct StorageStatistics {
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    uint64_t freeBytes = 0;
    bool capacityKnown = false;
};

/// <summary>Metadata describing one file or directory returned by a storage backend.</summary>
struct StorageEntry {
    /// <summary>Maximum number of bytes reserved for an entry path, including terminator storage.</summary>
    static constexpr std::size_t MaximumPathLength = 256;

    char path[MaximumPathLength] = {};
    uint64_t size = 0;
    bool isDirectory = false;
};

/// <summary>Callback invoked while enumerating storage entries; return <c>false</c> to stop enumeration.</summary>
using StorageListCallback = bool (*)(const StorageEntry& entry, void* context);

/// <summary>Returns a stable diagnostic name for a storage operation status.</summary>
inline const char* StorageStatusName(StorageStatus status) {
    switch (status) {
        case StorageStatus::Success: return "Success";
        case StorageStatus::NotInitialized: return "NotInitialized";
        case StorageStatus::AlreadyInitialized: return "AlreadyInitialized";
        case StorageStatus::InvalidArgument: return "InvalidArgument";
        case StorageStatus::NotFound: return "NotFound";
        case StorageStatus::AlreadyExists: return "AlreadyExists";
        case StorageStatus::NotSupported: return "NotSupported";
        case StorageStatus::PermissionDenied: return "PermissionDenied";
        case StorageStatus::NoSpace: return "NoSpace";
        case StorageStatus::CorruptData: return "CorruptData";
        case StorageStatus::Busy: return "Busy";
        case StorageStatus::IoError: return "IoError";
        case StorageStatus::PartialWrite: return "PartialWrite";
        default: return "UnknownError";
    }
}

} // namespace ESPressio::Persistence
