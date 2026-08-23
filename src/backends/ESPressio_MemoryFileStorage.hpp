#pragma once

#include <ESPressio_IFileStorage.hpp>
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ESPressio::Persistence {

class MemoryFileStorage final : public IFileStorage {
public:
    StorageStatus Initialize() override {
        if (_ready) return StorageStatus::AlreadyInitialized;
        _ready = true;
        _directories.insert("/");
        return StorageStatus::Success;
    }

    void Shutdown() override { _ready = false; }
    bool IsReady() const override { return _ready; }
    const char* GetBackendName() const override { return "MemoryFileStorage"; }

    StorageCapability GetCapabilities() const override {
        return StorageCapability::Hierarchical |
               StorageCapability::Directories |
               StorageCapability::Rename |
               StorageCapability::Append |
               StorageCapability::AtomicReplace;
    }

    StorageStatistics GetStatistics() const override { return {}; }

    StorageStatus Exists(const char* path, bool& exists) const override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(path)) return StorageStatus::InvalidArgument;
        exists = _files.count(path) != 0 || _directories.count(path) != 0;
        return StorageStatus::Success;
    }

    StorageStatus Stat(const char* path, StorageEntry& entry) const override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(path)) return StorageStatus::InvalidArgument;
        const auto file = _files.find(path);
        if (file != _files.end()) {
            CopyPath(entry.path, path);
            entry.size = file->second.size();
            entry.isDirectory = false;
            return StorageStatus::Success;
        }
        if (_directories.count(path) != 0) {
            CopyPath(entry.path, path);
            entry.size = 0;
            entry.isDirectory = true;
            return StorageStatus::Success;
        }
        return StorageStatus::NotFound;
    }

    StorageStatus Read(
        const char* path,
        uint64_t offset,
        uint8_t* buffer,
        std::size_t capacity,
        std::size_t& bytesRead
    ) const override {
        bytesRead = 0;
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(path) || (buffer == nullptr && capacity != 0)) {
            return StorageStatus::InvalidArgument;
        }
        const auto it = _files.find(path);
        if (it == _files.end()) return StorageStatus::NotFound;
        if (offset >= it->second.size()) return StorageStatus::Success;
        const std::size_t available = it->second.size() - static_cast<std::size_t>(offset);
        bytesRead = std::min(capacity, available);
        if (bytesRead != 0) {
            std::memcpy(buffer, it->second.data() + static_cast<std::size_t>(offset), bytesRead);
        }
        return StorageStatus::Success;
    }

    StorageStatus Write(
        const char* path,
        const uint8_t* data,
        std::size_t size,
        WriteMode mode = WriteMode::Replace
    ) override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(path) || (data == nullptr && size != 0)) {
            return StorageStatus::InvalidArgument;
        }
        auto& value = _files[path];
        if (mode == WriteMode::Replace) value.clear();
        if (size != 0) value.insert(value.end(), data, data + size);
        return StorageStatus::Success;
    }

    StorageStatus Remove(const char* path) override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(path)) return StorageStatus::InvalidArgument;
        return _files.erase(path) != 0 ? StorageStatus::Success : StorageStatus::NotFound;
    }

    StorageStatus Rename(const char* from, const char* to) override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(from) || !Valid(to)) return StorageStatus::InvalidArgument;
        const auto it = _files.find(from);
        if (it == _files.end()) return StorageStatus::NotFound;
        if (_files.count(to) != 0 || _directories.count(to) != 0) return StorageStatus::AlreadyExists;
        _files.emplace(to, std::move(it->second));
        _files.erase(it);
        return StorageStatus::Success;
    }

    StorageStatus CreateDirectory(const char* path) override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(path)) return StorageStatus::InvalidArgument;
        if (_directories.count(path) != 0 || _files.count(path) != 0) return StorageStatus::AlreadyExists;
        _directories.insert(path);
        return StorageStatus::Success;
    }

    StorageStatus RemoveDirectory(const char* path) override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(path) || std::strcmp(path, "/") == 0) return StorageStatus::InvalidArgument;
        if (_directories.count(path) == 0) return StorageStatus::NotFound;
        const std::string prefix = std::string(path) + "/";
        for (const auto& directory : _directories) {
            if (directory != path && directory.rfind(prefix, 0) == 0) return StorageStatus::Busy;
        }
        for (const auto& [name, ignored] : _files) {
            (void)ignored;
            if (name.rfind(prefix, 0) == 0) return StorageStatus::Busy;
        }
        _directories.erase(path);
        return StorageStatus::Success;
    }

    StorageStatus List(
        const char* path,
        StorageListCallback callback,
        void* context = nullptr
    ) const override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(path) || callback == nullptr) return StorageStatus::InvalidArgument;
        if (_directories.count(path) == 0) return StorageStatus::NotFound;
        const std::string prefix = std::strcmp(path, "/") == 0 ? "/" : std::string(path) + "/";
        for (const auto& item : _directories) {
            if (item == path || !IsDirectChild(prefix, item)) continue;
            StorageEntry entry{};
            CopyPath(entry.path, item.c_str());
            entry.isDirectory = true;
            if (!callback(entry, context)) return StorageStatus::Success;
        }
        for (const auto& [name, value] : _files) {
            if (!IsDirectChild(prefix, name)) continue;
            StorageEntry entry{};
            CopyPath(entry.path, name.c_str());
            entry.size = value.size();
            if (!callback(entry, context)) return StorageStatus::Success;
        }
        return StorageStatus::Success;
    }

private:
    static bool Valid(const char* value) { return value != nullptr && *value != '\0'; }
    static bool IsDirectChild(const std::string& prefix, const std::string& value) {
        if (value.rfind(prefix, 0) != 0) return false;
        const std::string remainder = value.substr(prefix.size());
        return !remainder.empty() && remainder.find('/') == std::string::npos;
    }
    static void CopyPath(char* destination, const char* source) {
        std::strncpy(destination, source, StorageEntry::MaximumPathLength - 1);
        destination[StorageEntry::MaximumPathLength - 1] = '\0';
    }

    bool _ready = false;
    std::map<std::string, std::vector<uint8_t>> _files;
    std::set<std::string> _directories;
};

} // namespace ESPressio::Persistence
