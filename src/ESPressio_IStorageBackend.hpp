#pragma once

#include <ESPressio_PersistenceTypes.hpp>

namespace ESPressio::Persistence {

/// <summary>Common lifecycle, capability, and statistics contract implemented by persistence backends.</summary>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;

    /// <summary>Initializes the backend and makes storage operations available.</summary>
    virtual StorageStatus Initialize() = 0;
    /// <summary>Releases backend resources and ends its ready state.</summary>
    virtual void Shutdown() = 0;
    /// <summary>Indicates whether the backend is initialized and ready for storage operations.</summary>
    virtual bool IsReady() const = 0;
    /// <summary>Returns a human-readable backend implementation name.</summary>
    virtual const char* GetBackendName() const = 0;
    /// <summary>Returns the storage capabilities supported by this backend.</summary>
    virtual StorageCapability GetCapabilities() const = 0;
    /// <summary>Returns the backend's current storage statistics.</summary>
    virtual StorageStatistics GetStatistics() const = 0;
};

} // namespace ESPressio::Persistence
