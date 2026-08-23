#include <ESPressio_Persistence.hpp>
#include <array>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

using namespace ESPressio::Persistence;

static void TestCapabilities() {
    const auto capabilities = StorageCapability::Hierarchical | StorageCapability::Rename;
    assert(HasCapability(capabilities, StorageCapability::Hierarchical));
    assert(HasCapability(capabilities, StorageCapability::Rename));
    assert(!HasCapability(capabilities, StorageCapability::KeyValue));
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

    assert(storage.Rename("/data.bin", "/renamed.bin") == StorageStatus::Success);
    bool exists = false;
    assert(storage.Exists("/renamed.bin", exists) == StorageStatus::Success && exists);
    assert(storage.Remove("/renamed.bin") == StorageStatus::Success);
    assert(storage.Remove("/renamed.bin") == StorageStatus::NotFound);
}

static bool CountEntries(const StorageEntry&, void* context) {
    ++(*static_cast<std::size_t*>(context));
    return true;
}

static void TestDirectoriesAndListing() {
    MemoryFileStorage storage;
    assert(storage.Initialize() == StorageStatus::Success);
    assert(storage.CreateDirectory("/config") == StorageStatus::Success);
    const uint8_t value = 9;
    assert(storage.Write("/config/a.bin", &value, 1) == StorageStatus::Success);
    std::size_t count = 0;
    assert(storage.List("/", CountEntries, &count) == StorageStatus::Success);
    assert(count >= 2);
    assert(storage.RemoveDirectory("/config") == StorageStatus::Success);
}

static void TestAtomicReplacement() {
    MemoryFileStorage storage;
    assert(storage.Initialize() == StorageStatus::Success);
    AtomicFileStore atomic(storage);
    const uint8_t oldValue[] = {'o','l','d'};
    const uint8_t newValue[] = {'n','e','w'};
    assert(storage.Write("/settings.bin", oldValue, sizeof(oldValue)) == StorageStatus::Success);
    assert(atomic.Replace("/settings.bin", newValue, sizeof(newValue)) == StorageStatus::Success);
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
    assert(storage.Remove("mode") == StorageStatus::Success);
    assert(storage.Clear() == StorageStatus::Success);
}

int main() {
    TestCapabilities();
    TestMemoryFileLifecycleAndValidation();
    TestFileWriteReadAppendStatRenameAndRemove();
    TestDirectoriesAndListing();
    TestAtomicReplacement();
    TestKeyValueStorage();
    return 0;
}
