#pragma once

#include <ESPressio_IFileStorage.hpp>
#include <ESPressio_Memory.hpp>
#include <algorithm>
#include <cstring>
#include <functional>
#include <string_view>
#include <utility>

namespace ESPressio::Persistence {

/// <summary>In-memory hierarchical <c>IFileStorage</c> implementation intended for volatile storage, host use, and tests.</summary>
/// <remarks>Owned paths, file payloads, and associative-container nodes use ESPressio System ExternalPreferred storage. Directory relationship checks operate on borrowed string views and do not allocate prefix or substring temporaries.</remarks>
class MemoryFileStorage final : public IFileStorage {
private:
    static constexpr auto ExternalPreferred =
        System::Memory::MemoryPolicy::ExternalPreferred;
    using StorageString = System::Memory::String<ExternalPreferred>;
    using StorageBytes = System::Memory::ByteVector<ExternalPreferred>;
    using FileStorage = System::Memory::Map<
        StorageString,
        StorageBytes,
        ExternalPreferred,
        std::less<>
    >;
    using DirectoryStorage = System::Memory::Set<
        StorageString,
        ExternalPreferred,
        std::less<>
    >;

public:
    /// <inheritdoc/>
    StorageStatus Initialize() override {
        if (_ready) return StorageStatus::AlreadyInitialized;
        try {
            _directories.insert(StorageString("/"));
        } catch (...) {
            return StorageStatus::NoSpace;
        }
        _ready = true;
        return StorageStatus::Success;
    }

    /// <inheritdoc/>
    void Shutdown() override { _ready = false; }
    /// <inheritdoc/>
    bool IsReady() const override { return _ready; }
    /// <inheritdoc/>
    const char* GetBackendName() const override { return "MemoryFileStorage"; }

    /// <inheritdoc/>
    StorageCapability GetCapabilities() const override {
        return StorageCapability::Hierarchical |
               StorageCapability::Directories |
               StorageCapability::Rename |
               StorageCapability::Append |
               StorageCapability::AtomicReplace;
    }

    /// <inheritdoc/>
    StorageStatistics GetStatistics() const override { return {}; }

    /// <inheritdoc/>
    StorageStatus Exists(const char* path, bool& exists) const override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(path)) return StorageStatus::InvalidArgument;
        exists = _files.find(path) != _files.end() ||
                 _directories.find(path) != _directories.end();
        return StorageStatus::Success;
    }

    /// <inheritdoc/>
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
        if (_directories.find(path) != _directories.end()) {
            CopyPath(entry.path, path);
            entry.size = 0;
            entry.isDirectory = true;
            return StorageStatus::Success;
        }
        return StorageStatus::NotFound;
    }

    /// <inheritdoc/>
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

    /// <inheritdoc/>
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

        auto it = _files.find(path);
        if (it == _files.end()) {
            try {
                it = _files.emplace(StorageString(path), StorageBytes{}).first;
            } catch (...) {
                return StorageStatus::NoSpace;
            }
        }

        try {
            if (mode == WriteMode::Replace) it->second.clear();
            if (size != 0) it->second.insert(it->second.end(), data, data + size);
        } catch (...) {
            return StorageStatus::NoSpace;
        }
        return StorageStatus::Success;
    }

    /// <inheritdoc/>
    StorageStatus Remove(const char* path) override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(path)) return StorageStatus::InvalidArgument;
        const auto it = _files.find(path);
        if (it == _files.end()) return StorageStatus::NotFound;
        _files.erase(it);
        return StorageStatus::Success;
    }

    /// <inheritdoc/>
    StorageStatus Rename(const char* from, const char* to) override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(from) || !Valid(to)) return StorageStatus::InvalidArgument;
        const auto it = _files.find(from);
        if (it == _files.end()) return StorageStatus::NotFound;
        if (_files.find(to) != _files.end() ||
            _directories.find(to) != _directories.end()) {
            return StorageStatus::AlreadyExists;
        }
        try {
            const auto inserted = _files.emplace(
                StorageString(to),
                std::move(it->second)
            );
            if (!inserted.second) return StorageStatus::AlreadyExists;
        } catch (...) {
            return StorageStatus::NoSpace;
        }
        _files.erase(it);
        return StorageStatus::Success;
    }

    /// <inheritdoc/>
    StorageStatus CreateDirectory(const char* path) override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(path)) return StorageStatus::InvalidArgument;
        if (_directories.find(path) != _directories.end() ||
            _files.find(path) != _files.end()) {
            return StorageStatus::AlreadyExists;
        }
        try {
            _directories.insert(StorageString(path));
        } catch (...) {
            return StorageStatus::NoSpace;
        }
        return StorageStatus::Success;
    }

    /// <inheritdoc/>
    StorageStatus RemoveDirectory(const char* path) override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(path) || std::strcmp(path, "/") == 0) return StorageStatus::InvalidArgument;
        const auto directory = _directories.find(path);
        if (directory == _directories.end()) return StorageStatus::NotFound;
        const std::string_view parent(path);
        for (const auto& candidate : _directories) {
            const auto view = View(candidate);
            if (view != parent && IsDescendant(parent, view)) return StorageStatus::Busy;
        }
        for (const auto& item : _files) {
            if (IsDescendant(parent, View(item.first))) return StorageStatus::Busy;
        }
        _directories.erase(directory);
        return StorageStatus::Success;
    }

    /// <inheritdoc/>
    StorageStatus List(
        const char* path,
        StorageListCallback callback,
        void* context = nullptr
    ) const override {
        if (!_ready) return StorageStatus::NotInitialized;
        if (!Valid(path) || callback == nullptr) return StorageStatus::InvalidArgument;
        if (_directories.find(path) == _directories.end()) return StorageStatus::NotFound;
        const std::string_view parent(path);
        for (const auto& item : _directories) {
            const auto view = View(item);
            if (view == parent || !IsDirectChild(parent, view)) continue;
            StorageEntry entry{};
            CopyPath(entry.path, item.c_str());
            entry.isDirectory = true;
            if (!callback(entry, context)) return StorageStatus::Success;
        }
        for (const auto& item : _files) {
            if (!IsDirectChild(parent, View(item.first))) continue;
            StorageEntry entry{};
            CopyPath(entry.path, item.first.c_str());
            entry.size = item.second.size();
            if (!callback(entry, context)) return StorageStatus::Success;
        }
        return StorageStatus::Success;
    }

private:
    static bool Valid(const char* value) { return value != nullptr && *value != '\0'; }

    static std::string_view View(const StorageString& value) noexcept {
        return std::string_view(value.data(), value.size());
    }

    static bool IsDescendant(std::string_view parent, std::string_view value) noexcept {
        if (parent.empty() || value.size() <= parent.size()) return false;
        if (value.compare(0, parent.size(), parent) != 0) return false;
        return value[parent.size()] == '/';
    }

    static bool IsDirectChild(std::string_view parent, std::string_view value) noexcept {
        std::size_t childStart = 0;
        if (parent == "/") {
            if (value.size() <= 1 || value.front() != '/') return false;
            childStart = 1;
        } else {
            if (!IsDescendant(parent, value)) return false;
            childStart = parent.size() + 1;
        }
        const auto remainder = value.substr(childStart);
        return !remainder.empty() && remainder.find('/') == std::string_view::npos;
    }

    static void CopyPath(char* destination, const char* source) {
        std::strncpy(destination, source, StorageEntry::MaximumPathLength - 1);
        destination[StorageEntry::MaximumPathLength - 1] = '\0';
    }

    bool _ready = false;
    FileStorage _files;
    DirectoryStorage _directories;
};

} // namespace ESPressio::Persistence
