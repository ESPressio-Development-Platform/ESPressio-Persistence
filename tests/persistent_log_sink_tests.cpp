#include <ESPressio_Persistence.hpp>
#include <ESPressio_Persistence_Logging.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

#include <ESPressio_LogCategory.hpp>
#include <ESPressio_LogField.hpp>
#include <ESPressio_LogLevel.hpp>
#include <ESPressio_LogPolicies.hpp>
#include <ESPressio_LogRecord.hpp>
#include <ESPressio_SharedLogRecord.hpp>

using namespace ESPressio;

namespace {

Logging::RollingLogPolicy Policy(std::size_t total, std::size_t file, std::size_t count) {
    return {
        Logging::LogByteCapacity::Bounded(total),
        Logging::LogByteCapacity::Bounded(file),
        Logging::LogCountCapacity::Bounded(count)
    };
}

template<std::size_t MaximumRecordBytes, std::size_t MaximumFiles, std::size_t QueueCapacity>
void Submit(
    Persistence::PersistentLogSink<MaximumRecordBytes, MaximumFiles, QueueCapacity>& sink,
    Logging::LogLevel level,
    std::string_view message,
    Logging::LogFieldView fields = {}
) {
    const Logging::LogRecordView view{
        {1234U, 5678U, Timing::ClockSynchronizationState::Synchronized},
        level,
        Logging::LogCategory::Named("PersistentTest"),
        message,
        fields
    };
    sink.Accept(Logging::LogRecordLease(view));
}

bool Collect(const std::uint8_t* data, std::size_t size, void* context) {
    auto& text = *static_cast<std::string*>(context);
    text.append(reinterpret_cast<const char*>(data), size);
    return true;
}

class CountingFileStorage final : public Persistence::IFileStorage {
public:
    Persistence::StorageStatus Initialize() override { return _inner.Initialize(); }
    void Shutdown() override { _inner.Shutdown(); }
    bool IsReady() const override { return _inner.IsReady(); }
    const char* GetBackendName() const override { return "CountingFileStorage"; }
    Persistence::StorageCapability GetCapabilities() const override { return _inner.GetCapabilities(); }
    Persistence::StorageStatistics GetStatistics() const override { return _inner.GetStatistics(); }
    Persistence::StorageStatus Exists(const char* p, bool& e) const override { return _inner.Exists(p, e); }
    Persistence::StorageStatus Stat(const char* p, Persistence::StorageEntry& e) const override { return _inner.Stat(p, e); }
    Persistence::StorageStatus Read(
        const char* p, std::uint64_t o, std::uint8_t* b, std::size_t c, std::size_t& r
    ) const override { return _inner.Read(p, o, b, c, r); }
    Persistence::StorageStatus Write(
        const char* p, const std::uint8_t* d, std::size_t s, Persistence::WriteMode m
    ) override {
        ++Writes;
        return _inner.Write(p, d, s, m);
    }
    Persistence::StorageStatus Remove(const char* p) override { return _inner.Remove(p); }
    Persistence::StorageStatus Rename(const char* from, const char* to) override {
        return _inner.Rename(from, to);
    }
    Persistence::StorageStatus CreateDirectory(const char* p) override { return _inner.CreateDirectory(p); }
    Persistence::StorageStatus RemoveDirectory(const char* p) override { return _inner.RemoveDirectory(p); }
    Persistence::StorageStatus List(
        const char* p, Persistence::StorageListCallback c, void* x
    ) const override { return _inner.List(p, c, x); }

