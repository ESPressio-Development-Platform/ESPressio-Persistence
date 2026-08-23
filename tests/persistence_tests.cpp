#include <ESPressio_Persistence.hpp>
#include <array>
#include <cassert>
#include <cstring>

using namespace ESPressio::Persistence;

static void TestCapabilitiesAndStatusNames() {
    const auto capabilities = StorageCapability::Hierarchical | StorageCapability::Rename;
    assert(HasCapability(capabilities, StorageCapability::Hierarchical));
    assert(HasCapability(capabilities, StorageCapability::Rename));
    assert(!HasCapability(capabilities, StorageCapability::KeyValue));
    assert(std::strcmp(StorageStatusName(StorageStatus::NoSpace), "NoSpace") == 0);
}

static void TestMemoryFileLifecycleAndValidation() {
    MemoryFileStorage storage;
    bool exists = false;
    assert(storage.Exists("/x", exists) == StorageStatus::NotInitialized);
    assert(storage.Initialize() == StorageStatus::Success);
    assert(storage.Initialize() == StorageStatus::AlreadyInitialized);
    assert(storage.Exists(nullptr, exists) == StorageStatus::InvalidArgument);
    storage.Shutdown();
    assert(!storage.IsReady());
}

static void TestFileWriteReadAppendStatRenameAndRemove() {
    MemoryFileStorage storage;
    assert(storage.Initialize() == StorageStatus::Success);
    const uint8_t first[] = {1, 2, 3};
    const uint8_t second[] = {4, 5};
    assert(storage.Write("/data.bin", first, sizeof(first)) == StorageStatus::Success);
    assert(storage.Write("/data.bin", second, sizeof(second), WriteMode::Append) == StorageStatus::Success);

    StorageEntry entry{};
    assert(storage.Stat("/data.bin", entry) == StorageStatus::Success);
    assert(entry.size == 5);
    assert(!entry.isDirectory);

    std::array<uint8_t, 3> buffer{};
    std::size_t bytesRead = 0;
    assert(storage.Read("/data.bin", 1, buffer.data(), buffer.size(), bytesRead) == StorageStatus::Success);
    assert(bytesRead == 3);
    assert((buffer == std::array<uint8_t, 3>{2, 3, 4}));

    bytesRead = 99;
    assert(storage.Read("/data.bin", 5, buffer.data(), buffer.size(), bytesRead) == StorageStatus::Success);
    assert(bytesRead == 0);

    assert(storage.Rename("/data.bin", "/renamed.bin") == StorageStatus::Success);
    bool exists = false;
    assert(storage.Exists("/renamed.bin", exists) == StorageStatus::Success && exists);
    assert(storage.Remove("/renamed.bin") == StorageStatus::Success);
    assert(storage.Remove("/renamed.bin") == StorageStatus::NotFound);
}

struct ListState { std::size_t count = 0; std::size_t stopAfter = 0; };
static bool CountEntries(const StorageEntry&, void* context) {
    auto& state = *static_cast<ListState*>(context);
    ++state.count;
    return state.stopAfter == 0 || state.count < state.stopAfter;
}

static void TestDirectoriesAndListing() {
    MemoryFileStorage storage;
    assert(storage.Initialize() == StorageStatus::Success);
    assert(storage.CreateDirectory("/config") == StorageStatus::Success);
    assert(storage.CreateDirectory("/config/nested") == StorageStatus::Success);
    const uint8_t value = 9;
    assert(storage.Write("/config/a.bin", &value, 1) == StorageStatus::Success);
    assert(storage.Write("/config/nested/b.bin", &value, 1) == StorageStatus::Success);

    ListState root{};
    assert(storage.List("/", CountEntries, &root) == StorageStatus::Success);
    assert(root.count == 1); // /config only; listing is one level.

    ListState config{};
    assert(storage.List("/config", CountEntries, &config) == StorageStatus::Success);
    assert(config.count == 2); // nested directory + a.bin

    ListState early{0, 1};
    assert(storage.List("/config", CountEntries, &early) == StorageStatus::Success);
    assert(early.count == 1);

    assert(storage.RemoveDirectory("/config") == StorageStatus::Busy);
    assert(storage.Remove("/config/nested/b.bin") == StorageStatus::Success);
    assert(storage.RemoveDirectory("/config/nested") == StorageStatus::Success);
    assert(storage.Remove("/config/a.bin") == StorageStatus::Success);
    assert(storage.RemoveDirectory("/config") == StorageStatus::Success);
}

