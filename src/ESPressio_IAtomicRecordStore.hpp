#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ESPressio::Persistence {
/// <summary>Finite opaque key for a durable record; no borrowed key storage survives an operation.</summary>
class AtomicRecordKey final {
    std::array<std::uint8_t,32> _bytes{};
    std::uint8_t _size=0;
public:
    /// <summary>Creates a key from at most 32 nonempty bytes. Invalid input leaves output unchanged.</summary>
    static constexpr bool TryCreate(std::string_view bytes, AtomicRecordKey& output) noexcept {
        if (bytes.empty() || bytes.size()>32) return false;
        AtomicRecordKey key;
        for (std::size_t i=0;i<bytes.size();++i) key._bytes[i]=static_cast<std::uint8_t>(bytes[i]);
        key._size=static_cast<std::uint8_t>(bytes.size()); output=key; return true;
    }
    /// <summary>Returns the immutable key bytes and active length.</summary>
    constexpr const std::uint8_t* Data() const noexcept { return _bytes.data(); }
    constexpr std::size_t Size() const noexcept { return _size; }
    constexpr explicit operator bool() const noexcept { return _size!=0; }
    /// <summary>Compares the complete opaque key.</summary>
    friend constexpr bool operator==(const AtomicRecordKey& a,const AtomicRecordKey& b) noexcept {
        if (a._size!=b._size) return false;
        for (std::size_t i=0;i<a._size;++i) if (a._bytes[i]!=b._bytes[i]) return false;
        return true;
    }
};
/// <summary>Explicit outcomes distinguish failed storage operations from unknown commit durability.</summary>
enum class AtomicRecordStatus : std::uint8_t {
    Success, NotFound, InvalidArgument, NotSupported, BufferTooSmall,
    NoSpace, Busy, Corrupt, StorageFailure, CommitAmbiguous
};
/// <summary>Known durable backend guarantees and finite admission limits.</summary>
/// <remarks>A generic file/key-value Write or a volatile atomic swap does not establish either guarantee.
/// Concrete backends must document power-loss old-or-new recovery and their bounded allocation behavior.</remarks>
struct AtomicRecordCapabilities {
    bool DurableOldOrNew=false;
    bool BoundedOperations=false;
    std::size_t MaximumRecordBytes=0;
    std::size_t MaximumRecords=0;
};
/// <summary>Reusable crash-consistent record seam for identity, execution history, results and authoritative snapshots.</summary>
/// <remarks>Composition owns the backend and serializes transactions. All key/data buffers are caller-bounded;
/// implementations must not retain them after return. Success on replacement means durable commit, not submission.
/// CommitAmbiguous never authorizes exposing a candidate. Recovery must expose only old/new complete committed records.</remarks>
class IAtomicRecordStore {
public:
    virtual ~IAtomicRecordStore()=default;
    /// <summary>Returns immutable finite capacity and proven backend guarantees.</summary>
    virtual AtomicRecordCapabilities Capabilities() const noexcept=0;
    /// <summary>Recovers/validates committed state before access; never fabricates empty state from corruption.</summary>
    virtual AtomicRecordStatus Recover() noexcept=0;
    /// <summary>Reads one complete record. Too-small buffers fail before copying; failed bytes are unpublished scratch.</summary>
    virtual AtomicRecordStatus Read(const AtomicRecordKey& key,std::uint8_t* buffer,std::size_t capacity,std::size_t& bytesRead) noexcept=0;
    /// <summary>Durably replaces a complete record atomically. Interrupted replacement recovers exactly old or new.</summary>
    virtual AtomicRecordStatus ReplaceAtomically(const AtomicRecordKey& key,const std::uint8_t* data,std::size_t size) noexcept=0;
    /// <summary>Removes obsolete data only after the owning semantic record no longer requires it.</summary>
    /// <remarks>The caller commits retirement first. Repeated removal of absent data succeeds. This is not a history-reset API.</remarks>
    virtual AtomicRecordStatus RemoveAfterCommit(const AtomicRecordKey& key) noexcept=0;
};
}
