#include <ESPressio_AtomicFileRecordStore.hpp>
#include <ESPressio_RuntimeIncarnationRecord.hpp>
#include "DurableFileModel.hpp"
#include <cassert>
#include <cstdlib>
#include <new>

using namespace ESPressio::Persistence;
static bool denyHeap=false;
void* operator new(std::size_t size) { if (denyHeap) std::abort(); if (auto* p=std::malloc(size ? size : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }

int main() {
    denyHeap=true;
    AtomicRecordKey key;
    assert(AtomicRecordKey::TryCreate("record",key));
    const auto previous=key;
    assert(!AtomicRecordKey::TryCreate("",key) && key==previous);
    assert(!AtomicRecordKey::TryCreate("123456789012345678901234567890123",key) && key==previous);
    const std::array<AtomicRecordKey,1> keys{key};
    const std::array<std::uint8_t,3> old{1,2,3};
    const std::array<std::uint8_t,4> next{4,5,6,7};
    for (auto cut:std::array<DurableFileModel::Cut,7>{DurableFileModel::Cut::PartialWrite,
        DurableFileModel::Cut::BeforeFileSync,DurableFileModel::Cut::AfterFileSync,
        DurableFileModel::Cut::BeforePublish,DurableFileModel::Cut::AfterPublish,
        DurableFileModel::Cut::BeforeDirectorySync,DurableFileModel::Cut::AfterDirectorySync}) {
        DurableFileModel files; assert(files.Initialize()==StorageStatus::Success);
        AtomicFileRecordStore<64,1> store(files,keys,"/"); assert(store.Recover()==AtomicRecordStatus::Success);
        assert(store.ReplaceAtomically(key,old.data(),old.size())==AtomicRecordStatus::Success);
        files.FailAt=cut;
        auto result=store.ReplaceAtomically(key,next.data(),next.size());
        assert(result!=AtomicRecordStatus::Success);
        if (cut>=DurableFileModel::Cut::BeforePublish) assert(result==AtomicRecordStatus::CommitAmbiguous);
        files.PowerLoss();
        AtomicFileRecordStore<64,1> recovered(files,keys,"/"); assert(recovered.Recover()==AtomicRecordStatus::Success);
        std::array<std::uint8_t,8> buffer{}; std::size_t count=0;
        assert(recovered.Read(key,buffer.data(),buffer.size(),count)==AtomicRecordStatus::Success);
        if (cut==DurableFileModel::Cut::AfterDirectorySync) assert(count==next.size() && std::memcmp(buffer.data(),next.data(),count)==0);
        else assert(count==old.size() && std::memcmp(buffer.data(),old.data(),count)==0);
        buffer.fill(0xa5); count=99;
        assert(recovered.Read(key,buffer.data(),1,count)==AtomicRecordStatus::BufferTooSmall && count==0 && buffer[0]==0xa5);
        assert(recovered.RemoveAfterCommit(key)==AtomicRecordStatus::Success);
        assert(recovered.RemoveAfterCommit(key)==AtomicRecordStatus::Success);
        files.PowerLoss(); assert(recovered.Recover()==AtomicRecordStatus::Success);
        assert(recovered.Read(key,buffer.data(),buffer.size(),count)==AtomicRecordStatus::NotFound);
    }
    {
        DurableFileModel files; assert(files.Initialize()==StorageStatus::Success);
        files.ProvesDurability=false;
        AtomicFileRecordStore<64,1> store(files,keys,"/");
        assert(store.Recover()==AtomicRecordStatus::NotSupported && files.Writes==0);
        files.ProvesDurability=true; files.ProvesBounded=false;
        assert(store.Recover()==AtomicRecordStatus::NotSupported && files.Writes==0);
        files.ProvesBounded=true; assert(store.Recover()==AtomicRecordStatus::Success);
        assert(store.ReplaceAtomically(key,old.data(),old.size())==AtomicRecordStatus::Success);
        assert(files.CorruptCommittedByte(8)); // generation/integrity cannot be silently repaired
        files.PowerLoss(); assert(store.Recover()==AtomicRecordStatus::Corrupt);
        assert(store.ReplaceAtomically(key,next.data(),next.size())!=AtomicRecordStatus::Success);
    }
    {
        ESPressio::System::DeviceIdentifier::Storage bytes{}; bytes[0]=1; bytes[15]=0xfe;
        RuntimeIncarnationRecord record{ESPressio::System::DeviceIdentifier(bytes),0xffffffffu};
        const auto encoded=record.Encode();
        assert(encoded.size()==40 && encoded[4]==1 && encoded[6]==40);
        assert(encoded[24]==0 && encoded[28]==1 && encoded[32]==0xff); // generation = 2^32, H = UINT32_MAX
        RuntimeIncarnationRecord decoded;
        assert(RuntimeIncarnationRecord::Decode(encoded,decoded) && decoded.LastCommitted==0xffffffffu && decoded.Device==record.Device);
        for (std::size_t i=0;i<encoded.size();++i) { auto corrupt=encoded; corrupt[i]^=1; assert(!RuntimeIncarnationRecord::Decode(corrupt,decoded)); }
    }
    denyHeap=false;
}