class FailingPromotionStorage final : public IFileStorage {
public:
    StorageStatus Initialize() override { return _inner.Initialize(); }
    void Shutdown() override { _inner.Shutdown(); }
    bool IsReady() const override { return _inner.IsReady(); }
    const char* GetBackendName() const override { return "FailingPromotionStorage"; }
    StorageCapability GetCapabilities() const override { return _inner.GetCapabilities(); }
    StorageStatistics GetStatistics() const override { return _inner.GetStatistics(); }
    StorageStatus Exists(const char* p, bool& e) const override { return _inner.Exists(p, e); }
    StorageStatus Stat(const char* p, StorageEntry& e) const override { return _inner.Stat(p, e); }
    StorageStatus Read(const char* p, uint64_t o, uint8_t* b, std::size_t c, std::size_t& r) const override { return _inner.Read(p, o, b, c, r); }
    StorageStatus Write(const char* p, const uint8_t* d, std::size_t s, WriteMode m) override { return _inner.Write(p, d, s, m); }
    StorageStatus Remove(const char* p) override { return _inner.Remove(p); }
    StorageStatus Rename(const char* from, const char* to) override {
        if (_failPromotion && std::strstr(from, ".tmp") != nullptr) {
            _failPromotion = false;
            return StorageStatus::IoError;
        }
        return _inner.Rename(from, to);
    }
    StorageStatus CreateDirectory(const char* p) override { return _inner.CreateDirectory(p); }
    StorageStatus RemoveDirectory(const char* p) override { return _inner.RemoveDirectory(p); }
    StorageStatus List(const char* p, StorageListCallback c, void* x) const override { return _inner.List(p, c, x); }
    void FailNextPromotion() { _failPromotion = true; }
private:
    MemoryFileStorage _inner;
    bool _failPromotion = false;
};

static void TestAtomicReplacementAndRollback() {
    FailingPromotionStorage storage;
    assert(storage.Initialize() == StorageStatus::Success);
    AtomicFileStore atomic(storage);
    const uint8_t oldValue[] = {'o','l','d'};
    const uint8_t newValue[] = {'n','e','w'};
    assert(storage.Write("/settings.bin", oldValue, sizeof(oldValue), WriteMode::Replace) == StorageStatus::Success);
    assert(atomic.Replace("/settings.bin", newValue, sizeof(newValue)) == StorageStatus::Success);

    storage.FailNextPromotion();
    const uint8_t failedValue[] = {'b','a','d'};
    assert(atomic.Replace("/settings.bin", failedValue, sizeof(failedValue)) == StorageStatus::IoError);

    std::array<uint8_t, 3> result{};
    std::size_t bytesRead = 0;
    assert(storage.Read("/settings.bin", 0, result.data(), result.size(), bytesRead) == StorageStatus::Success);
    assert(bytesRead == 3);
    assert(std::memcmp(result.data(), newValue, 3) == 0);
    bool temporary = true;
    bool backup = true;
    assert(storage.Exists("/settings.bin.tmp", temporary) == StorageStatus::Success && !temporary);
    assert(storage.Exists("/settings.bin.bak", backup) == StorageStatus::Success && !backup);
}

static void TestKeyValueStorage() {
    MemoryKeyValueStorage storage;
    assert(storage.Initialize() == StorageStatus::Success);
    const uint8_t payload[] = {10, 20, 30, 40};
    assert(storage.Write("mode", payload, sizeof(payload)) == StorageStatus::Success);
    bool exists = false;
    assert(storage.Contains("mode", exists) == StorageStatus::Success && exists);
    std::size_t size = 0;
    assert(storage.GetSize("mode", size) == StorageStatus::Success && size == 4);
    std::array<uint8_t, 3> tooSmall{};
    std::size_t bytesRead = 0;
    assert(storage.Read("mode", tooSmall.data(), tooSmall.size(), bytesRead) == StorageStatus::NoSpace);
    std::array<uint8_t, 4> result{};
    assert(storage.Read("mode", result.data(), result.size(), bytesRead) == StorageStatus::Success);
    assert(bytesRead == 4 && std::memcmp(result.data(), payload, 4) == 0);

    assert(storage.Write("empty", nullptr, 0) == StorageStatus::Success);
    assert(storage.GetSize("empty", size) == StorageStatus::Success && size == 0);
    bytesRead = 99;
    assert(storage.Read("empty", nullptr, 0, bytesRead) == StorageStatus::Success && bytesRead == 0);

    assert(storage.Remove("mode") == StorageStatus::Success);
    assert(storage.Remove("missing") == StorageStatus::NotFound);
    assert(storage.Clear() == StorageStatus::Success);
}

int main() {
    TestCapabilitiesAndStatusNames();
    TestMemoryFileLifecycleAndValidation();
    TestFileWriteReadAppendStatRenameAndRemove();
    TestDirectoriesAndListing();
    TestAtomicReplacementAndRollback();
    TestKeyValueStorage();
    return 0;
}
