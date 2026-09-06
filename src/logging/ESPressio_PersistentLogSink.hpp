#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>
#include <type_traits>
#include <variant>

#include <ESPressio_IFileStorage.hpp>
#include <ESPressio_ILogSink.hpp>
#include <ESPressio_LogField.hpp>
#include <ESPressio_LogLevel.hpp>
#include <ESPressio_LogPolicies.hpp>

namespace ESPressio::Persistence {

/// <summary>
/// Synchronously materialises structured ESPressio log records into a bounded rolling file set.
/// </summary>
/// <remarks>
/// The Sink owns no storage backend and never retains the supplied Logging::LogRecordLease. The complete
/// record is encoded into one bounded stack/member buffer while Accept() is executing, then appended through
/// IFileStorage before Accept() returns. This preserves Logging's borrowed-record lifetime contract and keeps
/// filesystem/platform knowledge at the injected Persistence backend boundary.
///
/// Rotation is deliberately bounded and deterministic. Generation 0 is the current file, generation 1 is the
/// previous file, and so on. RollingLogPolicy must specify bounded TotalCapacity, FileCapacity and FileCount;
/// unlimited flash retention is rejected by this Sink. Storage failures are reflected in statistics/status only:
/// the Sink never attempts to log its own failures, avoiding Logger -> Sink -> Logger recursion.
/// </remarks>
template<std::size_t MaximumRecordBytes = 1024U, std::size_t MaximumFiles = 16U>
class PersistentLogSink final : public Logging::ILogSink {
    static_assert(MaximumRecordBytes >= 64U, "Persistent log record capacity is unrealistically small.");
    static_assert(MaximumFiles > 0U, "Persistent log file capacity must be non-zero.");

    class RecordWriter final {
    public:
        explicit RecordWriter(std::array<std::uint8_t, MaximumRecordBytes>& buffer) noexcept
            : _buffer(buffer) {}

        bool Append(std::string_view value) noexcept {
            if (value.size() > Remaining()) return false;
            if (!value.empty()) {
                std::memcpy(_buffer.data() + _size, value.data(), value.size());
                _size += value.size();
            }
            return true;
        }

        bool Append(char value) noexcept {
            if (Remaining() == 0U) return false;
            _buffer[_size++] = static_cast<std::uint8_t>(value);
            return true;
        }

        bool AppendEscaped(std::string_view value) noexcept {
            for (const char character : value) {
                switch (character) {
                    case '\\': if (!Append("\\\\")) return false; break;
                    case '\n': if (!Append("\\n")) return false; break;
                    case '\r': if (!Append("\\r")) return false; break;
                    case '\t': if (!Append("\\t")) return false; break;
                    default: if (!Append(character)) return false; break;
                }
            }
            return true;
        }

        bool AppendUnsigned(std::uint64_t value) noexcept {
            char text[32]{};
            const int length = std::snprintf(
                text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
            return length > 0 && Append(std::string_view(text, static_cast<std::size_t>(length)));
        }

        bool AppendSigned(std::int64_t value) noexcept {
            char text[32]{};
            const int length = std::snprintf(
                text, sizeof(text), "%lld", static_cast<long long>(value));
            return length > 0 && Append(std::string_view(text, static_cast<std::size_t>(length)));
        }

        bool AppendFloating(double value) noexcept {
            char text[40]{};
            const int length = std::snprintf(text, sizeof(text), "%.9g", value);
            return length > 0 && Append(std::string_view(text, static_cast<std::size_t>(length)));
        }

        const std::uint8_t* Data() const noexcept { return _buffer.data(); }
        std::size_t Size() const noexcept { return _size; }

    private:
        std::size_t Remaining() const noexcept { return MaximumRecordBytes - _size; }

        std::array<std::uint8_t, MaximumRecordBytes>& _buffer;
        std::size_t _size{0U};
    };

public:
    /// <summary>
    /// Constructs a non-owning persistent Sink. The storage backend must remain alive while the Sink is registered.
    /// </summary>
    PersistentLogSink(
        IFileStorage& storage,
        const char* directory,
        const char* fileName,
        const Logging::RollingLogPolicy& policy,
        Logging::LogLevelMask levelMask = Logging::AllLogLevels
    ) noexcept :
        _storage(&storage), _policy(policy), _levelMask(levelMask) {
        CopyText(_directory, directory);
        CopyText(_fileName, fileName);
    }

