#include <ESPressio_RuntimeIncarnationAllocator.hpp>
#include <ESPressio_AtomicFileRecordStore.hpp>
#include <ESPressio_TypeDirectory.hpp>
#include <ESPressio_Serializable.hpp>
#include "DurableFileModel.hpp"
#include <cassert>
#include <cstdlib>
#include <new>
using namespace ESPressio;
using namespace ESPressio::Persistence;
using namespace ESPressio::Serializable;
static bool denyHeap=false;
void* operator new(std::size_t bytes) {
    if (denyHeap) std::abort();
    if (auto* p=std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
struct Payload : Serializable<Payload> {
    BoundedString<8> Name;
    std::uint32_t Value{};
    ESPRESSIO_SERIALIZABLE_TYPE(Payload)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("name",Name),ESPRESSIO_PROPERTY("value",Value))
};
int main() {
    denyHeap=true;
    DurableFileModel files;
    assert(files.Initialize()==StorageStatus::Success);
    const std::array<AtomicRecordKey,1> keys{RuntimeIncarnationAllocator::RecordKey()};
    AtomicFileRecordStore<40,1> store(files,keys,"/");
    RuntimeIncarnationAllocator bootstrap(store);
    System::DeviceIdentifier::Storage deviceBytes{}; deviceBytes[0]=1;
    const System::DeviceIdentifier device(deviceBytes);
    assert(bootstrap.Provision(device,IncarnationProvisioningEvidence::NewDeviceIdentifier)==RuntimeIncarnationStatus::Success);
    const auto installed=bootstrap.AllocateNext(device);
    assert(installed.Status==RuntimeIncarnationStatus::Success);
    assert(System::RuntimeIdentity::TryGet()->Incarnation.Value()==1);

    // The composition layer copies P3 size facts into P1. Primitive includes no
    // Serializable or Persistence header and does not interpret the opaque graph.
    constexpr const auto& schema=SchemaDescriptor<Payload>();
    constexpr auto maximum=std::max(schema.MaximumDirectBinaryBytes,std::max(schema.MaximumCborBytes,schema.MaximumJsonBytes));
    Primitive::PrimitiveTypeDescriptor descriptor;
    descriptor.Key={Primitive::FamilyIds::Event,0x5033463038ull};
    descriptor.CanonicalName="foundation.Payload";
    descriptor.Capabilities=Primitive::PrimitiveTypeCapabilities(1);
    descriptor.Versions={1,1};
    descriptor.SerializedSize={maximum};
    descriptor.FamilyExtension={&schema};
    Primitive::TypeDirectory<1> directory;
    assert(directory.Register(descriptor)==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    const auto* found=directory.View().Find(descriptor.Key);
    assert(found && found->SerializedSize.MaximumCompletePrimitiveWireBytes==maximum);
    const auto* copied=static_cast<const StaticSchemaDescriptor*>(found->FamilyExtension.Data);
    assert((copied==&schema && copied->MaximumDirectBinaryBytes==MaximumSerializedSize<Payload,DirectBinary>));
    assert((copied->MaximumCborBytes==MaximumSerializedSize<Payload,CBOR>));
    assert((copied->MaximumJsonBytes==MaximumSerializedSize<Payload,JSON>));
    Payload original, restored; assert(original.Name.assign("bounded")); original.Value=0xffffffffu;
    std::array<std::uint8_t,maximum> bytes{};
    const auto encoded=SerializeDirectBinary(original,bytes.data(),bytes.size());
    assert(encoded && DeserializeBoundedDirectBinary(bytes.data(),encoded.Bytes,restored));
    assert(restored.Name==original.Name && restored.Value==original.Value);
}