    std::size_t Writes{0U};

private:
    Persistence::MemoryFileStorage _inner;
};

void TestRequiresReadyBoundedCapableStorage() {
    Persistence::MemoryFileStorage storage;
    Persistence::PersistentLogSink<256U, 4U> sink(
        storage, "/logs", "device.log", Policy(512U, 256U, 2U));
    assert(sink.Initialize() == Persistence::StorageStatus::NotInitialized);
    assert(storage.Initialize() == Persistence::StorageStatus::Success);
    assert(sink.Initialize() == Persistence::StorageStatus::Success);
    assert(sink.Initialize() == Persistence::StorageStatus::AlreadyInitialized);

    Persistence::PersistentLogSink<256U, 4U> blocking(
        storage, "/blocking", "device.log", Policy(512U, 256U, 2U),
        Logging::AllLogLevels, Logging::LogBufferOverflowPolicy::Block);
    assert(blocking.Initialize() == Persistence::StorageStatus::InvalidArgument);

    // A configuration must always be capable of persisting the worst-case synthetic loss notice. Otherwise a
    // saturated queue could create a pending diagnostic record that can never be drained successfully.
    Persistence::PersistentLogSink<256U, 4U> undersizedLossNotice(
        storage, "/undersized", "device.log", Policy(106U, 53U, 2U));
    assert(undersizedLossNotice.Initialize() == Persistence::StorageStatus::InvalidArgument);
}

void TestAcceptDoesNotPerformStorageIO() {
    CountingFileStorage storage;
    assert(storage.Initialize() == Persistence::StorageStatus::Success);
    Persistence::PersistentLogSink<256U, 2U, 4U> sink(
        storage, "/logs", "device.log", Policy(512U, 256U, 2U));
    assert(sink.Initialize() == Persistence::StorageStatus::Success);

    Submit(sink, Logging::LogLevel::Info, "caller-thread-safe");
    assert(storage.Writes == 0U);
    assert(sink.QueuedRecords() == 1U);
    assert(sink.RetainedBytes() == 0U);

    assert(sink.FlushOne() == Persistence::StorageStatus::Success);
    assert(storage.Writes == 1U);
    assert(sink.QueuedRecords() == 0U);
    assert(sink.RetainedBytes() > 0U);
}

void TestFormattingEscapingFilteringAndClear() {
    Persistence::MemoryFileStorage storage;
    assert(storage.Initialize() == Persistence::StorageStatus::Success);
    Persistence::PersistentLogSink<256U, 4U> sink(
        storage, "/logs", "device.log", Policy(768U, 256U, 3U),
        Logging::AtOrAbove(Logging::LogLevel::Info));
    assert(sink.Initialize() == Persistence::StorageStatus::Success);

    const Logging::LogField fields[] = {
        {"count", std::uint32_t{7U}},
        {"detail", std::string_view("line\nvalue")}
    };
    Submit(sink, Logging::LogLevel::Info, "hello\nworld", Logging::Fields(fields));
    Submit(sink, Logging::LogLevel::Debug, "filtered");

    const auto queuedStats = sink.GetStatistics();
    assert(queuedStats.AcceptedRecords == 1U);
    assert(queuedStats.FilteredRecords == 1U);
    assert(sink.RetainedBytes() == 0U);
    assert(sink.Flush(4U) == Persistence::StorageStatus::Success);

    std::string retained;
    assert(sink.VisitRetained(Collect, &retained) == Persistence::StorageStatus::Success);
    assert(retained.find("[INFO] [PersistentTest] hello\\nworld") != std::string::npos);
    assert(retained.find("count=7") != std::string::npos);
    assert(retained.find("detail=line\\nvalue") != std::string::npos);
    assert(retained.find("filtered") == std::string::npos);

    Submit(sink, Logging::LogLevel::Info, "queued-before-clear");
    assert(sink.QueuedRecords() == 1U);
    assert(sink.Clear() == Persistence::StorageStatus::Success);
    assert(sink.QueuedRecords() == 0U);
    assert(sink.RetainedBytes() == 0U);
    retained.clear();
    assert(sink.VisitRetained(Collect, &retained) == Persistence::StorageStatus::Success);
    assert(retained.empty());
}

void TestRotationCapacityAndRestartDiscovery() {
    Persistence::MemoryFileStorage storage;
    assert(storage.Initialize() == Persistence::StorageStatus::Success);
    const auto policy = Policy(420U, 150U, 3U);

    {
        Persistence::PersistentLogSink<256U, 4U, 16U> sink(storage, "/logs", "device.log", policy);
        assert(sink.Initialize() == Persistence::StorageStatus::Success);
        for (std::uint32_t index = 0U; index < 12U; ++index) {
            const Logging::LogField field[] = {{"sequence", index}};
            Submit(sink, Logging::LogLevel::Info, "rotation-record", Logging::Fields(field));
        }
        assert(sink.QueuedRecords() == 12U);
        assert(sink.Flush(16U) == Persistence::StorageStatus::Success);
        assert(sink.QueuedRecords() == 0U);
        assert(sink.RetainedBytes() <= 420U);
        assert(sink.GenerationExists(0U));
        assert(sink.GenerationSize(0U) <= 150U);
        assert(sink.GetStatistics().AcceptedRecords == 12U);
        sink.Shutdown();
    }

    Persistence::PersistentLogSink<256U, 4U, 16U> restarted(storage, "/logs", "device.log", policy);
    assert(restarted.Initialize() == Persistence::StorageStatus::Success);
    assert(restarted.RetainedBytes() > 0U && restarted.RetainedBytes() <= 420U);
    assert(restarted.GenerationExists(0U));

    std::string retained;
    assert(restarted.VisitRetained(Collect, &retained) == Persistence::StorageStatus::Success);
    assert(retained.find("rotation-record") != std::string::npos);
}

void TestQueueOverflowProducesSyntheticNotice() {
    Persistence::MemoryFileStorage storage;
    assert(storage.Initialize() == Persistence::StorageStatus::Success);
    Persistence::PersistentLogSink<192U, 2U, 2U> sink(
        storage, "/logs", "device.log", Policy(512U, 256U, 2U));
    assert(sink.Initialize() == Persistence::StorageStatus::Success);

    Submit(sink, Logging::LogLevel::Info, "first");
    Submit(sink, Logging::LogLevel::Info, "second");
    Submit(sink, Logging::LogLevel::Info, "third");

    const auto stats = sink.GetStatistics();
    assert(stats.AcceptedRecords == 3U);
    assert(stats.DroppedRecords == 1U);
    assert(stats.QueueHighWaterMark == 2U);
    assert(sink.QueuedRecords() == 2U);

    // First work item is the synthetic loss notice, followed by the two surviving records.
    assert(sink.Flush(4U) == Persistence::StorageStatus::Success);
    std::string retained;
    assert(sink.VisitRetained(Collect, &retained) == Persistence::StorageStatus::Success);
    assert(retained.find("dropped_entries=1") != std::string::npos);
    assert(retained.find("first") == std::string::npos);
    assert(retained.find("second") != std::string::npos);
    assert(retained.find("third") != std::string::npos);
}

void TestVisitAllowsConcurrentQueueAdmission() {
    Persistence::MemoryFileStorage storage;
    assert(storage.Initialize() == Persistence::StorageStatus::Success);
    Persistence::PersistentLogSink<192U, 2U, 4U> sink(
        storage, "/logs", "device.log", Policy(512U, 256U, 2U));
    assert(sink.Initialize() == Persistence::StorageStatus::Success);
    Submit(sink, Logging::LogLevel::Info, "persisted-before-visit");
    assert(sink.FlushOne() == Persistence::StorageStatus::Success);

    struct VisitContext {
        Persistence::PersistentLogSink<192U, 2U, 4U>* Sink;
        std::string Text;
        bool Submitted{false};
    } context{&sink};

    const auto visitor = [](const std::uint8_t* data, std::size_t size, void* opaque) -> bool {
        auto& state = *static_cast<VisitContext*>(opaque);
        state.Text.append(reinterpret_cast<const char*>(data), size);
        if (!state.Submitted) {
            Submit(*state.Sink, Logging::LogLevel::Info, "queued-during-visit");
            state.Submitted = true;
        }
        return true;
    };

    assert(sink.VisitRetained(visitor, &context) == Persistence::StorageStatus::Success);
    assert(context.Text.find("persisted-before-visit") != std::string::npos);
    assert(context.Text.find("queued-during-visit") == std::string::npos);
    assert(sink.QueuedRecords() == 1U);
    assert(sink.FlushOne() == Persistence::StorageStatus::Success);
}

void TestOversizedRecordIsDroppedWithoutPartialWrite() {
    Persistence::MemoryFileStorage storage;
    assert(storage.Initialize() == Persistence::StorageStatus::Success);
    Persistence::PersistentLogSink<96U, 2U> sink(
        storage, "/logs", "device.log", Policy(256U, 128U, 2U));
    assert(sink.Initialize() == Persistence::StorageStatus::Success);

    Submit(sink, Logging::LogLevel::Info,
           "this message is intentionally much longer than the bounded encoded record buffer and must never be partially persisted");
    assert(sink.GetStatistics().DroppedRecords == 1U);
    assert(sink.QueuedRecords() == 0U);
    assert(sink.RetainedBytes() == 0U);
}

} // namespace

int main() {
    TestRequiresReadyBoundedCapableStorage();
    TestAcceptDoesNotPerformStorageIO();
    TestFormattingEscapingFilteringAndClear();
    TestRotationCapacityAndRestartDiscovery();
    TestQueueOverflowProducesSyntheticNotice();
    TestVisitAllowsConcurrentQueueAdmission();
    TestOversizedRecordIsDroppedWithoutPartialWrite();
    return 0;
}
