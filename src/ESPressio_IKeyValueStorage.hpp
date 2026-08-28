#pragma once

#include <ESPressio_IStorageBackend.hpp>

namespace ESPressio::Persistence {

/// <summary>Abstract persistence backend exposing byte-oriented values addressed by string keys.</summary>
class IKeyValueStorage : public IStorageBackend {
public:
    ~IKeyValueStorage() override = default;

    /// <summary>Determines whether a key currently exists.</summary>
    virtual StorageStatus Contains(const char* key, bool& exists) const = 0;
    /// <summary>Returns the stored byte length for a key.</summary>
    virtual StorageStatus GetSize(const char* key, std::size_t& size) const = 0;
    /// <summary>Reads the complete stored value into a caller-provided buffer.</summary>
    virtual StorageStatus Read(
        const char* key,
        uint8_t* buffer,
        std::size_t capacity,
        std::size_t& bytesRead
    ) const = 0;
    /// <summary>Stores a byte value under a key, replacing any existing value.</summary>
    virtual StorageStatus Write(
        const char* key,
        const uint8_t* data,
        std::size_t size
    ) = 0;
    /// <summary>Removes a stored key/value entry.</summary>
    virtual StorageStatus Remove(const char* key) = 0;
    /// <summary>Removes all key/value entries from the backend.</summary>
    virtual StorageStatus Clear() = 0;
};

} // namespace ESPressio::Persistence
