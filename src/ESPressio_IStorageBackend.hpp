#pragma once

#include <ESPressio_PersistenceTypes.hpp>

namespace ESPressio::Persistence {

class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;

    virtual StorageStatus Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual bool IsReady() const = 0;
    virtual const char* GetBackendName() const = 0;
    virtual StorageCapability GetCapabilities() const = 0;
    virtual StorageStatistics GetStatistics() const = 0;
};

} // namespace ESPressio::Persistence
