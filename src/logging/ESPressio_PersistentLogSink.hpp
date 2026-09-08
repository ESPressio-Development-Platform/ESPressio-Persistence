#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
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
/// Materialises structured ESPressio log records into a bounded RAM queue and flushes them into a bounded rolling
/// IFileStorage file set only when the owning composition explicitly requests storage work.
/// </summary>
/// <remarks>
/// Accept() never performs filesystem I/O and never retains a Logging::LogRecordLease. The complete borrowed record
/// is rendered into a fixed-capacity queue slot before Accept() returns, preserving Logging's "borrow across calls;
/// own across time" contract while keeping flash latency away from arbitrary logging caller threads.
///
/// Generation zero is the active file; higher generations are progressively older. This Sink accepts only fully
/// bounded RollingLogPolicy values and a bounded queue. LogBufferOverflowPolicy::Block is deliberately unsupported:
/// a persistent diagnostic Sink must not turn a radio/ISR-adjacent/worker log call into an unbounded wait on storage.
///
/// Flush()/FlushOne() are explicit. Applications should call them from a non-critical owner context. DropOldest is
/// the default queue policy; dropped records accumulate a synthetic dropped_entries record which is written at the
/// next successful flush opportunity. Storage failures never recursively log through Logger.
///
/// The injected IFileStorage is non-owning and should not be externally mutated at the Sink's managed paths while
/// the Sink is initialized.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - _storage (IFileStorage*): 4 bytes [0 bytes dynamic allocation]
 * - _policy (Logging::RollingLogPolicy): 24 bytes [0 bytes dynamic allocation]
 * - _directory (std::array<char, StorageEntry::MaximumPathLength>): 256 bytes [0 bytes dynamic allocation]
 * - _fileName (std::array<char, StorageEntry::MaximumPathLength>): 256 bytes [0 bytes dynamic allocation]
 * - _queue (std::array<QueuedRecord, QueueCapacity>): QueueCapacity * (4 bytes known/aligned storage + MaximumRecordBytes * (1 bytes)) [0 bytes dynamic allocation]
 * - _encodeBuffer (std::array<std::uint8_t, MaximumRecordBytes>): MaximumRecordBytes * (1 bytes) [0 bytes dynamic allocation]
 * - _flushBuffer (std::array<std::uint8_t, MaximumRecordBytes>): MaximumRecordBytes * (1 bytes) [0 bytes dynamic allocation]
 * - _exists (std::array<bool, MaximumFiles>): MaximumFiles * (1 bytes) [0 bytes dynamic allocation]
 * - _sizes (std::array<std::uint64_t, MaximumFiles>): MaximumFiles * (8 bytes) [0 bytes dynamic allocation]
 * - _levelMask (std::atomic<Logging::LogLevelMask>): 1 bytes [0 bytes dynamic allocation]
 * - _overflowPolicy (Logging::LogBufferOverflowPolicy): 1 bytes [0 bytes dynamic allocation]
 * - _initialized (std::atomic<bool>): 1 bytes [0 bytes dynamic allocation]
 * - _maintenance (std::atomic<bool>): 1 bytes [0 bytes dynamic allocation]
 * - _lastStatus (std::atomic<StorageStatus>): 1 bytes [0 bytes dynamic allocation]
 * - _queueMutex (std::mutex): 4 bytes [native synchronization state may allocate platform resources lazily]
 * - _storageMutex (std::mutex): 4 bytes [native synchronization state may allocate platform resources lazily]
 * - _flushMutex (std::mutex): 4 bytes [native synchronization state may allocate platform resources lazily]
 * - _queueHead (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - _queueTail (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - _queueSize (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - _pendingDropNotice (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - _retainedBytes (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - _acceptedRecords (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _filteredRecords (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _droppedRecords (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _writeFailures (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _bytesWritten (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _queueHighWaterMark (std::atomic<std::size_t>): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 636 bytes known/aligned storage + QueueCapacity * (4 bytes known/aligned storage + MaximumRecordBytes * (1 bytes)) + MaximumRecordBytes * (1 bytes) + MaximumRecordBytes * (1 bytes) + MaximumFiles * (1 bytes) + MaximumFiles * (8 bytes) [_queueMutex: native synchronization state may allocate platform resources lazily; _storageMutex: native synchronization state may allocate platform resources lazily; _flushMutex: native synchronization state may allocate platform resources lazily]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
template<
    std::size_t MaximumRecordBytes = 1024U,
    std::size_t MaximumFiles = 16U,
    std::size_t QueueCapacity = 8U>
class PersistentLogSink final : public Logging::ILogSink {
    static_assert(MaximumRecordBytes >= 64U, "Persistent log record capacity is unrealistically small.");
    static_assert(MaximumFiles > 0U, "Persistent log generation capacity must be non-zero.");
    static_assert(QueueCapacity > 0U, "Persistent log queue capacity must be non-zero.");

    static constexpr std::size_t MaximumDropNoticeBytes =
        (sizeof("[PERSISTENT_LOG] dropped_entries=") - 1U) +
        std::numeric_limits<std::uint64_t>::digits10 + 2U;

public:
    using ReadCallback = bool (*)(const std::uint8_t* data, std::size_t size, void* context);

    PersistentLogSink(
        IFileStorage& storage,
        const char* directory,
        const char* fileName,
        const Logging::RollingLogPolicy& policy,
        Logging::LogLevelMask levelMask = Logging::AllLogLevels,
        Logging::LogBufferOverflowPolicy overflowPolicy = Logging::LogBufferOverflowPolicy::DropOldest
    ) noexcept :
        _storage(&storage),
        _policy(policy),
        _levelMask(levelMask),
        _overflowPolicy(overflowPolicy) {
        CopyText(_directory, directory);
        CopyText(_fileName, fileName);
    }

    /// <summary>
    /// Discovers retained generations and enables queue admission. The injected IFileStorage must already be ready.
    /// </summary>
    StorageStatus Initialize() noexcept {
        std::lock_guard<std::mutex> flushLock(_flushMutex);
        std::lock_guard<std::mutex> storageLock(_storageMutex);
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
        _maintenance.store(false, std::memory_order_release);
        _initialized.store(true, std::memory_order_release);
        return SetStatus(StorageStatus::Success);
    }

    /// <summary>
    /// Stops queue admission and waits for in-flight queue admission and flush/maintenance storage work to complete.
    /// Queued records remain owned by the Sink and can be flushed if the same Sink instance is initialized again.
    /// </summary>
    void Shutdown() noexcept {
        _initialized.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> flushLock(_flushMutex);
        std::lock_guard<std::mutex> queueLock(_queueMutex);
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

    /// <summary>
    /// Encodes and enqueues one record. No IFileStorage operation is reachable from this call path.
    /// </summary>
    void Accept(const Logging::LogRecordLease& record) noexcept override {
        if (!IsInitialized()) return;
        const auto& view = record.View();
        if (!Logging::ContainsLevel(GetLevelMask(), view.Level)) {
            _filteredRecords.fetch_add(1U, std::memory_order_relaxed);
            return;
        }

        std::lock_guard<std::mutex> queueLock(_queueMutex);
        if (!_initialized.load(std::memory_order_relaxed)) return;

        Writer writer(_encodeBuffer);
        if (!Encode(view, writer) || writer.Size() == 0U ||
            writer.Size() > FileCapacity() || writer.Size() > TotalCapacity()) {
            NoteDroppedLocked(1U);
            (void)SetStatus(StorageStatus::NoSpace);
            return;
        }

        if (_queueSize == QueueCapacity) {
            switch (_overflowPolicy) {
                case Logging::LogBufferOverflowPolicy::DropOldest:
                    _queue[_queueHead].Size = 0U;
                    _queueHead = (_queueHead + 1U) % QueueCapacity;
                    --_queueSize;
                    NoteDroppedLocked(1U);
                    break;
                case Logging::LogBufferOverflowPolicy::DropNewest:
                case Logging::LogBufferOverflowPolicy::Reject:
                    NoteDroppedLocked(1U);
                    return;
                case Logging::LogBufferOverflowPolicy::Block:
                    // Block is rejected during Initialize(); this is a defensive no-wait fallback.
                    NoteDroppedLocked(1U);
                    return;
            }
        }

        auto& destination = _queue[_queueTail];
        std::memcpy(destination.Bytes.data(), writer.Data(), writer.Size());
        destination.Size = writer.Size();
        _queueTail = (_queueTail + 1U) % QueueCapacity;
        ++_queueSize;
        _acceptedRecords.fetch_add(1U, std::memory_order_relaxed);
        UpdateQueueHighWaterMark(_queueSize);
    }

    /// <summary>
    /// Flushes at most one synthetic drop notice or one queued record. Filesystem I/O occurs only in this method.
    /// </summary>
    StorageStatus FlushOne() noexcept {
        if (!IsInitialized()) return SetStatus(StorageStatus::NotInitialized);
        if (_maintenance.load(std::memory_order_acquire)) return SetStatus(StorageStatus::Busy);

        std::lock_guard<std::mutex> flushLock(_flushMutex);
        if (!IsInitialized()) return SetStatus(StorageStatus::NotInitialized);
        if (_maintenance.load(std::memory_order_acquire)) return SetStatus(StorageStatus::Busy);

        std::uint64_t droppedNotice = 0U;
        bool hasRecord = false;
        std::size_t recordSize = 0U;
        {
            std::lock_guard<std::mutex> queueLock(_queueMutex);
            if (_pendingDropNotice != 0U) {
                droppedNotice = _pendingDropNotice;
                _pendingDropNotice = 0U;
            } else if (_queueSize != 0U) {
                auto& source = _queue[_queueHead];
                recordSize = source.Size;
                if (recordSize != 0U) {
                    std::memcpy(_flushBuffer.data(), source.Bytes.data(), recordSize);
                    hasRecord = true;
                }
                source.Size = 0U;
                _queueHead = (_queueHead + 1U) % QueueCapacity;
                --_queueSize;
            } else {
                return SetStatus(StorageStatus::Success);
            }
        }

        if (droppedNotice != 0U) {
            const int length = std::snprintf(
                reinterpret_cast<char*>(_flushBuffer.data()),
                _flushBuffer.size(),
                "[PERSISTENT_LOG] dropped_entries=%llu\n",
                static_cast<unsigned long long>(droppedNotice));
            if (length <= 0 || static_cast<std::size_t>(length) >= _flushBuffer.size()) {
                RestoreDropNotice(droppedNotice);
                return SetStatus(StorageStatus::NoSpace);
            }
            recordSize = static_cast<std::size_t>(length);
            hasRecord = true;
        }

        if (!hasRecord || recordSize == 0U) return SetStatus(StorageStatus::Success);

        StorageStatus status = StorageStatus::UnknownError;
        {
            std::lock_guard<std::mutex> storageLock(_storageMutex);
            if (_storage == nullptr || !_storage->IsReady()) {
                status = StorageStatus::NotInitialized;
            } else {
                status = AppendDurable(_flushBuffer.data(), recordSize);
            }
        }

        if (status == StorageStatus::Success) {
            _bytesWritten.fetch_add(recordSize, std::memory_order_relaxed);
            return SetStatus(status);
        }

        _writeFailures.fetch_add(1U, std::memory_order_relaxed);
        if (droppedNotice != 0U) {
            RestoreDropNotice(droppedNotice);
        } else {
            _droppedRecords.fetch_add(1U, std::memory_order_relaxed);
            RestoreDropNotice(1U);
        }
        return SetStatus(status);
    }

    /// <summary>
    /// Performs bounded owner-context storage work. A value of zero performs no work.
    /// </summary>
    StorageStatus Flush(std::size_t maximumWorkItems = 1U) noexcept {
        if (maximumWorkItems == 0U) return StorageStatus::Success;
        StorageStatus status = StorageStatus::Success;
        for (std::size_t index = 0U; index < maximumWorkItems; ++index) {
            if (!HasPendingWork()) break;
            status = FlushOne();
            if (status != StorageStatus::Success) break;
        }
        return status;
    }

    /// <summary>
    /// Deletes retained files and queued records as one maintenance operation. Lifetime statistics are preserved.
    /// </summary>
    StorageStatus Clear() noexcept {
        if (!BeginMaintenance()) return SetStatus(StorageStatus::Busy);
        MaintenanceGuard maintenanceGuard(_maintenance);
        std::lock_guard<std::mutex> flushLock(_flushMutex);
        if (!IsInitialized() || _storage == nullptr || !_storage->IsReady()) {
            return SetStatus(StorageStatus::NotInitialized);
        }

        {
            std::lock_guard<std::mutex> queueLock(_queueMutex);
            for (auto& record : _queue) record.Size = 0U;
            _queueHead = 0U;
            _queueTail = 0U;
            _queueSize = 0U;
            _pendingDropNotice = 0U;
        }

        std::lock_guard<std::mutex> storageLock(_storageMutex);
        for (std::size_t generation = 0U; generation < FileCount(); ++generation) {
            const auto status = RemoveGeneration(generation);
            if (status != StorageStatus::Success) return SetStatus(status);
        }
        return SetStatus(StorageStatus::Success);
    }

    /// <summary>
    /// Visits persisted bytes oldest-generation first. Storage mutation is paused for the snapshot, but Accept()
    /// remains non-blocking and continues to enqueue records in RAM. Slow callbacks therefore do not hold the queue
    /// mutex or impose filesystem latency on logging callers; queue overflow remains governed by the configured
    /// LogBufferOverflowPolicy and is reported by the next synthetic drop notice.
    /// </summary>
    StorageStatus VisitRetained(ReadCallback callback, void* context = nullptr) const noexcept {
        if (callback == nullptr) return StorageStatus::InvalidArgument;
        if (!BeginMaintenance()) return SetStatus(StorageStatus::Busy);
        MaintenanceGuard maintenanceGuard(_maintenance);
        std::lock_guard<std::mutex> flushLock(_flushMutex);
        if (!IsInitialized() || _storage == nullptr || !_storage->IsReady()) {
            return SetStatus(StorageStatus::NotInitialized);
        }

        std::array<bool, MaximumFiles> exists{};
        std::array<std::uint64_t, MaximumFiles> sizes{};
        {
            std::lock_guard<std::mutex> storageLock(_storageMutex);
            exists = _exists;
            sizes = _sizes;
        }

        std::array<std::uint8_t, 256U> chunk{};
        for (std::size_t remaining = FileCount(); remaining > 0U; --remaining) {
            const std::size_t generation = remaining - 1U;
            if (!exists[generation]) continue;
            char path[StorageEntry::MaximumPathLength]{};
            if (!BuildPath(generation, path, sizeof(path))) return SetStatus(StorageStatus::InvalidArgument);
            std::uint64_t offset = 0U;
            while (offset < sizes[generation]) {
                const auto available = sizes[generation] - offset;
                const std::size_t requested = available < chunk.size()
                    ? static_cast<std::size_t>(available)
                    : chunk.size();
                std::size_t bytesRead = 0U;
                const auto status = _storage->Read(path, offset, chunk.data(), requested, bytesRead);
                if (status != StorageStatus::Success) return SetStatus(status);
                if (bytesRead == 0U) return SetStatus(StorageStatus::IoError);
                if (!callback(chunk.data(), bytesRead, context)) return SetStatus(StorageStatus::Success);
                offset += bytesRead;
            }
        }
        return SetStatus(StorageStatus::Success);
    }

    std::size_t GenerationCapacity() const noexcept { return FileCount(); }

    bool GenerationExists(std::size_t generation) const noexcept {
        std::lock_guard<std::mutex> storageLock(_storageMutex);
        return generation < FileCount() && _exists[generation];
    }

    std::uint64_t GenerationSize(std::size_t generation) const noexcept {
        std::lock_guard<std::mutex> storageLock(_storageMutex);
        return generation < FileCount() && _exists[generation] ? _sizes[generation] : 0U;
    }

    std::uint64_t RetainedBytes() const noexcept {
        std::lock_guard<std::mutex> storageLock(_storageMutex);
        return _retainedBytes;
    }

    std::size_t QueuedRecords() const noexcept {
        std::lock_guard<std::mutex> queueLock(_queueMutex);
        return _queueSize;
    }

    bool HasPendingWork() const noexcept {
        std::lock_guard<std::mutex> queueLock(_queueMutex);
        return _queueSize != 0U || _pendingDropNotice != 0U;
    }

    Logging::LogSinkStatistics GetStatistics() const noexcept {
        Logging::LogSinkStatistics statistics{};
        statistics.AcceptedRecords = _acceptedRecords.load(std::memory_order_relaxed);
        statistics.FilteredRecords = _filteredRecords.load(std::memory_order_relaxed);
        statistics.DroppedRecords = _droppedRecords.load(std::memory_order_relaxed);
        statistics.WriteFailures = _writeFailures.load(std::memory_order_relaxed);
        statistics.BytesWritten = _bytesWritten.load(std::memory_order_relaxed);
        statistics.QueueHighWaterMark = _queueHighWaterMark.load(std::memory_order_relaxed);
        return statistics;
    }

    StorageStatus GetLastStorageStatus() const noexcept {
        return _lastStatus.load(std::memory_order_relaxed);
    }

private:
/**
 * ESPressio Memory Audit
 * Members:
 * - Bytes (std::array<std::uint8_t, MaximumRecordBytes>): MaximumRecordBytes * (1 bytes) [0 bytes dynamic allocation]
 * - Size (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 4 bytes known/aligned storage + MaximumRecordBytes * (1 bytes) [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
struct QueuedRecord final {
        std::array<std::uint8_t, MaximumRecordBytes> Bytes{};
        std::size_t Size{0U};
    };

/**
 * ESPressio Memory Audit
 * Members:
 * - _flag (std::atomic<bool>&): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class MaintenanceGuard final {
    public:
        explicit MaintenanceGuard(std::atomic<bool>& flag) noexcept : _flag(flag) {}
        ~MaintenanceGuard() { _flag.store(false, std::memory_order_release); }
        MaintenanceGuard(const MaintenanceGuard&) = delete;
        MaintenanceGuard& operator=(const MaintenanceGuard&) = delete;
    private:
        std::atomic<bool>& _flag;
    };

/**
 * ESPressio Memory Audit
 * Members:
 * - _buffer (std::array<std::uint8_t, MaximumRecordBytes>&): 4 bytes [0 bytes dynamic allocation]
 * - _size (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 8 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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
            _policy.FileCapacity.Bytes < MaximumDropNoticeBytes ||
            _policy.TotalCapacity.Bytes < _policy.FileCapacity.Bytes ||
            _overflowPolicy == Logging::LogBufferOverflowPolicy::Block) return false;
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

    bool BeginMaintenance() const noexcept {
        bool expected = false;
        return _maintenance.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    void ResetDiscoveredState() noexcept {
        _exists.fill(false);
        _sizes.fill(0U);
        _retainedBytes = 0U;
    }

    void UpdateQueueHighWaterMark(std::size_t value) noexcept {
        auto current = _queueHighWaterMark.load(std::memory_order_relaxed);
        while (value > current &&
               !_queueHighWaterMark.compare_exchange_weak(
                   current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }

    void NoteDroppedLocked(std::uint64_t count) noexcept {
        _droppedRecords.fetch_add(count, std::memory_order_relaxed);
        if (std::numeric_limits<std::uint64_t>::max() - _pendingDropNotice < count) {
            _pendingDropNotice = std::numeric_limits<std::uint64_t>::max();
        } else {
            _pendingDropNotice += count;
        }
    }

    void RestoreDropNotice(std::uint64_t count) noexcept {
        std::lock_guard<std::mutex> queueLock(_queueMutex);
        if (std::numeric_limits<std::uint64_t>::max() - _pendingDropNotice < count) {
            _pendingDropNotice = std::numeric_limits<std::uint64_t>::max();
        } else {
            _pendingDropNotice += count;
        }
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
            // Historical generations are removed before the active generation. Because TotalCapacity is required
            // to be >= FileCapacity, a record that fits the active file cannot require deleting generation zero here.
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

    StorageStatus AppendDurable(const std::uint8_t* data, std::size_t size) noexcept {
        if (data == nullptr || size == 0U || size > FileCapacity() || size > TotalCapacity()) {
            return StorageStatus::InvalidArgument;
        }
        if (_exists[0U] && _sizes[0U] + size > FileCapacity()) {
            const auto rotated = Rotate();
            if (rotated != StorageStatus::Success) return rotated;
        }
        const auto pruned = PruneForAppend(size);
        if (pruned != StorageStatus::Success) return pruned;

        char path[StorageEntry::MaximumPathLength]{};
        if (!BuildPath(0U, path, sizeof(path))) return StorageStatus::InvalidArgument;
        const auto status = _storage->Write(path, data, size, WriteMode::Append);
        if (status != StorageStatus::Success) return status;

        _exists[0U] = true;
        _sizes[0U] += size;
        _retainedBytes += size;
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
    std::array<QueuedRecord, QueueCapacity> _queue{};
    std::array<std::uint8_t, MaximumRecordBytes> _encodeBuffer{};
    std::array<std::uint8_t, MaximumRecordBytes> _flushBuffer{};
    std::array<bool, MaximumFiles> _exists{};
    std::array<std::uint64_t, MaximumFiles> _sizes{};
    std::atomic<Logging::LogLevelMask> _levelMask{Logging::AllLogLevels};
    Logging::LogBufferOverflowPolicy _overflowPolicy{Logging::LogBufferOverflowPolicy::DropOldest};
    std::atomic<bool> _initialized{false};
    mutable std::atomic<bool> _maintenance{false};
    mutable std::atomic<StorageStatus> _lastStatus{StorageStatus::NotInitialized};
    mutable std::mutex _queueMutex;
    mutable std::mutex _storageMutex;
    mutable std::mutex _flushMutex;
    std::size_t _queueHead{0U};
    std::size_t _queueTail{0U};
    std::size_t _queueSize{0U};
    std::uint64_t _pendingDropNotice{0U};
    std::uint64_t _retainedBytes{0U};
    std::atomic<std::uint64_t> _acceptedRecords{0U};
    std::atomic<std::uint64_t> _filteredRecords{0U};
    std::atomic<std::uint64_t> _droppedRecords{0U};
    std::atomic<std::uint64_t> _writeFailures{0U};
    std::atomic<std::uint64_t> _bytesWritten{0U};
    std::atomic<std::size_t> _queueHighWaterMark{0U};
};

} // namespace ESPressio::Persistence
