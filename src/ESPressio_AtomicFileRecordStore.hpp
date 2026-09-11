#pragma once
#include <array>
#include <cstring>
#include <limits>
#include "ESPressio_AtomicFileStore.hpp"
#include "ESPressio_IAtomicRecordStore.hpp"

namespace ESPressio::Persistence {
/// <summary>Fixed-key, fixed-buffer durable record store using a proven atomic filesystem publication contract.</summary>
/// <remarks>Composition supplies all keys and an existing directory before Recover. Transactions are owner-serialized.
/// The store owns one MaximumRecordBytes+64 scratch buffer, one fixed key array and one fixed directory buffer.
/// Generic file backends lacking durability or bounded-operation guarantees are rejected without writes.</remarks>
template<std::size_t MaximumRecordBytes,std::size_t RecordCount>
class AtomicFileRecordStore final : public IAtomicRecordStore {
    static_assert(MaximumRecordBytes>0 && MaximumRecordBytes<=std::numeric_limits<std::uint32_t>::max()-64u,
        "Atomic record payload bound must be positive and leave room for framing");
    static_assert(RecordCount>0,"Atomic file record store needs at least one registered key");
    static constexpr std::size_t Header=64;
    IFileStorage& _storage;
    const std::array<AtomicRecordKey,RecordCount> _keys;
    std::array<char,StorageEntry::MaximumPathLength> _directory{};
    std::size_t _directorySize=0;
    std::array<std::uint8_t,MaximumRecordBytes+Header> _scratch{};
    bool _valid=true;
    bool _ready=false;
    static AtomicRecordStatus Map(StorageStatus status) noexcept {
        switch (status) {
            case StorageStatus::Success: return AtomicRecordStatus::Success;
            case StorageStatus::NotFound: return AtomicRecordStatus::NotFound;
            case StorageStatus::NotSupported: return AtomicRecordStatus::NotSupported;
            case StorageStatus::NoSpace: return AtomicRecordStatus::NoSpace;
            case StorageStatus::Busy: return AtomicRecordStatus::Busy;
            case StorageStatus::CorruptData: return AtomicRecordStatus::Corrupt;
            case StorageStatus::CommitAmbiguous: return AtomicRecordStatus::CommitAmbiguous;
            default: return AtomicRecordStatus::StorageFailure;
        }
    }
    bool Registered(const AtomicRecordKey& key) const noexcept {
        for (const auto& candidate:_keys) if (candidate==key) return true;
        return false;
    }
    bool Path(const AtomicRecordKey& key,std::array<char,StorageEntry::MaximumPathLength>& path) const noexcept {
        if (!_valid || !key || !Registered(key)) return false;
        std::size_t cursor=0;
        for (std::size_t i=0;i<_directorySize;++i) path[cursor++]=_directory[i];
        if (cursor && path[cursor-1]!='/') path[cursor++]='/';
        constexpr char hex[]="0123456789abcdef";
        for (std::size_t i=0;i<key.Size();++i) { path[cursor++]=hex[key.Data()[i]>>4]; path[cursor++]=hex[key.Data()[i]&15]; }
        path[cursor]='\0'; return true;
    }
    static void Put(std::uint8_t* bytes,std::size_t offset,std::uint64_t value,unsigned size) noexcept {
        for (unsigned i=0;i<size;++i) { bytes[offset+i]=static_cast<std::uint8_t>(value); value>>=8; }
    }
    static std::uint64_t Get(const std::uint8_t* bytes,std::size_t offset,unsigned size) noexcept {
        std::uint64_t value=0;
        for (unsigned i=0;i<size;++i) value|=std::uint64_t(bytes[offset+i])<<(8*i);
        return value;
    }
    static std::uint32_t Crc(const std::uint8_t* bytes,std::size_t size) noexcept {
        std::uint32_t crc=0xffffffffu;
        for (std::size_t i=0;i<size;++i) {
            if (i>=56 && i<60) continue;
            crc^=bytes[i];
            for (unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
        }
        return ~crc;
    }
    AtomicRecordStatus Load(const AtomicRecordKey& key,const char* path,std::size_t& payload,std::uint64_t& generation) noexcept {
        try {
            StorageEntry entry{};
            auto status=_storage.Stat(path,entry);
            if (status!=StorageStatus::Success) return Map(status);
            if (entry.isDirectory || entry.size<Header || entry.size>_scratch.size()) return AtomicRecordStatus::Corrupt;
            std::size_t read=0;
            status=_storage.Read(path,0,_scratch.data(),static_cast<std::size_t>(entry.size),read);
            if (status!=StorageStatus::Success) return Map(status);
            if (read!=entry.size) return AtomicRecordStatus::Corrupt;
            const auto* bytes=_scratch.data();
            if (bytes[0]!='E' || bytes[1]!='P' || bytes[2]!='R' || bytes[3]!='D' || Get(bytes,4,2)!=1 || Get(bytes,6,2)!=Header)
                return AtomicRecordStatus::Corrupt;
            payload=static_cast<std::size_t>(Get(bytes,20,4)); generation=Get(bytes,8,8);
            if (!generation || payload>MaximumRecordBytes || read!=Header+payload || bytes[16]!=key.Size() || Get(bytes,56,4)!=Crc(bytes,read))
                return AtomicRecordStatus::Corrupt;
            for (std::size_t i=17;i<20;++i) if (bytes[i]) return AtomicRecordStatus::Corrupt;
            for (std::size_t i=60;i<64;++i) if (bytes[i]) return AtomicRecordStatus::Corrupt;
            for (std::size_t i=0;i<32;++i) if (bytes[24+i]!=(i<key.Size() ? key.Data()[i] : 0)) return AtomicRecordStatus::Corrupt;
            return AtomicRecordStatus::Success;
        } catch (...) { return AtomicRecordStatus::StorageFailure; }
    }
public:
    /// <summary>Copies the fixed key set and directory. Invalid/duplicate keys or an overlong directory reject Recover.</summary>
    AtomicFileRecordStore(IFileStorage& storage,const std::array<AtomicRecordKey,RecordCount>& keys,std::string_view directory) noexcept
        : _storage(storage),_keys(keys) {
        // Reserve slash, largest hex key, .tmp suffix and terminator before any storage operation.
        if (directory.empty() || directory.size()+1+64+5>_directory.size()) { _valid=false; return; }
        for (std::size_t i=0;i<directory.size();++i) {
            if (directory[i]=='\0') { _valid=false; return; }
            _directory[i]=directory[i];
        }
        _directorySize=directory.size();
        for (std::size_t i=0;i<RecordCount;++i) {
            if (!_keys[i]) _valid=false;
            for (std::size_t j=0;j<i;++j) if (_keys[i]==_keys[j]) _valid=false;
        }
    }
    /// <inheritdoc/>
    AtomicRecordCapabilities Capabilities() const noexcept override {
        const auto caps=_storage.GetCapabilities();
        const bool durable=HasCapability(caps,StorageCapability::AtomicReplace) && HasCapability(caps,StorageCapability::DurableFileSync) && HasCapability(caps,StorageCapability::DurableDirectorySync);
        return {durable,HasCapability(caps,StorageCapability::BoundedOperations),MaximumRecordBytes,RecordCount};
    }
    /// <inheritdoc/>
    AtomicRecordStatus Recover() noexcept override {
        _ready=false;
        if (!_valid) return AtomicRecordStatus::InvalidArgument;
        const auto caps=Capabilities();
        if (!caps.DurableOldOrNew || !caps.BoundedOperations) return AtomicRecordStatus::NotSupported;
        if (!_storage.IsReady()) return AtomicRecordStatus::StorageFailure;
        try {
            StorageEntry directory{};
            const auto status=_storage.Stat(_directory.data(),directory);
            if (status!=StorageStatus::Success || !directory.isDirectory) return AtomicRecordStatus::StorageFailure;
        } catch (...) { return AtomicRecordStatus::StorageFailure; }
        for (const auto& key:_keys) {
            std::array<char,StorageEntry::MaximumPathLength> path{}; Path(key,path);
            std::size_t size=0; std::uint64_t generation=0;
            const auto status=Load(key,path.data(),size,generation);
            if (status!=AtomicRecordStatus::Success && status!=AtomicRecordStatus::NotFound) return status;
        }
        // Orphan prepared files are not published records and never become a recovery fallback.
        _ready=true; return AtomicRecordStatus::Success;
    }
    /// <inheritdoc/>
    AtomicRecordStatus Read(const AtomicRecordKey& key,std::uint8_t* buffer,std::size_t capacity,std::size_t& bytesRead) noexcept override {
        bytesRead=0;
        if (!_ready) return AtomicRecordStatus::StorageFailure;
        if (!buffer && capacity) return AtomicRecordStatus::InvalidArgument;
        std::array<char,StorageEntry::MaximumPathLength> path{};
        if (!Path(key,path)) return AtomicRecordStatus::InvalidArgument;
        std::size_t size=0; std::uint64_t generation=0;
        const auto status=Load(key,path.data(),size,generation);
        if (status!=AtomicRecordStatus::Success) return status;
        if (size>capacity) return AtomicRecordStatus::BufferTooSmall;
        if (size) std::memcpy(buffer,_scratch.data()+Header,size);
        bytesRead=size; return AtomicRecordStatus::Success;
    }
    /// <inheritdoc/>
    AtomicRecordStatus ReplaceAtomically(const AtomicRecordKey& key,const std::uint8_t* data,std::size_t size) noexcept override {
        if (!_ready) return AtomicRecordStatus::StorageFailure;
        if ((!data && size) || size>MaximumRecordBytes) return AtomicRecordStatus::InvalidArgument;
        std::array<char,StorageEntry::MaximumPathLength> path{};
        if (!Path(key,path)) return AtomicRecordStatus::InvalidArgument;
        std::size_t previousSize=0; std::uint64_t generation=0;
        auto status=Load(key,path.data(),previousSize,generation);
        if (status!=AtomicRecordStatus::Success && status!=AtomicRecordStatus::NotFound) return status;
        if (generation==std::numeric_limits<std::uint64_t>::max()) return AtomicRecordStatus::NoSpace;
        ++generation;
        for (std::size_t i=0;i<Header;++i) _scratch[i]=0;
        auto* bytes=_scratch.data(); bytes[0]='E'; bytes[1]='P'; bytes[2]='R'; bytes[3]='D';
        Put(bytes,4,1,2); Put(bytes,6,Header,2); Put(bytes,8,generation,8); bytes[16]=static_cast<std::uint8_t>(key.Size());
        Put(bytes,20,size,4); for (std::size_t i=0;i<key.Size();++i) bytes[24+i]=key.Data()[i];
        if (size) std::memcpy(bytes+Header,data,size);
        Put(bytes,56,Crc(bytes,Header+size),4);
        AtomicFileStore atomic(_storage);
        return Map(atomic.Replace(path.data(),bytes,Header+size));
    }
    /// <inheritdoc/>
    AtomicRecordStatus RemoveAfterCommit(const AtomicRecordKey& key) noexcept override {
        if (!_ready) return AtomicRecordStatus::StorageFailure;
        std::array<char,StorageEntry::MaximumPathLength> path{};
        if (!Path(key,path)) return AtomicRecordStatus::InvalidArgument;
        try {
            const auto status=_storage.Remove(path.data());
            if (status!=StorageStatus::Success && status!=StorageStatus::NotFound) return AtomicRecordStatus::CommitAmbiguous;
            // Even an already absent name may reflect an earlier unsynced removal.
            if (_storage.SyncDirectory(_directory.data())!=StorageStatus::Success) return AtomicRecordStatus::CommitAmbiguous;
            return AtomicRecordStatus::Success;
        } catch (...) { return AtomicRecordStatus::CommitAmbiguous; }
    }
};
}
