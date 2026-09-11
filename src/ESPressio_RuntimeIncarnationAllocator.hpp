#pragma once
#include <atomic>
#include <limits>
#include <ESPressio_RuntimeIdentity.hpp>
#include "ESPressio_IAtomicRecordStore.hpp"
#include "ESPressio_RuntimeIncarnationRecord.hpp"

namespace ESPressio::Persistence {
/// <summary>Explicit provisioning/bootstrap outcomes; none implies identity availability except successful installation.</summary>
enum class RuntimeIncarnationStatus : std::uint8_t {
    Success, AlreadyInstalled, Unprovisioned, AlreadyProvisioned, DeviceMismatch,
    Corrupt, CommitAmbiguous, Exhausted, StorageFailure, BackendNotSupported, Busy, InvalidDevice
};
/// <summary>External evidence required for deliberate provisioning; no default or automatic reset is supplied.</summary>
enum class IncarnationProvisioningEvidence : std::uint8_t {
    NewDeviceIdentifier, RemoteHistoryInvalidated
};
/// <summary>Bootstrap returns an identity only after known durable commit and immutable System installation.</summary>
struct RuntimeIncarnationResult {
    RuntimeIncarnationStatus Status=RuntimeIncarnationStatus::StorageFailure;
    System::DeviceRuntimeIdentity Identity{};
};
/// <summary>Read-only status of a provisioned durable record, independent of the current System identity.</summary>
struct RuntimeIncarnationProvisioningStatus {
    RuntimeIncarnationStatus Status=RuntimeIncarnationStatus::StorageFailure;
    std::uint32_t LastCommitted=0;
};
/// <summary>Owns P4 commit-before-install bootstrap over a proven bounded atomic record store.</summary>
/// <remarks>All allocator instances share one process-lifetime bootstrap coordinator. A second service start never
/// consumes an incarnation. Any bootstrap failure is sticky for this process; explicit provisioning can prepare
/// the next boot but cannot clear a failed attempt. The store is borrowed only for synchronous calls.</remarks>
class RuntimeIncarnationAllocator final {
    IAtomicRecordStore& _store;
    inline static std::atomic_flag Access=ATOMIC_FLAG_INIT;
    inline static bool Attempted=false; // Access protects the entire read/commit/install transaction.
    inline static RuntimeIncarnationStatus Failure=RuntimeIncarnationStatus::StorageFailure;
    inline static constexpr AtomicRecordKey Key=[] {
        AtomicRecordKey key; AtomicRecordKey::TryCreate("system.runtime.incarnation",key); return key;
    }();
    struct Guard {
        bool Owned=!Access.test_and_set(std::memory_order_acquire);
        ~Guard() { if (Owned) Access.clear(std::memory_order_release); }
    };
    static RuntimeIncarnationStatus Map(AtomicRecordStatus status) noexcept {
        switch (status) {
            case AtomicRecordStatus::Success: return RuntimeIncarnationStatus::Success;
            case AtomicRecordStatus::NotFound: return RuntimeIncarnationStatus::Unprovisioned;
            case AtomicRecordStatus::Corrupt: case AtomicRecordStatus::BufferTooSmall: return RuntimeIncarnationStatus::Corrupt;
            case AtomicRecordStatus::CommitAmbiguous: return RuntimeIncarnationStatus::CommitAmbiguous;
            case AtomicRecordStatus::NotSupported: return RuntimeIncarnationStatus::BackendNotSupported;
            case AtomicRecordStatus::Busy: return RuntimeIncarnationStatus::Busy;
            default: return RuntimeIncarnationStatus::StorageFailure;
        }
    }
    RuntimeIncarnationStatus Ready() noexcept {
        const auto limits=_store.Capabilities();
        if (!limits.DurableOldOrNew || !limits.BoundedOperations || limits.MaximumRecordBytes<RuntimeIncarnationRecord::Bytes{}.size() || limits.MaximumRecords==0)
            return RuntimeIncarnationStatus::BackendNotSupported;
        return Map(_store.Recover());
    }
    RuntimeIncarnationStatus Read(System::DeviceIdentifier device,RuntimeIncarnationRecord& record) noexcept {
        RuntimeIncarnationRecord::Bytes bytes{};
        std::size_t count=0;
        const auto status=Map(_store.Read(Key,bytes.data(),bytes.size(),count));
        if (status!=RuntimeIncarnationStatus::Success) return status;
        if (count!=bytes.size() || !RuntimeIncarnationRecord::Decode(bytes,record)) return RuntimeIncarnationStatus::Corrupt;
        return record.Device==device ? RuntimeIncarnationStatus::Success : RuntimeIncarnationStatus::DeviceMismatch;
    }
public:
    /// <summary>Returns the fixed record key for predeclaring bounded backend capacity during composition.</summary>
    static constexpr AtomicRecordKey RecordKey() noexcept { return Key; }
    /// <summary>Borrows a store whose lifetime covers all calls; construction performs no reads, writes or allocation.</summary>
    explicit RuntimeIncarnationAllocator(IAtomicRecordStore& store) noexcept : _store(store) {}
    /// <summary>Deliberately provisions H=0 only when no record exists and identity-domain safety was established externally.</summary>
    /// <remarks>After erasure, callers must supply a new DeviceIdentifier or invalidate remote history first.
    /// Existing/corrupt/mismatched records are never overwritten. Evidence is an explicit caller precondition,
    /// not a claim that storage can distinguish first use from complete erasure.</remarks>
    RuntimeIncarnationStatus Provision(System::DeviceIdentifier device,IncarnationProvisioningEvidence evidence) noexcept {
        if (!device) return RuntimeIncarnationStatus::InvalidDevice;
        if (evidence!=IncarnationProvisioningEvidence::NewDeviceIdentifier && evidence!=IncarnationProvisioningEvidence::RemoteHistoryInvalidated)
            return RuntimeIncarnationStatus::InvalidDevice;
        if (System::RuntimeIdentity::IsInstalled()) return RuntimeIncarnationStatus::AlreadyInstalled;
        Guard guard; if (!guard.Owned) return RuntimeIncarnationStatus::Busy;
        if (System::RuntimeIdentity::IsInstalled()) return RuntimeIncarnationStatus::AlreadyInstalled;
        auto status=Ready(); if (status!=RuntimeIncarnationStatus::Success) return status;
        RuntimeIncarnationRecord record;
        status=Read(device,record);
        if (status==RuntimeIncarnationStatus::Success) return RuntimeIncarnationStatus::AlreadyProvisioned;
        if (status!=RuntimeIncarnationStatus::Unprovisioned) return status;
        const auto bytes=RuntimeIncarnationRecord{device,0}.Encode();
        return Map(_store.ReplaceAtomically(Key,bytes.data(),bytes.size()));
    }
    /// <summary>Reads validated provisioning status without advancing or installing an identity.</summary>
    RuntimeIncarnationProvisioningStatus ReadProvisioningStatus(System::DeviceIdentifier device) noexcept {
        if (!device) return {RuntimeIncarnationStatus::InvalidDevice,0};
        Guard guard; if (!guard.Owned) return {RuntimeIncarnationStatus::Busy,0};
        auto status=Ready(); if (status!=RuntimeIncarnationStatus::Success) return {status,0};
        RuntimeIncarnationRecord record; status=Read(device,record);
        return {status,status==RuntimeIncarnationStatus::Success ? record.LastCommitted : 0};
    }
    /// <summary>Allocates exactly one durable incarnation for this process and installs it into System after commit.</summary>
    /// <remarks>UINT32_MAX is final. Failed/ambiguous commit exposes no candidate and cannot be retried in this process.
    /// Restarting a family, adapter, Mesh or Thread returns AlreadyInstalled without accessing storage.</remarks>
    RuntimeIncarnationResult AllocateNext(System::DeviceIdentifier device) noexcept {
        if (const auto* current=System::RuntimeIdentity::TryGet()) return {RuntimeIncarnationStatus::AlreadyInstalled,*current};
        Guard guard; if (!guard.Owned) return {RuntimeIncarnationStatus::Busy,{}};
        if (const auto* current=System::RuntimeIdentity::TryGet()) return {RuntimeIncarnationStatus::AlreadyInstalled,*current};
        if (Attempted) return {Failure,{}};
        Attempted=true;
        auto fail=[&](RuntimeIncarnationStatus status) noexcept {
            Failure=status; return RuntimeIncarnationResult{status,{}};
        };
        if (!device) return fail(RuntimeIncarnationStatus::InvalidDevice);
        auto status=Ready(); if (status!=RuntimeIncarnationStatus::Success) return fail(status);
        RuntimeIncarnationRecord record;
        status=Read(device,record); if (status!=RuntimeIncarnationStatus::Success) return fail(status);
        if (record.LastCommitted==std::numeric_limits<std::uint32_t>::max()) return fail(RuntimeIncarnationStatus::Exhausted);
        ++record.LastCommitted;
        const auto bytes=record.Encode();
        status=Map(_store.ReplaceAtomically(Key,bytes.data(),bytes.size()));
        if (status!=RuntimeIncarnationStatus::Success) return fail(status);
        const System::DeviceRuntimeIdentity identity{device,System::RuntimeIncarnationId(record.LastCommitted)};
        if (System::RuntimeIdentity::Install(identity)!=System::RuntimeIdentity::InstallationStatus::Success) {
            // Another composition path installed first. Never roll back a committed-but-unused value.
            if (const auto* current=System::RuntimeIdentity::TryGet()) return {RuntimeIncarnationStatus::AlreadyInstalled,*current};
            return fail(RuntimeIncarnationStatus::Busy);
        }
        return {RuntimeIncarnationStatus::Success,identity};
    }
};
}
