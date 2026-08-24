# ESPressio Persistence

A capability-aware persistence foundation for the ESPressio Development Platform.

ESPressio Persistence gives application code a stable way to store and retrieve data without coupling domain logic to LittleFS, SPIFFS, FAT, SD cards or ESP32 Preferences/NVS.

## Current version — 0.3.2

0.3.2 is a dependency-maintenance release validating typed persistence against ESPressio Serializable 0.11.3 and protected typed persistence against ESPressio Security 0.4.2. The public storage and persistence APIs introduced through 0.3.0 are unchanged.

Core Persistence remains dependency-free.

# Storage concepts

Persistence intentionally separates filesystems from key/value stores:

```text
IFileStorage
    hierarchical paths
    files/directories
    bounded reads
    replace/append
    rename/list/stat

IKeyValueStorage
    namespace-owned keys
    compact binary values
    replace/read/remove/clear
```

Both inherit `IStorageBackend` for initialization, readiness, capabilities and statistics.

## Which backend should I choose?

| Need | Recommended backend |
| --- | --- |
| Normal files/configuration in internal ESP32 flash | **LittleFS** |
| Legacy SPIFFS project | **SPIFFS** |
| FAT semantics in internal flash | **FFat** |
| Small settings/records | **Preferences/NVS** |
| External/removable SPI SD | **SD** |
| Native SD/MMC | **SD_MMC** |
| Host/unit tests | **MemoryFileStorage / MemoryKeyValueStorage** |

# Installation

Core:

```ini
lib_deps =
    espressio-development-platform/ESPressio-Persistence@^0.3.2
```

Typed Serializable persistence:

```ini
lib_deps =
    espressio-development-platform/ESPressio-Persistence@^0.3.2
    espressio-development-platform/ESPressio-Serializable@^0.11.3
```

Protected typed persistence additionally requires Security:

```ini
lib_deps =
    espressio-development-platform/ESPressio-Persistence@^0.3.2
    espressio-development-platform/ESPressio-Serializable@^0.11.3
    espressio-development-platform/ESPressio-Security@^0.4.2
    espressio-development-platform/ESPressio-Observable@^3.0.2
```

Headers are deliberately opt-in:

```cpp
#include <ESPressio_Persistence.hpp>                         // raw storage
#include <ESPressio_ESP32Persistence.hpp>                    // ESP32 backends
#include <ESPressio_Persistence_Serializable.hpp>            // typed, unprotected
#include <ESPressio_Persistence_Serializable_Security.hpp>   // typed + protected
```

# The easiest protected configuration flow

Assume a normal Serializable configuration:

```cpp
class DeviceConfiguration final
    : public ESPressio::Serializable::Serializable<DeviceConfiguration> {

    ESPRESSIO_SERIALIZABLE_TYPE(DeviceConfiguration)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)

private:
    std::string _ssid = "ESPressio-Lab";
    std::string _password = "secret";
    uint8_t _channel = 6;

public:
    ESPRESSIO_SERIALIZABLE_PROPERTIES(
        ESPRESSIO_PROPERTY("ssid", _ssid),
        ESPRESSIO_PROPERTY("password", _password),
        ESPRESSIO_PROPERTY("channel", _channel)
    )
};
```

Configure Security once and package it in Serializable's protection config:

```cpp
Security::AES256GCMCipher cipher;
Security::AeadCipherRegistry ciphers;
Security::StaticKeyProvider keys;
Security::ESP32RandomSource randomSource;

ciphers.Register(cipher);
keys.Add(1, Security::AeadAlgorithm::AES256GCM, keyBytes, 32);

Security::DataProtector protector(ciphers, keys, randomSource);

Serializable::SerializationProtectionConfig protection(
    protector,
    "MyApplication.DeviceConfiguration"
);
```

Then save through LittleFS:

```cpp
LittleFSStorage storage(false);
storage.Initialize();

DeviceConfiguration config;

auto result = SaveSerializable(
    storage,
    "/device-config.esdp",
    config,
    protection
);
```

Restore directly into a fresh concrete instance:

```cpp
DeviceConfiguration restored;

auto result = LoadSerializable(
    storage,
    "/device-config.esdp",
    restored,
    protection
);
```

That one call performs:

```text
DeviceConfiguration
      ↓
Serializable BinaryArchive / ESPB
      ↓
Security::IDataProtector
      ↓
protected bytes
      ↓
IFileStorage
```

Loading performs the exact reverse, including normal Serializable schema migration/default/validation behavior after successful authentication.

# The exact same object through Preferences/NVS

```cpp
PreferencesStorage storage("wifi");
storage.Initialize();

SaveSerializable(
    storage,
    "configuration",
    config,
    protection
);

DeviceConfiguration restored;
LoadSerializable(
    storage,
    "configuration",
    restored,
    protection
);
```

The application does not know whether the provider is LittleFS, Preferences, SD, FFat or a test implementation. Only the storage abstraction and locator differ.

# Protection is optional

Existing unprotected typed calls remain unchanged:

```cpp
SaveSerializable(storage, "/config.espb", config);
LoadSerializable(storage, "/config.espb", restored);
```

