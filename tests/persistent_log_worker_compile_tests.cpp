#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_Persistence_Logging.hpp>

using namespace ESPressio;
using namespace ESPressio::Persistence;

namespace {

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - Pending (bool): 1 bytes [0 bytes dynamic allocation]
 * - LastQuantum (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 12 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class CompileOnlyPersistentSink final : public Logging::ILogSink {
public:
    bool IsEnabled(Logging::LogLevel, const Logging::LogCategory&) const noexcept override {
        return true;
    }

    void Accept(const Logging::LogRecordLease&) noexcept override {}

    StorageStatus Flush(std::size_t maximumWorkItems) noexcept {
        LastQuantum = maximumWorkItems;
        Pending = false;
        return StorageStatus::Success;
    }

    bool HasPendingWork() const noexcept { return Pending; }

    bool Pending{true};
    std::size_t LastQuantum{0U};
};

} // namespace

int main() {
    PersistentLogWorkerConfiguration configuration;
    assert(configuration.TaskConfiguration.Priority == 1U);
    assert(configuration.TaskConfiguration.Core == -1);
    assert(configuration.TaskConfiguration.QueueDepth == 1U);
    assert(configuration.TaskConfiguration.OverflowPolicy == Task::TaskQueueOverflowPolicy::Reject);
    assert(configuration.FlushQuantum == 2U);

    CompileOnlyPersistentSink sink;
    PersistentLogWorker<CompileOnlyPersistentSink> worker(sink, configuration);
    assert(&worker.Sink() == &sink);
    assert(!worker.IsRunning());

    // Deliberately do not initialize the task runtime in this host compile contract. Runtime behaviour is exercised by
    // ESP32/Lab composition where the System execution provider is installed. This test protects the optional surface,
    // task priority, one-token queue and template requirements from silently drifting.
    return 0;
}
