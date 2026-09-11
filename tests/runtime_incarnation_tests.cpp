#include <ESPressio_RuntimeIncarnationAllocator.hpp>
#include <ESPressio_AtomicFileRecordStore.hpp>
#include "DurableFileModel.hpp"
#include <cassert>
#include <string_view>
#include <thread>

using namespace ESPressio;
using namespace ESPressio::Persistence;
static System::DeviceIdentifier Device(std::uint8_t first=1) {
    System::DeviceIdentifier::Storage bytes{}; bytes[0]=first; return System::DeviceIdentifier(bytes);
}
int main(int argc,char** argv) {
    assert(argc==2);
    const std::string_view test=argv[1];
    DurableFileModel files; assert(files.Initialize()==StorageStatus::Success);
    const std::array<AtomicRecordKey,1> keys{RuntimeIncarnationAllocator::RecordKey()};
    AtomicFileRecordStore<40,1> store(files,keys,"/");
    RuntimeIncarnationAllocator allocator(store);
    if (test=="unsupported") {
        files.ProvesDurability=false;
        assert(allocator.AllocateNext(Device()).Status==RuntimeIncarnationStatus::BackendNotSupported);
        assert(!System::RuntimeIdentity::IsInstalled() && files.Writes==0); return 0;
    }
    if (test=="missing") {
        assert(allocator.AllocateNext(Device()).Status==RuntimeIncarnationStatus::Unprovisioned);
        assert(files.Writes==0 && !System::RuntimeIdentity::IsInstalled());
        assert(allocator.Provision(Device(),IncarnationProvisioningEvidence::NewDeviceIdentifier)==RuntimeIncarnationStatus::Success);
        assert(allocator.AllocateNext(Device()).Status==RuntimeIncarnationStatus::Unprovisioned); // sticky failure: next boot must retry
        assert(files.Writes==1 && !System::RuntimeIdentity::IsInstalled()); return 0;
    }
    assert(allocator.Provision(Device(),IncarnationProvisioningEvidence::NewDeviceIdentifier)==RuntimeIncarnationStatus::Success);
    assert(allocator.Provision(Device(),IncarnationProvisioningEvidence::NewDeviceIdentifier)==RuntimeIncarnationStatus::AlreadyProvisioned);
    assert(allocator.Provision(Device(2),IncarnationProvisioningEvidence::NewDeviceIdentifier)==RuntimeIncarnationStatus::DeviceMismatch);
    auto provisioned=allocator.ReadProvisioningStatus(Device());
    assert(provisioned.Status==RuntimeIncarnationStatus::Success && provisioned.LastCommitted==0);
    if (test=="mismatch") {
        assert(allocator.AllocateNext(Device(2)).Status==RuntimeIncarnationStatus::DeviceMismatch);
        assert(!System::RuntimeIdentity::IsInstalled() && files.Writes==1); return 0;
    }
    if (test=="corrupt") {
        assert(files.CorruptCommittedByte(80));
        assert(allocator.AllocateNext(Device()).Status==RuntimeIncarnationStatus::Corrupt);
        assert(allocator.Provision(Device(),IncarnationProvisioningEvidence::RemoteHistoryInvalidated)==RuntimeIncarnationStatus::Corrupt);
        assert(!System::RuntimeIdentity::IsInstalled() && files.Writes==1); return 0;
    }
    if (test=="exhausted" || test=="final") {
        RuntimeIncarnationRecord record{Device(),test=="final" ? 0xfffffffeu : 0xffffffffu};
        const auto bytes=record.Encode();
        assert(store.ReplaceAtomically(keys[0],bytes.data(),bytes.size())==AtomicRecordStatus::Success);
        auto result=allocator.AllocateNext(Device());
        if (test=="exhausted") { assert(result.Status==RuntimeIncarnationStatus::Exhausted && !result.Identity && !System::RuntimeIdentity::IsInstalled()); }
        else { assert(result.Status==RuntimeIncarnationStatus::Success && result.Identity.Incarnation.Value()==0xffffffffu); }
        return 0;
    }
    if (test=="ambiguous_old" || test=="ambiguous_new" || test=="failed_write") {
        files.FailAt=test=="ambiguous_old" ? DurableFileModel::Cut::BeforeDirectorySync :
            test=="ambiguous_new" ? DurableFileModel::Cut::AfterDirectorySync : DurableFileModel::Cut::PartialWrite;
        auto result=allocator.AllocateNext(Device());
        assert(result.Status==(test=="failed_write" ? RuntimeIncarnationStatus::StorageFailure : RuntimeIncarnationStatus::CommitAmbiguous));
        assert(!result.Identity && !System::RuntimeIdentity::IsInstalled());
        const auto writes=files.Writes;
        assert(allocator.AllocateNext(Device()).Status==result.Status && files.Writes==writes);
        files.PowerLoss(); // storage-only recovery does not reset the process bootstrap coordinator
        auto state=allocator.ReadProvisioningStatus(Device());
        assert(state.Status==RuntimeIncarnationStatus::Success && state.LastCommitted==(test=="ambiguous_new" ? 1u : 0u));
        return 0;
    }
    files.AfterDurableCommit=[](void*) noexcept { assert(!System::RuntimeIdentity::IsInstalled()); };
    if (test=="concurrent") {
        std::array<std::thread,8> threads;
        std::array<RuntimeIncarnationStatus,8> results{};
        for (std::size_t i=0;i<threads.size();++i) threads[i]=std::thread([&,i] { RuntimeIncarnationAllocator other(store); results[i]=other.AllocateNext(Device()).Status; });
        for (auto& thread:threads) thread.join();
        unsigned successes=0;
        for (auto status:results) { if (status==RuntimeIncarnationStatus::Success) ++successes; else assert(status==RuntimeIncarnationStatus::AlreadyInstalled || status==RuntimeIncarnationStatus::Busy); }
        assert(successes==1 && files.Writes==2);
    } else {
        assert(test=="success");
        auto result=allocator.AllocateNext(Device());
        assert(result.Status==RuntimeIncarnationStatus::Success && result.Identity.Incarnation.Value()==1 && result.Identity.Device==Device());
    }
    files.Shutdown();
    RuntimeIncarnationAllocator restartedService(store);
    auto again=restartedService.AllocateNext(Device());
    assert(again.Status==RuntimeIncarnationStatus::AlreadyInstalled && again.Identity.Incarnation.Value()==1 && files.Writes==2);
}