Only consumers including `ESPressio_Persistence_Serializable_Security.hpp` acquire the protected integration surface.

Persistence does **not** implement cryptography itself. The dependency layering is:

```text
Persistence
    ↓
Serializable protected representation
    ↓
Security::IDataProtector
```

This keeps storage policy, serialization/schema policy and cryptography separate.

# Protected result handling

Protected calls return `ProtectedSerializablePersistenceResult`:

```cpp
auto result = LoadSerializable(
    storage,
    "/config.esdp",
    restored,
    protection
);

if (!result) {
    Serial.printf("storage=%s protected-serialization=%s\n",
        StorageStatusName(result.Storage),
        Serializable::ProtectedSerializationStatusName(
            result.Serialization.Status
        ));

    if (!result.Serialization.SecurityResult.Success) {
        Serial.printf("security error=%u message=%s\n",
            static_cast<unsigned>(result.Serialization.SecurityResult.Error),
            result.Serialization.SecurityResult.Message.c_str());
    }
}
```

This lets callers distinguish storage/media failures from key/authentication failures and from schema/deserialization failures.

# File atomicity

Protected file saves still use `AtomicFileStore` automatically when the backend advertises rename support:

```text
serialize
protect
write temporary
backup existing target
promote temporary
rollback on promotion failure
```

The protected overload accepts `preferAtomicFileReplace=false` when a caller deliberately wants direct replacement.

# Typed unprotected persistence

The ordinary typed APIs introduced in 0.2.0 are still available through `ESPressio_Persistence_Serializable.hpp` and use ESPB `BinaryArchive` directly.

```cpp
LittleFSStorage files(false);
files.Initialize();

DeviceConfiguration source;
SaveSerializable(files, "/config.espb", source);

DeviceConfiguration restored;
LoadSerializable(files, "/config.espb", restored);
```

The same calls work with `PreferencesStorage` using a key instead of a path.

# Raw storage remains first-class

Typed persistence is layered above the same low-level APIs. Applications can continue to use bounded byte-oriented storage directly.

```cpp
uint8_t buffer[128];
std::size_t bytesRead = 0;
storage.Read("/config.bin", 0, buffer, sizeof(buffer), bytesRead);
```

`AtomicFileStore` remains available independently:

```cpp
AtomicFileStore atomic(storage);
atomic.Replace("/config.bin", bytes, size);
```

# Backend examples

LittleFS:

```cpp
LittleFSStorage storage(false); // never format automatically
storage.Initialize();
```

Preferences/NVS:

```cpp
PreferencesStorage settings("camera");
settings.Initialize();
```

External SD/SPI:

```cpp
SDStorage sd(5);
sd.Initialize();
```

SD/MMC:

```cpp
SDMMCStorage sdmmc(true);
sdmmc.Initialize();
```

# Capability discovery

```cpp
if (HasCapability(storage.GetCapabilities(), StorageCapability::Removable)) {
    // Treat media disappearance as an expected runtime condition.
}
```

Capabilities expose hierarchical/key-value semantics, directories, rename, append, removable media, capacity reporting and atomic-replacement suitability.

# Format-on-failure policy

LittleFS, SPIFFS and FFat accept `formatOnFailure`. The default is deliberately `false` so a mount failure does not silently destroy persisted data.

```cpp
LittleFSStorage conservative(false);
LittleFSStorage recoverByFormatting(true);
```

# Host testing

```cpp
MemoryFileStorage files;
files.Initialize();

MemoryKeyValueStorage values;
values.Initialize();
```

Both typed and protected typed APIs can be exercised against these implementations, allowing application persistence/security behavior to be tested without hardware.

# Architecture

```text
Application object
       |
       v
Serializable
       |
       +---- ordinary ESPB --------------------+
       |                                       |
       +---- optional Security protection -----+
                                               |
                                    Persistence storage
                                      /            \
                               IFileStorage   IKeyValueStorage
```

Dependency direction:

```text
Persistence core
    -> none

Persistence Serializable integration
    - - -> Serializable >= 0.11.3 < 1.0.0

Persistence protected Serializable integration
    - - -> Serializable >= 0.11.3 < 1.0.0
            - - -> Security >= 0.4.2 < 1.0.0
```

Persistence itself never depends directly on a cipher, key provider or concrete Security implementation.

See [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

# Reliability principles

1. Initialization failure is visible.
2. Automatic destructive formatting is opt-in.
3. Raw reads are caller-bounded.
4. Typed archive decoding is bounded.
5. Partial writes are never success.
6. Filesystem and key/value semantics remain distinct.
7. Atomic file replacement is explicit/capability-aware.
8. Serializable persistence retains schema-evolution support.
9. Protected persistence authenticates before deserialization.
10. Optional integrations do not force dependencies on core-only consumers.

# Testing

Coverage includes raw backend conformance, atomic replacement and rollback, typed file/key-value round trips, malformed data, resource limits, protected file/key-value round trips, authenticated-context rejection, atomic cleanup and unprotected compatibility. CI additionally compiles the protected ESP32 LittleFS/Preferences surface against released Serializable 0.11.3 and Security 0.4.2, with Observable 3.0.2.

# License

Apache License 2.0. See [LICENSE](LICENSE).