    /// <summary>
    /// Discovers existing generations and starts accepting records. The injected storage must already be ready.
    /// </summary>
    StorageStatus Initialize() noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_initialized.load(std::memory_order_acquire)) {
            return SetLastStatus(StorageStatus::AlreadyInitialized);
        }
        if (_storage == nullptr || !_storage->IsReady() || !ConfigurationIsValid()) {
            return SetLastStatus(_storage == nullptr || !_storage->IsReady()
                ? StorageStatus::NotInitialized
                : StorageStatus::InvalidArgument);
        }

        const auto capabilities = _storage->GetCapabilities();
        if (!HasCapability(capabilities, StorageCapability::Hierarchical) ||
            !HasCapability(capabilities, StorageCapability::Append) ||
            (_policy.FileCount.Count > 1U && !HasCapability(capabilities, StorageCapability::Rename))) {
            return SetLastStatus(StorageStatus::NotSupported);
        }

        if (!DirectoryIsRoot()) {
            if (!HasCapability(capabilities, StorageCapability::Directories)) {
                return SetLastStatus(StorageStatus::NotSupported);
            }
            bool exists = false;
            auto status = _storage->Exists(_directory.data(), exists);
            if (status != StorageStatus::Success) return SetLastStatus(status);
            if (!exists) {
                status = _storage->CreateDirectory(_directory.data());
                if (status != StorageStatus::Success && status != StorageStatus::AlreadyExists) {
                    return SetLastStatus(status);
                }
            } else {
                StorageEntry entry{};
                status = _storage->Stat(_directory.data(), entry);
                if (status != StorageStatus::Success) return SetLastStatus(status);
                if (!entry.isDirectory) return SetLastStatus(StorageStatus::InvalidArgument);
            }
        }

        ClearDiscoveredState();
        for (std::size_t generation = 0U; generation < FileCount(); ++generation) {
            char path[StorageEntry::MaximumPathLength]{};
            if (!BuildPath(generation, path, sizeof(path))) {
                return SetLastStatus(StorageStatus::InvalidArgument);
            }
            bool exists = false;
            auto status = _storage->Exists(path, exists);
            if (status != StorageStatus::Success) return SetLastStatus(status);
            if (!exists) continue;
            StorageEntry entry{};
            status = _storage->Stat(path, entry);
            if (status != StorageStatus::Success) return SetLastStatus(status);
            if (entry.isDirectory) return SetLastStatus(StorageStatus::InvalidArgument);

            // A generation produced under an older/larger policy cannot be allowed to violate the
            // currently configured per-file bound. Remove it before the Sink becomes observable.
            if (entry.size > FileCapacity()) {
                status = _storage->Remove(path);
                if (status != StorageStatus::Success && status != StorageStatus::NotFound) {
                    return SetLastStatus(status);
                }
                continue;
            }
            _exists[generation] = true;
            _sizes[generation] = entry.size;
            _retainedBytes += entry.size;
        }

        const auto pruneStatus = PruneToTotalCapacity();
        if (pruneStatus != StorageStatus::Success) return SetLastStatus(pruneStatus);
        _initialized.store(true, std::memory_order_release);
        return SetLastStatus(StorageStatus::Success);
    }

