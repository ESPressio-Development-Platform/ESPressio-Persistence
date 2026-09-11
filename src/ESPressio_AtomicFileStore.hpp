#pragma once
#include <array>
#include <ESPressio_IFileStorage.hpp>

namespace ESPressio::Persistence {
/// <summary>Bounded crash-consistent file replacement using explicit durable data and atomic namespace operations.</summary>
/// <remarks>The caller owns the target and its .tmp sibling and serializes operations. No backup/rollback or
/// ordinary-Write fallback exists. Unsupported durability is rejected before modifying storage.</remarks>
class AtomicFileStore final {
    IFileStorage& _storage;
public:
    /// <summary>Borrows a file backend. Construction performs no storage operation or allocation.</summary>
    explicit AtomicFileStore(IFileStorage& storage) noexcept : _storage(storage) {}
    /// <summary>Reports whether the backend declares all crash-durability operations required by this helper.</summary>
    bool SupportsDurableReplacement() const noexcept {
        const auto caps=_storage.GetCapabilities();
        return HasCapability(caps,StorageCapability::AtomicReplace) &&
            HasCapability(caps,StorageCapability::DurableFileSync) &&
            HasCapability(caps,StorageCapability::DurableDirectorySync);
    }
    /// <summary>Writes/syncs a prepared sibling, atomically publishes it, then durably syncs the parent directory.</summary>
    /// <remarks>Success means known durable commit. Publication/sync failure returns CommitAmbiguous and never
    /// rolls back a possibly committed value. Interruptions before publication leave the old target intact.
    /// Derived paths use two fixed arrays of StorageEntry::MaximumPathLength bytes.</remarks>
    StorageStatus Replace(const char* path,const std::uint8_t* data,std::size_t size) {
        if (!path || !*path || (!data && size)) return StorageStatus::InvalidArgument;
        if (!_storage.IsReady()) return StorageStatus::NotInitialized;
        if (!SupportsDurableReplacement()) return StorageStatus::NotSupported;
        constexpr auto capacity=StorageEntry::MaximumPathLength;
        std::array<char,capacity> temporary{};
        std::array<char,capacity> parent{};
        std::size_t length=0,slash=capacity;
        while (length<capacity && path[length]) {
            if (path[length]=='/') slash=length;
            if (length+5>=capacity) return StorageStatus::InvalidArgument;
            temporary[length]=path[length]; ++length;
        }
        if (!length || length==capacity || path[length-1]=='/') return StorageStatus::InvalidArgument;
        for (std::size_t i=0;i<5;++i) temporary[length+i]=".tmp"[i];
        if (slash==capacity) parent[0]='.';
        else if (slash==0) parent[0]='/';
        else for (std::size_t i=0;i<slash;++i) parent[i]=path[i];
        try {
            auto status=_storage.Remove(temporary.data());
            if (status!=StorageStatus::Success && status!=StorageStatus::NotFound) return status;
            status=_storage.Write(temporary.data(),data,size,WriteMode::Replace);
            if (status!=StorageStatus::Success) return status;
            status=_storage.SyncFile(temporary.data());
            if (status!=StorageStatus::Success) return status;
        } catch (...) { return StorageStatus::IoError; }
        // From this point onward neither an error nor an exception proves publication did not occur.
        try {
            if (_storage.ReplaceFileAtomically(temporary.data(),path)!=StorageStatus::Success)
                return StorageStatus::CommitAmbiguous;
            if (_storage.SyncDirectory(parent.data())!=StorageStatus::Success)
                return StorageStatus::CommitAmbiguous;
        } catch (...) { return StorageStatus::CommitAmbiguous; }
        return StorageStatus::Success;
    }
};
}
