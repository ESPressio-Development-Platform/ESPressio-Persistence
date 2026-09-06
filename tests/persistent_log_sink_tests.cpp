#include <ESPressio_Persistence.hpp>
#include <ESPressio_Persistence_Logging.hpp>

#include <array>
#include <cassert>
#include <cstdint>
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

template<std::size_t MaximumRecordBytes, std::size_t MaximumFiles>
void Submit(
    Persistence::PersistentLogSink<MaximumRecordBytes, MaximumFiles>& sink,
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

void TestRequiresReadyBoundedCapableStorage() {
    Persistence::MemoryFileStorage storage;
    Persistence::PersistentLogSink<256U, 4U> sink(
        storage, "/logs", "device.log", Policy(512U, 256U, 2U));
    assert(sink.Initialize() == Persistence::StorageStatus::NotInitialized);
    assert(storage.Initialize() == Persistence::StorageStatus::Success);
    assert(sink.Initialize() == Persistence::StorageStatus::Success);
    assert(sink.Initialize() == Persistence::StorageStatus::AlreadyInitialized);
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

    const auto stats = sink.GetStatistics();
    assert(stats.AcceptedRecords == 1U);
    // Normal Logger routing would avoid calling Accept for a disabled level. Calling Accept directly
    // here deliberately verifies the Sink's defensive filter path as well.
    assert(stats.FilteredRecords == 1U);

    std::string retained;
    assert(sink.VisitRetained(Collect, &retained) == Persistence::StorageStatus::Success);
    assert(retained.find("[INFO] [PersistentTest] hello\\nworld") != std::string::npos);
    assert(retained.find("count=7") != std::string::npos);
    assert(retained.find("detail=line\\nvalue") != std::string::npos);
    assert(retained.find("filtered") == std::string::npos);

    assert(sink.Clear() == Persistence::StorageStatus::Success);
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
        Persistence::PersistentLogSink<256U, 4U> sink(storage, "/logs", "device.log", policy);
        assert(sink.Initialize() == Persistence::StorageStatus::Success);
        for (std::uint32_t index = 0U; index < 12U; ++index) {
            const Logging::LogField field[] = {{"sequence", index}};
            Submit(sink, Logging::LogLevel::Info, "rotation-record", Logging::Fields(field));
        }
        assert(sink.RetainedBytes() <= 420U);
        assert(sink.GenerationExists(0U));
        assert(sink.GenerationSize(0U) <= 150U);
        assert(sink.GetStatistics().AcceptedRecords == 12U);
        sink.Shutdown();
    }

    // A fresh Sink instance must discover the file generations rather than assuming a fresh boot filesystem.
    Persistence::PersistentLogSink<256U, 4U> restarted(storage, "/logs", "device.log", policy);
    assert(restarted.Initialize() == Persistence::StorageStatus::Success);
    assert(restarted.RetainedBytes() > 0U && restarted.RetainedBytes() <= 420U);
    assert(restarted.GenerationExists(0U));

    std::string retained;
    assert(restarted.VisitRetained(Collect, &retained) == Persistence::StorageStatus::Success);
    assert(retained.find("rotation-record") != std::string::npos);
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
    assert(sink.RetainedBytes() == 0U);
}

} // namespace

int main() {
    TestRequiresReadyBoundedCapableStorage();
    TestFormattingEscapingFilteringAndClear();
    TestRotationCapacityAndRestartDiscovery();
    TestOversizedRecordIsDroppedWithoutPartialWrite();
    return 0;
}