    /// <summary>Stops accepting records without shutting down the injected storage backend.</summary>
    void Shutdown() noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        _initialized.store(false, std::memory_order_release);
    }

    bool IsInitialized() const noexcept {
        return _initialized.load(std::memory_order_acquire);
    }

    void SetLevelMask(Logging::LogLevelMask levelMask) noexcept {
        _levelMask.store(levelMask, std::memory_order_relaxed);
    }

    Logging::LogLevelMask GetLevelMask() const noexcept {
        return _levelMask.load(std::memory_order_relaxed);
    }

    bool IsEnabled(Logging::LogLevel level, const Logging::LogCategory&) const noexcept override {
        return IsInitialized() && ContainsLevel(GetLevelMask(), level);
    }

    void Accept(const Logging::LogRecordLease& record) noexcept override {
        if (!IsInitialized()) return;
        const auto& view = record.View();
        if (!ContainsLevel(GetLevelMask(), view.Level)) {
            std::lock_guard<std::mutex> lock(_mutex);
            ++_statistics.FilteredRecords;
            return;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        if (!_initialized.load(std::memory_order_relaxed) || _storage == nullptr || !_storage->IsReady()) {
            ++_statistics.WriteFailures;
            (void)SetLastStatus(StorageStatus::NotInitialized);
            return;
        }

        RecordWriter writer(_recordBuffer);
        if (!EncodeRecord(view, writer) || writer.Size() == 0U ||
            writer.Size() > FileCapacity() || writer.Size() > TotalCapacity()) {
            ++_statistics.DroppedRecords;
            (void)SetLastStatus(StorageStatus::NoSpace);
            return;
        }

        if (_exists[0] && _sizes[0] + writer.Size() > FileCapacity()) {
            const auto rotation = Rotate();
            if (rotation != StorageStatus::Success) {
                ++_statistics.WriteFailures;
                (void)SetLastStatus(rotation);
                return;
            }
        }

        const auto prune = PruneForAppend(writer.Size());
        if (prune != StorageStatus::Success) {
            ++_statistics.WriteFailures;
            (void)SetLastStatus(prune);
            return;
        }

        char path[StorageEntry::MaximumPathLength]{};
        if (!BuildPath(0U, path, sizeof(path))) {
            ++_statistics.WriteFailures;
            (void)SetLastStatus(StorageStatus::InvalidArgument);
            return;
        }
        const auto status = _storage->Write(path, writer.Data(), writer.Size(), WriteMode::Append);
        if (status != StorageStatus::Success) {
            ++_statistics.WriteFailures;
            (void)SetLastStatus(status);
            return;
        }

        _exists[0] = true;
        _sizes[0] += writer.Size();
        _retainedBytes += writer.Size();
        ++_statistics.AcceptedRecords;
        _statistics.BytesWritten += writer.Size();
        (void)SetLastStatus(StorageStatus::Success);
    }

    /// <summary>Removes every retained generation while leaving the Sink initialized.</summary>
    StorageStatus Clear() noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_initialized.load(std::memory_order_relaxed) || _storage == nullptr || !_storage->IsReady()) {
            return SetLastStatus(StorageStatus::NotInitialized);
        }
        for (std::size_t generation = 0U; generation < FileCount(); ++generation) {
            if (!_exists[generation]) continue;
            char path[StorageEntry::MaximumPathLength]{};
            if (!BuildPath(generation, path, sizeof(path))) {
                return SetLastStatus(StorageStatus::InvalidArgument);
            }
            const auto status = _storage->Remove(path);
            if (status != StorageStatus::Success && status != StorageStatus::NotFound) {
                return SetLastStatus(status);
            }
            _exists[generation] = false;
            _sizes[generation] = 0U;
        }
        _retainedBytes = 0U;
        return SetLastStatus(StorageStatus::Success);
    }

    /// <summary>Reads one retained generation without exposing its backend-specific path.</summary>
    StorageStatus ReadGeneration(
        std::size_t generation,
        std::uint64_t offset,
        std::uint8_t* buffer,
        std::size_t capacity,
        std::size_t& bytesRead
    ) const noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        bytesRead = 0U;
        if (!_initialized.load(std::memory_order_relaxed) || _storage == nullptr || !_storage->IsReady()) {
            return StorageStatus::NotInitialized;
        }
        if (generation >= FileCount() || !_exists[generation]) return StorageStatus::NotFound;
        char path[StorageEntry::MaximumPathLength]{};
        if (!BuildPath(generation, path, sizeof(path))) return StorageStatus::InvalidArgument;
        return _storage->Read(path, offset, buffer, capacity, bytesRead);
    }

    std::size_t GenerationCapacity() const noexcept { return FileCount(); }

    bool GenerationExists(std::size_t generation) const noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        return generation < FileCount() && _exists[generation];
    }

    std::uint64_t GenerationSize(std::size_t generation) const noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        return generation < FileCount() && _exists[generation] ? _sizes[generation] : 0U;
    }

    std::uint64_t RetainedBytes() const noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        return _retainedBytes;
    }

    Logging::LogSinkStatistics GetStatistics() const noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        return _statistics;
    }

    StorageStatus GetLastStorageStatus() const noexcept {
        return _lastStatus.load(std::memory_order_relaxed);
    }

