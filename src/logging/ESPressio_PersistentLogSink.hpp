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
/// Synchronously materialises structured ESPressio log records into a bounded rolling IFileStorage file set.
/// </summary>
/// <remarks>
/// The Sink does not own the storage backend and never retains a Logging::LogRecordLease. A complete record is
/// rendered into a fixed-capacity buffer and written before Accept() returns, preserving Logging's borrowed-lifetime
/// contract. Generation zero is the active file; higher generations are progressively older.
///
/// This implementation deliberately accepts only fully bounded RollingLogPolicy values. Persistent flash logging
/// should never obtain unlimited retention implicitly or explicitly. Storage failures update local status/statistics;
/// the Sink never emits a log from its own failure path, avoiding Logger -> Sink -> Logger recursion.
/// </remarks>
template<std::size_t MaximumRecordBytes = 1024U, std::size_t MaximumFiles = 16U>
class PersistentLogSink final : public Logging::ILogSink {
    static_assert(MaximumRecordBytes >= 64U, "Persistent log record capacity is unrealistically small.");
    static_assert(MaximumFiles > 0U, "Persistent log generation capacity must be non-zero.");

public:
    using ReadCallback = bool (*)(const std::uint8_t* data, std::size_t size, void* context);

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
    /// Discovers retained generations and enables the Sink. The injected IFileStorage must already be initialized.
    /// </summary>
    StorageStatus Initialize() noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_initialized.load(std::memory_order_acquire)) return SetStatus(StorageStatus::AlreadyInitialized);
        if (_storage == nullptr || !_storage->IsReady()) return SetStatus(StorageStatus::NotInitialized);
        if (!ConfigurationIsValid()) return SetStatus(StorageStatus::InvalidArgument);

        const auto capabilities = _storage->GetCapabilities();
        if (!HasCapability(capabilities, StorageCapability::Hierarchical) ||
            !HasCapability(capabilities, StorageCapability::Append) ||
            (FileCount() > 1U && !HasCapability(capabilities, StorageCapability::Rename))) {
            return SetStatus(StorageStatus::NotSupported);
        }
        if (!DirectoryIsRoot()) {
            if (!HasCapability(capabilities, StorageCapability::Directories)) {
                return SetStatus(StorageStatus::NotSupported);
            }
            bool exists = false;
            auto status = _storage->Exists(_directory.data(), exists);
            if (status != StorageStatus::Success) return SetStatus(status);
            if (!exists) {
                status = _storage->CreateDirectory(_directory.data());
                if (status != StorageStatus::Success && status != StorageStatus::AlreadyExists) {
                    return SetStatus(status);
                }
            } else {
                StorageEntry entry{};
                status = _storage->Stat(_directory.data(), entry);
                if (status != StorageStatus::Success) return SetStatus(status);
                if (!entry.isDirectory) return SetStatus(StorageStatus::InvalidArgument);
            }
        }

        ResetDiscoveredState();
        for (std::size_t generation = 0U; generation < FileCount(); ++generation) {
            char path[StorageEntry::MaximumPathLength]{};
            if (!BuildPath(generation, path, sizeof(path))) return SetStatus(StorageStatus::InvalidArgument);
            bool exists = false;
            auto status = _storage->Exists(path, exists);
            if (status != StorageStatus::Success) return SetStatus(status);
            if (!exists) continue;

            StorageEntry entry{};
            status = _storage->Stat(path, entry);
            if (status != StorageStatus::Success) return SetStatus(status);
            if (entry.isDirectory) return SetStatus(StorageStatus::InvalidArgument);

            // A generation written under an older/larger policy may not violate the current file bound.
            if (entry.size > FileCapacity()) {
                status = _storage->Remove(path);
                if (status != StorageStatus::Success && status != StorageStatus::NotFound) {
                    return SetStatus(status);
                }
                continue;
            }
            _exists[generation] = true;
            _sizes[generation] = entry.size;
            _retainedBytes += entry.size;
        }

        const auto pruned = PruneToTotalCapacity();
        if (pruned != StorageStatus::Success) return SetStatus(pruned);
        _initialized.store(true, std::memory_order_release);
        return SetStatus(StorageStatus::Success);
    }

    /// <summary>Stops accepting records. It does not shut down the injected storage backend.</summary>
    void Shutdown() noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        _initialized.store(false, std::memory_order_release);
    }

    bool IsInitialized() const noexcept { return _initialized.load(std::memory_order_acquire); }

    void SetLevelMask(Logging::LogLevelMask mask) noexcept {
        _levelMask.store(mask, std::memory_order_relaxed);
    }

    Logging::LogLevelMask GetLevelMask() const noexcept {
        return _levelMask.load(std::memory_order_relaxed);
    }

    bool IsEnabled(Logging::LogLevel level, const Logging::LogCategory&) const noexcept override {
        return IsInitialized() && Logging::ContainsLevel(GetLevelMask(), level);
    }

    void Accept(const Logging::LogRecordLease& record) noexcept override {
        if (!IsInitialized()) return;
        const auto& view = record.View();
        if (!Logging::ContainsLevel(GetLevelMask(), view.Level)) {
            std::lock_guard<std::mutex> lock(_mutex);
            ++_statistics.FilteredRecords;
            return;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        if (!_initialized.load(std::memory_order_relaxed) || _storage == nullptr || !_storage->IsReady()) {
            ++_statistics.WriteFailures;
            (void)SetStatus(StorageStatus::NotInitialized);
            return;
        }

        Writer writer(_recordBuffer);
        if (!Encode(view, writer) || writer.Size() == 0U ||
            writer.Size() > FileCapacity() || writer.Size() > TotalCapacity()) {
            ++_statistics.DroppedRecords;
            (void)SetStatus(StorageStatus::NoSpace);
            return;
        }

        if (_exists[0U] && _sizes[0U] + writer.Size() > FileCapacity()) {
            const auto rotated = Rotate();
            if (rotated != StorageStatus::Success) {
                ++_statistics.WriteFailures;
                (void)SetStatus(rotated);
                return;
            }
        }

        const auto pruned = PruneForAppend(writer.Size());
        if (pruned != StorageStatus::Success) {
            ++_statistics.WriteFailures;
            (void)SetStatus(pruned);
            return;
        }

        char path[StorageEntry::MaximumPathLength]{};
        if (!BuildPath(0U, path, sizeof(path))) {
            ++_statistics.WriteFailures;
            (void)SetStatus(StorageStatus::InvalidArgument);
            return;
        }
        const auto status = _storage->Write(path, writer.Data(), writer.Size(), WriteMode::Append);
        if (status != StorageStatus::Success) {
            ++_statistics.WriteFailures;
            (void)SetStatus(status);
            return;
        }

        _exists[0U] = true;
        _sizes[0U] += writer.Size();
        _retainedBytes += writer.Size();
        ++_statistics.AcceptedRecords;
        _statistics.BytesWritten += writer.Size();
        (void)SetStatus(StorageStatus::Success);
    }

    /// <summary>Deletes every retained generation while leaving the Sink initialized.</summary>
    StorageStatus Clear() noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_initialized.load(std::memory_order_relaxed) || _storage == nullptr || !_storage->IsReady()) {
            return SetStatus(StorageStatus::NotInitialized);
        }
        for (std::size_t generation = 0U; generation < FileCount(); ++generation) {
            const auto status = RemoveGeneration(generation);
            if (status != StorageStatus::Success) return SetStatus(status);
        }
        return SetStatus(StorageStatus::Success);
    }

    /// <summary>
    /// Visits retained bytes oldest-generation first while holding the Sink mutex, producing a stable dump snapshot.
    /// The callback must not log or call back into this Sink.
    /// </summary>
    StorageStatus VisitRetained(ReadCallback callback, void* context = nullptr) const noexcept {
        if (callback == nullptr) return StorageStatus::InvalidArgument;
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_initialized.load(std::memory_order_relaxed) || _storage == nullptr || !_storage->IsReady()) {
            return StorageStatus::NotInitialized;
        }

        std::array<std::uint8_t, 256U> chunk{};
        for (std::size_t remaining = FileCount(); remaining > 0U; --remaining) {
            const std::size_t generation = remaining - 1U;
            if (!_exists[generation]) continue;
            char path[StorageEntry::MaximumPathLength]{};
            if (!BuildPath(generation, path, sizeof(path))) return StorageStatus::InvalidArgument;
            std::uint64_t offset = 0U;
            const auto snapshotSize = _sizes[generation];
            while (offset < snapshotSize) {
                const auto available = snapshotSize - offset;
                const std::size_t requested = available < chunk.size()
                    ? static_cast<std::size_t>(available)
                    : chunk.size();
                std::size_t bytesRead = 0U;
                const auto status = _storage->Read(path, offset, chunk.data(), requested, bytesRead);
                if (status != StorageStatus::Success) return status;
                if (bytesRead == 0U) return StorageStatus::IoError;
                if (!callback(chunk.data(), bytesRead, context)) return StorageStatus::Success;
                offset += bytesRead;
            }
        }
        return StorageStatus::Success;
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
    class Writer final {
    public:
        explicit Writer(std::array<std::uint8_t, MaximumRecordBytes>& buffer) noexcept : _buffer(buffer) {}

        bool Text(std::string_view value) noexcept {
            if (value.size() > Remaining()) return false;
            if (!value.empty()) {
                std::memcpy(_buffer.data() + _size, value.data(), value.size());
                _size += value.size();
            }
            return true;
        }

        bool Character(char value) noexcept {
            if (Remaining() == 0U) return false;
            _buffer[_size++] = static_cast<std::uint8_t>(value);
            return true;
        }

        bool Escaped(std::string_view value) noexcept {
            for (const char character : value) {
                switch (character) {
                    case '\\': if (!Text("\\\\")) return false; break;
                    case '\n': if (!Text("\\n")) return false; break;
                    case '\r': if (!Text("\\r")) return false; break;
                    case '\t': if (!Text("\\t")) return false; break;
                    default: if (!Character(character)) return false; break;
                }
            }
            return true;
        }

        bool Unsigned(std::uint64_t value) noexcept {
            char text[32]{};
            const int length = std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
            return length > 0 && Text(std::string_view(text, static_cast<std::size_t>(length)));
        }

        bool Signed(std::int64_t value) noexcept {
            char text[32]{};
            const int length = std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(value));
            return length > 0 && Text(std::string_view(text, static_cast<std::size_t>(length)));
        }

        bool Floating(double value) noexcept {
            char text[40]{};
            const int length = std::snprintf(text, sizeof(text), "%.9g", value);
            return length > 0 && Text(std::string_view(text, static_cast<std::size_t>(length)));
        }

        const std::uint8_t* Data() const noexcept { return _buffer.data(); }
        std::size_t Size() const noexcept { return _size; }

    private:
        std::size_t Remaining() const noexcept { return MaximumRecordBytes - _size; }
        std::array<std::uint8_t, MaximumRecordBytes>& _buffer;
        std::size_t _size{0U};
    };

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

    static bool FieldValue(Writer& writer, const Logging::LogFieldValue& value) noexcept {
        bool success = false;
        std::visit(
            [&writer, &success](const auto& typed) noexcept {
                using TValue = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<TValue, bool>) {
                    success = writer.Text(typed ? "true" : "false");
                } else if constexpr (std::is_same_v<TValue, std::int32_t> || std::is_same_v<TValue, std::int64_t>) {
                    success = writer.Signed(static_cast<std::int64_t>(typed));
                } else if constexpr (std::is_same_v<TValue, std::uint32_t> || std::is_same_v<TValue, std::uint64_t>) {
                    success = writer.Unsigned(static_cast<std::uint64_t>(typed));
                } else if constexpr (std::is_same_v<TValue, float> || std::is_same_v<TValue, double>) {
                    success = writer.Floating(static_cast<double>(typed));
                } else if constexpr (std::is_same_v<TValue, std::string_view>) {
                    success = writer.Escaped(typed);
                }
            }, value);
        return success;
    }

    static bool Encode(const Logging::LogRecordView& view, Writer& writer) noexcept {
        if (!writer.Text("[mono=") || !writer.Unsigned(view.Timestamp.MonotonicNanoseconds) || !writer.Text("ns")) {
            return false;
        }
        if (view.Timestamp.SystemNanoseconds != 0U &&
            (!writer.Text(" system=") || !writer.Unsigned(view.Timestamp.SystemNanoseconds) || !writer.Text("ns"))) {
            return false;
        }
        if (!writer.Text("] [") || !writer.Text(LevelName(view.Level)) || !writer.Text("]")) return false;
        if (!view.Category.Name.empty() &&
            (!writer.Text(" [") || !writer.Escaped(view.Category.Name) || !writer.Text("]"))) return false;
        if (!writer.Text(" ") || !writer.Escaped(view.Message)) return false;
        for (const auto& field : view.Metadata) {
            if (!writer.Text(" ") || !writer.Escaped(field.Name) || !writer.Text("=") ||
                !FieldValue(writer, field.Value)) return false;
        }
        return writer.Character('\n');
    }

    bool ConfigurationIsValid() const noexcept {
        if (_directory[0U] != '/' || _fileName[0U] == '\0' || std::strchr(_fileName.data(), '/') != nullptr) return false;
        const std::size_t directoryLength = std::strlen(_directory.data());
        if (directoryLength > 1U && _directory[directoryLength - 1U] == '/') return false;
        if (!_policy.IsValid() ||
            _policy.TotalCapacity.CapacityMode != Logging::LogByteCapacity::Mode::Bounded ||
            _policy.FileCapacity.CapacityMode != Logging::LogByteCapacity::Mode::Bounded ||
            _policy.FileCount.CapacityMode != Logging::LogCountCapacity::Mode::Bounded ||
            _policy.FileCount.Count == 0U || _policy.FileCount.Count > MaximumFiles ||
            _policy.TotalCapacity.Bytes < _policy.FileCapacity.Bytes) return false;
        char path[StorageEntry::MaximumPathLength]{};
        return BuildPath(_policy.FileCount.Count - 1U, path, sizeof(path));
    }

    bool DirectoryIsRoot() const noexcept { return _directory[0U] == '/' && _directory[1U] == '\0'; }
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
        int written = -1;
        if (DirectoryIsRoot()) {
            written = generation == 0U
                ? std::snprintf(output, outputBytes, "/%s", _fileName.data())
                : std::snprintf(output, outputBytes, "/%s.%u", _fileName.data(), static_cast<unsigned>(generation));
        } else {
            written = generation == 0U
                ? std::snprintf(output, outputBytes, "%s/%s", _directory.data(), _fileName.data())
                : std::snprintf(output, outputBytes, "%s/%s.%u", _directory.data(), _fileName.data(), static_cast<unsigned>(generation));
        }
        return written > 0 && static_cast<std::size_t>(written) < outputBytes;
    }

    void ResetDiscoveredState() noexcept {
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
        _retainedBytes = _retainedBytes >= _sizes[generation]
            ? _retainedBytes - _sizes[generation]
            : 0U;
        _exists[generation] = false;
        _sizes[generation] = 0U;
        return StorageStatus::Success;
    }

    StorageStatus Rediscover() noexcept {
        ResetDiscoveredState();
        for (std::size_t generation = 0U; generation < FileCount(); ++generation) {
            char path[StorageEntry::MaximumPathLength]{};
            if (!BuildPath(generation, path, sizeof(path))) return StorageStatus::InvalidArgument;
            bool exists = false;
            auto status = _storage->Exists(path, exists);
            if (status != StorageStatus::Success) return status;
            if (!exists) continue;
            StorageEntry entry{};
            status = _storage->Stat(path, entry);
            if (status != StorageStatus::Success) return status;
            if (entry.isDirectory) return StorageStatus::InvalidArgument;
            _exists[generation] = true;
            _sizes[generation] = entry.size;
            _retainedBytes += entry.size;
        }
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
                (void)Rediscover();
                return status;
            }
            _exists[destination] = true;
            _sizes[destination] = _sizes[source];
            _exists[source] = false;
            _sizes[source] = 0U;
        }
        return StorageStatus::Success;
    }

    StorageStatus PruneToTotalCapacity() noexcept {
        while (_retainedBytes > TotalCapacity()) {
            bool removed = false;
            for (std::size_t remaining = FileCount(); remaining > 0U; --remaining) {
                const std::size_t generation = remaining - 1U;
                if (!_exists[generation]) continue;
                const auto status = RemoveGeneration(generation);
                if (status != StorageStatus::Success) return status;
                removed = true;
                break;
            }
            if (!removed) return StorageStatus::NoSpace;
        }
        return StorageStatus::Success;
    }

    StorageStatus PruneForAppend(std::size_t bytes) noexcept {
        while (_retainedBytes + bytes > TotalCapacity()) {
            bool removed = false;
            // Historical generations are always removed before the active generation. Because TotalCapacity is
            // required to be >= FileCapacity, a record that fits the active file cannot require deleting it here.
            for (std::size_t remaining = FileCount(); remaining > 1U; --remaining) {
                const std::size_t generation = remaining - 1U;
                if (!_exists[generation]) continue;
                const auto status = RemoveGeneration(generation);
                if (status != StorageStatus::Success) return status;
                removed = true;
                break;
            }
            if (!removed) return StorageStatus::NoSpace;
        }
        return StorageStatus::Success;
    }

    StorageStatus SetStatus(StorageStatus status) const noexcept {
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
