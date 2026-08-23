#pragma once

#include <cstddef>
#include <cstdint>

namespace ESPressio::Persistence {

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

enum class WriteMode : uint8_t {
    Replace = 0,
    Append
};

enum class StorageCapability : uint32_t {
    None              = 0,
    Hierarchical      = 1u << 0,
    KeyValue          = 1u << 1,
    Directories       = 1u << 2,
    Rename            = 1u << 3,
    Append            = 1u << 4,
    Removable         = 1u << 5,
    CapacityReporting = 1u << 6,
    AtomicReplace     = 1u << 7
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

constexpr bool HasCapability(StorageCapability value, StorageCapability capability) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(capability)) != 0;
}

struct StorageStatistics {
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    uint64_t freeBytes = 0;
    bool capacityKnown = false;
};

struct StorageEntry {
    static constexpr std::size_t MaximumPathLength = 256;

    char path[MaximumPathLength] = {};
    uint64_t size = 0;
    bool isDirectory = false;
};

using StorageListCallback = bool (*)(const StorageEntry& entry, void* context);

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