private:
    static const char* LevelName(Logging::LogLevel level) noexcept {
        switch (level) {
            case Logging::LogLevel::Trace: return "TRACE";
            case Logging::LogLevel::Debug: return "DEBUG";
            case Logging::LogLevel::Info: return "INFO";
            case Logging::LogLevel::Warn: return "WARN";
            case Logging::LogLevel::Error: return "ERROR";
            case Logging::LogLevel::Fatal: return "FATAL";
        }
        return "?";
    }

    static bool AppendFieldValue(RecordWriter& writer, const Logging::LogFieldValue& value) noexcept {
        bool result = false;
        std::visit(
            [&writer, &result](const auto& typedValue) noexcept {
                using TValue = std::decay_t<decltype(typedValue)>;
                if constexpr (std::is_same_v<TValue, bool>) {
                    result = writer.Append(typedValue ? "true" : "false");
                } else if constexpr (std::is_same_v<TValue, std::int32_t> ||
                                     std::is_same_v<TValue, std::int64_t>) {
                    result = writer.AppendSigned(static_cast<std::int64_t>(typedValue));
                } else if constexpr (std::is_same_v<TValue, std::uint32_t> ||
                                     std::is_same_v<TValue, std::uint64_t>) {
                    result = writer.AppendUnsigned(static_cast<std::uint64_t>(typedValue));
                } else if constexpr (std::is_same_v<TValue, float> || std::is_same_v<TValue, double>) {
                    result = writer.AppendFloating(static_cast<double>(typedValue));
                } else if constexpr (std::is_same_v<TValue, std::string_view>) {
                    result = writer.AppendEscaped(typedValue);
                }
            },
            value
        );
        return result;
    }

    static bool EncodeRecord(const Logging::LogRecordView& view, RecordWriter& writer) noexcept {
        if (!writer.Append("[mono=") || !writer.AppendUnsigned(view.Timestamp.MonotonicNanoseconds) ||
            !writer.Append("ns")) return false;
        if (view.Timestamp.SystemNanoseconds != 0U &&
            (!writer.Append(" system=") || !writer.AppendUnsigned(view.Timestamp.SystemNanoseconds) ||
             !writer.Append("ns"))) return false;
        if (!writer.Append("] [") || !writer.Append(LevelName(view.Level)) || !writer.Append("]")) return false;
        if (!view.Category.Name.empty() &&
            (!writer.Append(" [") || !writer.AppendEscaped(view.Category.Name) || !writer.Append("]"))) return false;
        if (!writer.Append(" ") || !writer.AppendEscaped(view.Message)) return false;
        for (const auto& field : view.Metadata) {
            if (!writer.Append(" ") || !writer.AppendEscaped(field.Name) || !writer.Append("=") ||
                !AppendFieldValue(writer, field.Value)) return false;
        }
        return writer.Append('\n');
    }

    bool ConfigurationIsValid() const noexcept {
        if (_directory[0] != '/' || _fileName[0] == '\0' || std::strchr(_fileName.data(), '/') != nullptr ||
            !_policy.IsValid() ||
            _policy.TotalCapacity.CapacityMode != Logging::LogByteCapacity::Mode::Bounded ||
            _policy.FileCapacity.CapacityMode != Logging::LogByteCapacity::Mode::Bounded ||
            _policy.FileCount.CapacityMode != Logging::LogCountCapacity::Mode::Bounded ||
            _policy.FileCount.Count == 0U || _policy.FileCount.Count > MaximumFiles ||
            _policy.TotalCapacity.Bytes < _policy.FileCapacity.Bytes ||
            _policy.FileCapacity.Bytes < MaximumRecordBytes) {
            return false;
        }
        return true;
    }

    bool DirectoryIsRoot() const noexcept {
        return _directory[0] == '/' && _directory[1] == '\0';
    }

    std::size_t FileCount() const noexcept { return _policy.FileCount.Count; }
    std::uint64_t FileCapacity() const noexcept { return _policy.FileCapacity.Bytes; }
    std::uint64_t TotalCapacity() const noexcept { return _policy.TotalCapacity.Bytes; }

    static void CopyText(std::array<char, StorageEntry::MaximumPathLength>& destination, const char* source) noexcept {
        destination.fill('\0');
        if (source == nullptr) return;
        std::strncpy(destination.data(), source, destination.size() - 1U);
        destination.back() = '\0';
    }

    bool BuildPath(std::size_t generation, char* output, std::size_t outputBytes) const noexcept {
        if (output == nullptr || outputBytes == 0U || generation >= MaximumFiles) return false;
        const int written = generation == 0U
            ? std::snprintf(
                output, outputBytes, DirectoryIsRoot() ? "/%s" : "%s/%s",
                DirectoryIsRoot() ? _fileName.data() : _directory.data(),
                DirectoryIsRoot() ? "" : _fileName.data())
            : std::snprintf(
                output, outputBytes, DirectoryIsRoot() ? "/%s.%u" : "%s/%s.%u",
                DirectoryIsRoot() ? _fileName.data() : _directory.data(),
                DirectoryIsRoot() ? static_cast<unsigned>(generation) : _fileName.data(),
                static_cast<unsigned>(generation));
        return written > 0 && static_cast<std::size_t>(written) < outputBytes;
    }

    void ClearDiscoveredState() noexcept {
        _exists.fill(false);
        _sizes.fill(0U);
        _retainedBytes = 0U;
    }

    StorageStatus RemoveGeneration(std::size_t generation) noexcept {
        if (generation >= FileCount() || !_exists[generation]) return StorageStatus::Success;
        char path[StorageEntry::MaximumPathLength]{};
        if (!BuildPath(generation, path, sizeof(path))) return StorageStatus::InvalidArgument;
        const auto status = _storage->Remove(path);
        if (status != StorageStatus::Success && status != StorageStatus::NotFound) return status;
        if (_retainedBytes >= _sizes[generation]) _retainedBytes -= _sizes[generation];
        else _retainedBytes = 0U;
        _exists[generation] = false;
        _sizes[generation] = 0U;
        return StorageStatus::Success;
    }

    StorageStatus Rotate() noexcept {
        if (FileCount() == 1U) return RemoveGeneration(0U);

        auto status = RemoveGeneration(FileCount() - 1U);
        if (status != StorageStatus::Success) return status;

        for (std::size_t destination = FileCount() - 1U; destination > 0U; --destination) {
            const std::size_t source = destination - 1U;
            if (!_exists[source]) continue;
            char from[StorageEntry::MaximumPathLength]{};
            char to[StorageEntry::MaximumPathLength]{};
            if (!BuildPath(source, from, sizeof(from)) || !BuildPath(destination, to, sizeof(to))) {
                return StorageStatus::InvalidArgument;
            }
            status = _storage->Rename(from, to);
            if (status != StorageStatus::Success) {
                // Rebuild the in-memory view after a partial rotation so the next operation never acts
                // on stale generation metadata. No log is emitted from this failure path.
                (void)RediscoverExistingFiles();
                return status;
            }
            _exists[destination] = true;
            _sizes[destination] = _sizes[source];
            _exists[source] = false;
            _sizes[source] = 0U;
        }
        return StorageStatus::Success;
    }

    StorageStatus RediscoverExistingFiles() noexcept {
        ClearDiscoveredState();
        for (std::size_t generation = 0U; generation < FileCount(); ++generation) {
            char path[StorageEntry::MaximumPathLength]{};
            if (!BuildPath(generation, path, sizeof(path))) return StorageStatus::InvalidArgument;
            bool exists = false;
            auto status = _storage->Exists(path, exists);
            if (status != StorageStatus::Success) return status;
            if (!exists) continue;
            StorageEntry entry{};
            status = _storage->Stat(path, entry);
            if (status != StorageStatus::Success || entry.isDirectory) {
                return status == StorageStatus::Success ? StorageStatus::InvalidArgument : status;
            }
            _exists[generation] = true;
            _sizes[generation] = entry.size;
            _retainedBytes += entry.size;
        }
        return StorageStatus::Success;
    }

    StorageStatus PruneToTotalCapacity() noexcept {
        while (_retainedBytes > TotalCapacity()) {
            bool removed = false;
            for (std::size_t generation = FileCount(); generation > 0U; --generation) {
                const std::size_t index = generation - 1U;
                if (!_exists[index]) continue;
                const auto status = RemoveGeneration(index);
                if (status != StorageStatus::Success) return status;
                removed = true;
                break;
            }
            if (!removed) break;
        }
        return _retainedBytes <= TotalCapacity() ? StorageStatus::Success : StorageStatus::NoSpace;
    }

    StorageStatus PruneForAppend(std::size_t appendBytes) noexcept {
        while (_retainedBytes + appendBytes > TotalCapacity()) {
            bool removed = false;
            // Prefer removing historical generations. With TotalCapacity >= FileCapacity the current
            // generation alone can always accept a record that passed the size checks above.
            for (std::size_t generation = FileCount(); generation > 1U; --generation) {
                const std::size_t index = generation - 1U;
                if (!_exists[index]) continue;
                const auto status = RemoveGeneration(index);
                if (status != StorageStatus::Success) return status;
                removed = true;
                break;
            }
            if (!removed) return StorageStatus::NoSpace;
        }
        return StorageStatus::Success;
    }

    StorageStatus SetLastStatus(StorageStatus status) const noexcept {
        _lastStatus.store(status, std::memory_order_relaxed);
        return status;
    }

    IFileStorage* _storage{nullptr};
    Logging::RollingLogPolicy _policy{};
    std::array<char, StorageEntry::MaximumPathLength> _directory{};
    std::array<char, StorageEntry::MaximumPathLength> _fileName{};
    std::array<bool, MaximumFiles> _exists{};
    std::array<std::uint64_t, MaximumFiles> _sizes{};
    std::array<std::uint8_t, MaximumRecordBytes> _recordBuffer{};
    std::atomic<Logging::LogLevelMask> _levelMask{Logging::AllLogLevels};
    std::atomic<bool> _initialized{false};
    mutable std::atomic<StorageStatus> _lastStatus{StorageStatus::NotInitialized};
    mutable std::mutex _mutex;
    std::uint64_t _retainedBytes{0U};
    Logging::LogSinkStatistics _statistics{};
};

} // namespace ESPressio::Persistence
