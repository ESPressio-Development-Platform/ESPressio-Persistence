#pragma once

#include <cstddef>
#include <cstdint>

namespace ESPressio::Persistence {

/// <summary>Result status returned by persistence backend operations.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum class WriteMode : uint8_t {
    Replace = 0,
    Append
};

/// <summary>Bit flags describing optional features exposed by a storage backend.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 4 bytes
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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
/**
 * ESPressio Memory Audit
 * Members:
 * - totalBytes (uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - usedBytes (uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - freeBytes (uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - capacityKnown (bool): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 28 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct StorageStatistics {
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    uint64_t freeBytes = 0;
    bool capacityKnown = false;
};

/// <summary>Metadata describing one file or directory returned by a storage backend.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - path (char[MaximumPathLength]): 256 bytes [0 bytes dynamic allocation]
 * - size (uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - isDirectory (bool): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 268 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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
