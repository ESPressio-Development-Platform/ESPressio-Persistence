#include <ESPressio_RuntimeIncarnationAllocator.hpp>
#include <ESPressio_AtomicFileRecordStore.hpp>
#include "DurableFileModel.hpp"
#include <cassert>
#include <new>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace ESPressio;
using namespace ESPressio::Persistence;
int main() {
    void* memory=mmap(nullptr,sizeof(DurableFileModel),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    assert(memory!=MAP_FAILED);
    auto& files=*new(memory) DurableFileModel;
    assert(files.Initialize()==StorageStatus::Success);
    System::DeviceIdentifier::Storage deviceBytes{}; deviceBytes[0]=1;
    const System::DeviceIdentifier device(deviceBytes);
    const std::array<AtomicRecordKey,1> keys{RuntimeIncarnationAllocator::RecordKey()};
    AtomicFileRecordStore<40,1> store(files,keys,"/");
    RuntimeIncarnationAllocator provisioning(store);
    assert(provisioning.Provision(device,IncarnationProvisioningEvidence::NewDeviceIdentifier)==RuntimeIncarnationStatus::Success);
    // Each child has a fresh process identity slot/coordinator. Shared model media alone survives.
    // No production reset hook is introduced to simulate another boot.
    for (unsigned boot=1;boot<=3;++boot) {
        auto process=fork(); assert(process>=0);
        if (process==0) {
            AtomicFileRecordStore<40,1> childStore(files,keys,"/");
            RuntimeIncarnationAllocator allocator(childStore);
            if (boot==1) files.AfterDurableCommit=[](void*) noexcept {
                assert(!System::RuntimeIdentity::IsInstalled());
                _exit(0); // power loss after durable counter advancement, before installation
            };
            auto result=allocator.AllocateNext(device);
            assert(boot!=1 && result.Status==RuntimeIncarnationStatus::Success);
            assert(result.Identity.Incarnation.Value()==boot);
            assert(System::RuntimeIdentity::TryGet()->Incarnation.Value()==boot);
            _exit(0);
        }
        int status{}; assert(waitpid(process,&status,0)==process);
        assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
        files.PowerLoss();
        assert(!System::RuntimeIdentity::IsInstalled());
        auto persisted=provisioning.ReadProvisioningStatus(device);
        assert(persisted.Status==RuntimeIncarnationStatus::Success && persisted.LastCommitted==boot);
    }
    files.~DurableFileModel(); assert(munmap(memory,sizeof(DurableFileModel))==0);
}
