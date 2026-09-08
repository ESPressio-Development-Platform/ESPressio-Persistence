#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <ESPressio_ILogSink.hpp>
#include <ESPressio_TaskExecutor.hpp>

#include "ESPressio_IPersistentLogWorkTarget.hpp"

namespace ESPressio::Persistence {

/// <summary>Controls how queued sink-owned records are treated when the persistent logging worker is stopped.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum class PersistentLogWorkerStopMode : std::uint8_t {
    /// <summary>Stop storage execution immediately; queued records remain owned by the sink.</summary>
    PreserveQueued,
    /// <summary>Perform one caller-requested bounded final drain before stopping the storage task.</summary>
    DrainBounded
};

/// <summary>Configuration for the dedicated persistent-log storage task.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - TaskConfiguration (Task::TaskConfiguration): 24 bytes [0 bytes dynamic allocation]
 * - FlushQuantum (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - ShutdownDrainMaximumItems (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 32 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct PersistentLogWorkerConfiguration final {
    Task::TaskConfiguration TaskConfiguration{};
    /// <summary>Maximum sink work items written by one worker wake.</summary>
    std::size_t FlushQuantum{2U};
    /// <summary>Maximum sink work items written by DrainBounded shutdown.</summary>
    std::size_t ShutdownDrainMaximumItems{16U};

    PersistentLogWorkerConfiguration() noexcept {
        TaskConfiguration.Name = "persistentLog";
        TaskConfiguration.StackSize = 4096U;
        // Deliberately below ordinary Thread/RadioWorker priority 2 and RadioControlWorker priority 4.
        TaskConfiguration.Priority = 1U;
        TaskConfiguration.Core = -1;
        // This is a coalesced wake-token queue, never a second log-record queue.
        TaskConfiguration.QueueDepth = 1U;
        TaskConfiguration.OverflowPolicy = Task::TaskQueueOverflowPolicy::Reject;
        TaskConfiguration.MemoryPolicy = Task::TaskMemoryPolicy::PreferExternal;
    }
};

/// <summary>Snapshot of persistent-log worker scheduling/storage activity.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - WorkSignals (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - WakeTokensQueued (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - WakeSignalsCoalesced (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - FlushPasses (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - FlushFailures (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - LastFlushStatus (StorageStatus): 1 bytes [0 bytes dynamic allocation]
 * - TaskStatistics (Task::TaskExecutionStatistics): 40 bytes [0 bytes dynamic allocation]
 * Total Memory: 64 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct PersistentLogWorkerStatistics final {
    std::uint32_t WorkSignals{0U};
    std::uint32_t WakeTokensQueued{0U};
    std::uint32_t WakeSignalsCoalesced{0U};
    std::uint32_t FlushPasses{0U};
    std::uint32_t FlushFailures{0U};
    StorageStatus LastFlushStatus{StorageStatus::NotInitialized};
    Task::TaskExecutionStatistics TaskStatistics{};
};

/// <summary>
/// Presents an existing bounded persistent sink as an ILogSink while moving all routine filesystem work onto a dedicated
/// ESPressio-Task execution context.
/// </summary>
/// <typeparam name="TPersistentSink">
/// Sink type implementing ILogSink plus Flush(maximumWorkItems) and HasPendingWork(). PersistentLogSink is the canonical
/// implementation.
/// </typeparam>
/// <remarks>
/// The wrapped sink remains the sole owner of encoded log records, queue capacity, overflow policy and drop accounting.
/// This worker owns only a one-token coalescing wake queue. Accept() delegates record materialisation to the sink and then
/// performs an atomic wake coalescing operation; it never performs filesystem I/O.
///
/// A successful worker pass writes at most FlushQuantum sink work items. If more work remains it schedules one further
/// wake and returns through the task scheduler, preventing storage backlog from becoming a drain-until-empty monopolizing
/// loop. Storage Busy/error status intentionally does not self-spin; the next producer signal or RequestFlush() retries.
///
/// Register this worker facade with Logger rather than registering the wrapped sink directly. Direct calls to the sink's
/// Accept() bypass worker signalling by design.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 8 bytes [0 bytes dynamic allocation]
 * Members:
 * - _sink (TPersistentSink*): 4 bytes [0 bytes dynamic allocation]
 * - _configuration (PersistentLogWorkerConfiguration): 32 bytes [0 bytes dynamic allocation]
 * - _executor (Task::TaskExecutor<std::uint8_t>): 160 bytes [_handler: Name: Capacity + 1 bytes when capacity exceeds 15-byte SSO; _handler: LastId: Capacity + 1 bytes when capacity exceeds 15-byte SSO; _queue: owned object: 4 bytes; _startGate: owned object: 4 bytes; _lifecycleMutex: _owned: owned object: 4 bytes; _lifecycleMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * - _running (std::atomic<bool>): 1 bytes [0 bytes dynamic allocation]
 * - _wakeOutstanding (std::atomic<bool>): 1 bytes [0 bytes dynamic allocation]
 * - _workSignals (std::atomic<std::uint32_t>): 4 bytes [0 bytes dynamic allocation]
 * - _wakeTokensQueued (std::atomic<std::uint32_t>): 4 bytes [0 bytes dynamic allocation]
 * - _wakeSignalsCoalesced (std::atomic<std::uint32_t>): 4 bytes [0 bytes dynamic allocation]
 * - _flushPasses (std::atomic<std::uint32_t>): 4 bytes [0 bytes dynamic allocation]
 * - _flushFailures (std::atomic<std::uint32_t>): 4 bytes [0 bytes dynamic allocation]
 * - _lastFlushStatus (std::atomic<StorageStatus>): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 232 bytes [_executor: _handler: Name: Capacity + 1 bytes when capacity exceeds 15-byte SSO; _executor: _handler: LastId: Capacity + 1 bytes when capacity exceeds 15-byte SSO; _executor: _queue: owned object: 4 bytes; _executor: _startGate: owned object: 4 bytes; _executor: _lifecycleMutex: _owned: owned object: 4 bytes; _executor: _lifecycleMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
template<typename TPersistentSink>
class PersistentLogWorker final
    : public Logging::ILogSink,
      public IPersistentLogWorkSignal {
public:
    explicit PersistentLogWorker(
        TPersistentSink& sink,
        PersistentLogWorkerConfiguration configuration = {}
    ) :
        _sink(&sink),
        _configuration(configuration),
        _executor(configuration.TaskConfiguration) {}

    ~PersistentLogWorker() override {
        Stop(PersistentLogWorkerStopMode::PreserveQueued);
    }

    PersistentLogWorker(const PersistentLogWorker&) = delete;
    PersistentLogWorker& operator=(const PersistentLogWorker&) = delete;
    PersistentLogWorker(PersistentLogWorker&&) = delete;
    PersistentLogWorker& operator=(PersistentLogWorker&&) = delete;

    Task::TaskExecutionStatus Initialize() {
        if (_sink == nullptr || _configuration.FlushQuantum == 0U) {
            return Task::TaskExecutionStatus::InvalidConfiguration;
        }
        return _executor.Initialize([this](const std::uint8_t&) { ServiceWake(); });
    }

    Task::TaskExecutionStatus Start() {
        const auto status = _executor.Start();
        if (status == Task::TaskExecutionStatus::Success ||
            status == Task::TaskExecutionStatus::AlreadyStarted) {
            _running.store(true, std::memory_order_release);
            if (_sink != nullptr && _sink->HasPendingWork()) RequestFlush();
        }
        return status;
    }

    /// <summary>
    /// Stops the dedicated task. DrainBounded may perform the explicitly bounded final storage drain on the caller;
    /// routine operation never performs storage on the caller thread.
    /// </summary>
    void Stop(PersistentLogWorkerStopMode mode = PersistentLogWorkerStopMode::PreserveQueued) noexcept {
        if (!_running.exchange(false, std::memory_order_acq_rel)) {
            _executor.Stop();
            _wakeOutstanding.store(false, std::memory_order_release);
            return;
        }

        _executor.Stop();
        _wakeOutstanding.store(false, std::memory_order_release);

        if (mode == PersistentLogWorkerStopMode::DrainBounded &&
            _sink != nullptr &&
            _configuration.ShutdownDrainMaximumItems != 0U) {
            (void)_sink->Flush(_configuration.ShutdownDrainMaximumItems);
        }
    }

    bool IsRunning() const noexcept {
        return _running.load(std::memory_order_acquire);
    }

    /// <summary>Explicitly requests storage service; duplicate requests are atomically coalesced.</summary>
    void RequestFlush() noexcept {
        OnPersistentLogWorkAvailable();
    }

    void OnPersistentLogWorkAvailable() noexcept override {
        _workSignals.fetch_add(1U, std::memory_order_relaxed);
        if (!_running.load(std::memory_order_acquire)) return;

        bool expected = false;
        if (!_wakeOutstanding.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            _wakeSignalsCoalesced.fetch_add(1U, std::memory_order_relaxed);
            return;
        }

        const std::uint8_t token = 1U;
        const auto status = _executor.Submit(token, 0U);
        if (status == Task::TaskExecutionStatus::Success) {
            _wakeTokensQueued.fetch_add(1U, std::memory_order_relaxed);
            return;
        }

        // A failed one-token submission must not permanently suppress future wake attempts.
        _wakeOutstanding.store(false, std::memory_order_release);
        _wakeSignalsCoalesced.fetch_add(1U, std::memory_order_relaxed);
    }

    bool IsEnabled(Logging::LogLevel level, const Logging::LogCategory& category) const noexcept override {
        return _sink != nullptr && _sink->IsEnabled(level, category);
    }

    /// <summary>Copies one record into the sink-owned bounded RAM queue and coalesces one worker wake.</summary>
    void Accept(const Logging::LogRecordLease& record) noexcept override {
        if (_sink == nullptr) return;
        _sink->Accept(record);
        OnPersistentLogWorkAvailable();
    }

    PersistentLogWorkerStatistics GetStatistics() const noexcept {
        return {
            _workSignals.load(std::memory_order_relaxed),
            _wakeTokensQueued.load(std::memory_order_relaxed),
            _wakeSignalsCoalesced.load(std::memory_order_relaxed),
            _flushPasses.load(std::memory_order_relaxed),
            _flushFailures.load(std::memory_order_relaxed),
            _lastFlushStatus.load(std::memory_order_relaxed),
            _executor.GetStatistics()
        };
    }

    TPersistentSink& Sink() noexcept { return *_sink; }
    const TPersistentSink& Sink() const noexcept { return *_sink; }

private:
    void ServiceWake() noexcept {
        if (_sink == nullptr || !_running.load(std::memory_order_acquire)) {
            _wakeOutstanding.store(false, std::memory_order_release);
            return;
        }

        const auto status = _sink->Flush(_configuration.FlushQuantum);
        _flushPasses.fetch_add(1U, std::memory_order_relaxed);
        _lastFlushStatus.store(status, std::memory_order_relaxed);
        if (status != StorageStatus::Success) {
            _flushFailures.fetch_add(1U, std::memory_order_relaxed);
        }

        // Publish that this token has been consumed before examining remaining work. A producer racing after this store
        // either queues the continuation itself or leaves _wakeOutstanding true for the continuation below to observe.
        _wakeOutstanding.store(false, std::memory_order_release);

        if (status == StorageStatus::Success &&
            _running.load(std::memory_order_acquire) &&
            _sink->HasPendingWork()) {
            RequestFlush();
        }
    }

    TPersistentSink* _sink{nullptr};
    PersistentLogWorkerConfiguration _configuration{};
    Task::TaskExecutor<std::uint8_t> _executor;
    std::atomic<bool> _running{false};
    std::atomic<bool> _wakeOutstanding{false};
    std::atomic<std::uint32_t> _workSignals{0U};
    std::atomic<std::uint32_t> _wakeTokensQueued{0U};
    std::atomic<std::uint32_t> _wakeSignalsCoalesced{0U};
    std::atomic<std::uint32_t> _flushPasses{0U};
    std::atomic<std::uint32_t> _flushFailures{0U};
    std::atomic<StorageStatus> _lastFlushStatus{StorageStatus::NotInitialized};
};

} // namespace ESPressio::Persistence
