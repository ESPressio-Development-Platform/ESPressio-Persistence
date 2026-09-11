#pragma once
#include <ESPressio_IFileStorage.hpp>
#include <array>
#include <algorithm>
#include <cstring>

// Test-only bounded power-loss model. Live namespace/data and durable namespace/data
// are distinct, so returning Success from Write alone never makes a durable record.
class DurableFileModel final : public ESPressio::Persistence::IFileStorage {
    using Status=ESPressio::Persistence::StorageStatus;
    using Capability=ESPressio::Persistence::StorageCapability;
    struct File {
        bool Live=false,Stable=false;
        std::array<char,256> Name{},StableName{};
        std::array<std::uint8_t,512> Data{},StableData{};
        std::size_t Size=0,StableSize=0;
    };
    std::array<File,12> _files{};
    bool _ready=false;
    File* Find(const char* path) noexcept {
        for (auto& file:_files) if (file.Live && std::strcmp(path,file.Name.data())==0) return &file;
        return nullptr;
    }
    const File* Find(const char* path) const noexcept {
        for (const auto& file:_files) if (file.Live && std::strcmp(path,file.Name.data())==0) return &file;
        return nullptr;
    }
    static bool Name(std::array<char,256>& out,const char* value) noexcept {
        if (!value || !*value) return false;
        for (std::size_t i=0;i<out.size();++i) { out[i]=value[i]; if (!value[i]) return true; }
        return false;
    }
public:
    enum class Cut { None,PartialWrite,BeforeFileSync,AfterFileSync,BeforePublish,AfterPublish,BeforeDirectorySync,AfterDirectorySync };
    Cut FailAt=Cut::None;
    bool ProvesDurability=true;
    bool ProvesBounded=true;
    unsigned Writes=0,Publishes=0,DirectorySyncs=0;
    void (*AfterDurableCommit)(void*) noexcept=nullptr;
    void* HookContext=nullptr;
    bool CutAt(Cut point) noexcept { if (FailAt!=point) return false; FailAt=Cut::None; return true; }
    Status Initialize() override { if (_ready) return Status::AlreadyInitialized; _ready=true; return Status::Success; }
    void Shutdown() override { _ready=false; }
    bool IsReady() const override { return _ready; }
    const char* GetBackendName() const override { return "DurableFileModel"; }
    Capability GetCapabilities() const override {
        auto caps=Capability::Hierarchical|Capability::Rename|Capability::Directories;
        if (ProvesDurability) caps=caps|Capability::AtomicReplace|Capability::DurableFileSync|Capability::DurableDirectorySync;
        if (ProvesBounded) caps=caps|Capability::BoundedOperations;
        return caps;
    }
    ESPressio::Persistence::StorageStatistics GetStatistics() const override { return {}; }
    Status Exists(const char* path,bool& exists) const override {
        if (!_ready) return Status::NotInitialized;
        exists=path && (std::strcmp(path,"/")==0 || Find(path)); return Status::Success;
    }
    Status Stat(const char* path,ESPressio::Persistence::StorageEntry& entry) const override {
        if (!_ready) return Status::NotInitialized;
        if (!path) return Status::InvalidArgument;
        if (std::strcmp(path,"/")==0) { entry={}; entry.isDirectory=true; entry.path[0]='/'; return Status::Success; }
        const auto* file=Find(path); if (!file) return Status::NotFound;
        entry={}; entry.size=file->Size; std::memcpy(entry.path,file->Name.data(),file->Name.size()); return Status::Success;
    }
    Status Read(const char* path,std::uint64_t offset,std::uint8_t* buffer,std::size_t capacity,std::size_t& count) const override {
        count=0; if (!_ready) return Status::NotInitialized;
        const auto* file=Find(path); if (!file) return Status::NotFound;
        if (offset>=file->Size) return Status::Success;
        count=std::min(capacity,file->Size-static_cast<std::size_t>(offset));
        if (count) std::memcpy(buffer,file->Data.data()+offset,count); return Status::Success;
    }
    Status Write(const char* path,const std::uint8_t* data,std::size_t size,ESPressio::Persistence::WriteMode mode) override {
        if (!_ready) return Status::NotInitialized;
        if (size>512 || (!data && size)) return Status::NoSpace;
        auto* file=Find(path);
        if (!file) {
            for (auto& candidate:_files) if (!candidate.Live && !candidate.Stable) { file=&candidate; break; }
            if (!file) return Status::NoSpace;
            *file={}; if (!Name(file->Name,path)) return Status::InvalidArgument; file->Live=true;
        }
        ++Writes;
        auto offset=mode==ESPressio::Persistence::WriteMode::Append ? file->Size : 0;
        if (offset+size>file->Data.size()) return Status::NoSpace;
        bool partial=CutAt(Cut::PartialWrite); if (partial) size/=2;
        if (size) std::memcpy(file->Data.data()+offset,data,size); file->Size=offset+size;
        return partial ? Status::PartialWrite : Status::Success;
    }
    Status Remove(const char* path) override {
        if (!_ready) return Status::NotInitialized;
        auto* file=Find(path); if (!file) return Status::NotFound; file->Live=false; return Status::Success;
    }
    Status Rename(const char* from,const char* to) override {
        auto* file=Find(from); if (!file) return Status::NotFound;
        if (Find(to)) return Status::AlreadyExists;
        return Name(file->Name,to) ? Status::Success : Status::InvalidArgument;
    }
    Status SyncFile(const char* path) override {
        if (CutAt(Cut::BeforeFileSync)) return Status::IoError;
        auto* file=Find(path); if (!file) return Status::NotFound;
        file->StableData=file->Data; file->StableSize=file->Size;
        return CutAt(Cut::AfterFileSync) ? Status::IoError : Status::Success;
    }
    Status ReplaceFileAtomically(const char* from,const char* to) override {
        if (CutAt(Cut::BeforePublish)) return Status::IoError;
        auto* prepared=Find(from); if (!prepared) return Status::NotFound;
        if (auto* old=Find(to)) old->Live=false;
        if (!Name(prepared->Name,to)) return Status::InvalidArgument;
        ++Publishes; return CutAt(Cut::AfterPublish) ? Status::IoError : Status::Success;
    }
    Status SyncDirectory(const char*) override {
        if (CutAt(Cut::BeforeDirectorySync)) return Status::IoError;
        for (auto& file:_files) { file.Stable=file.Live; if (file.Live) file.StableName=file.Name; }
        ++DirectorySyncs;
        if (AfterDurableCommit) AfterDurableCommit(HookContext);
        return CutAt(Cut::AfterDirectorySync) ? Status::IoError : Status::Success;
    }
    Status CreateDirectory(const char*) override { return Status::NotSupported; }
    Status RemoveDirectory(const char*) override { return Status::NotSupported; }
    Status List(const char*,ESPressio::Persistence::StorageListCallback,void*) const override { return Status::NotSupported; }
    void PowerLoss() noexcept {
        for (auto& file:_files) {
            file.Live=file.Stable; file.Name=file.StableName; file.Data=file.StableData; file.Size=file.StableSize;
        }
        FailAt=Cut::None; AfterDurableCommit=nullptr; HookContext=nullptr; _ready=true;
    }
    bool CorruptCommittedByte(std::size_t offset) noexcept {
        for (auto& file:_files) if (file.Live && file.Stable && offset<file.Size) { file.Data[offset]^=1; file.StableData[offset]^=1; return true; }
        return false;
    }
};
