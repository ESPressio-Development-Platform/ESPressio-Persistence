#pragma once

#include <cstddef>

#include <ESPressio_PersistenceTypes.hpp>

namespace ESPressio::Persistence {

/// <summary>
/// Receives a coalescible, non-blocking indication that a persistent logging target owns storage work.
/// </summary>
/// <remarks>
/// Implementations must never perform filesystem I/O inline. The signal may be raised from latency-sensitive logging
/// callers after a record has been copied into bounded sink-owned RAM. Its only responsibility is to wake or enqueue a
/// separate storage execution context.
/// </remarks>

class IPersistentLogWorkSignal {
public:
    virtual ~IPersistentLogWorkSignal() = default;
    virtual void OnPersistentLogWorkAvailable() noexcept = 0;
};

/// <summary>
/// Bounded persistent-log storage work surface consumed by a dedicated worker.
/// </summary>
/// <remarks>
/// A target remains the sole owner of log records and backpressure/drop accounting. Workers schedule bounded calls to
/// Flush(); they do not introduce a second log-record queue.
/// </remarks>

class IPersistentLogWorkTarget {
public:
    virtual ~IPersistentLogWorkTarget() = default;

    /// <summary>Performs at most the requested number of storage work items.</summary>
    virtual StorageStatus Flush(std::size_t maximumWorkItems) noexcept = 0;

    /// <summary>Returns whether queued records or synthetic maintenance/drop records remain.</summary>
    virtual bool HasPendingWork() const noexcept = 0;

    /// <summary>Installs or removes the non-owning worker wake target.</summary>
    virtual void SetWorkSignal(IPersistentLogWorkSignal* signal) noexcept = 0;
};

} // namespace ESPressio::Persistence
