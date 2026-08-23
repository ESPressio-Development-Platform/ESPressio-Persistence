# ESPressio Persistence

A capability-aware persistence foundation for the ESPressio Development Platform.

ESPressio Persistence gives application code a stable way to store and retrieve data without coupling domain logic to LittleFS, SPIFFS, FAT, SD cards or ESP32 Preferences/NVS.

## Current version — 0.2.0

0.2.0 adds an **optional ESPressio Serializable integration** so a Serializable object can be persisted and reconstructed through any Persistence file or key/value backend while the core Persistence API remains dependency-free.

## Why Persistence is split into two storage concepts

A filesystem and NVS are not the same thing. Pretending they are leads to leaky APIs and surprising behaviour.

Persistence therefore exposes two explicit contracts:

```text
IFileStorage
    hierarchical paths
    files/directories
    bounded reads
    replace/append writes
    rename/list/stat

IKeyValueStorage
    namespace-owned keys
    compact binary values
    replace/read/remove/clear
```

Both inherit `IStorageBackend`, which supplies initialization, readiness, capability discovery and storage statistics.

## Which backend should I choose?

| Need | Recommended backend |
| --- | --- |
| Normal configuration/files on ESP32 internal flash | **LittleFS** |
| Existing project already using SPIFFS | **SPIFFS** |
| FAT semantics on an internal flash partition | **FFat** |
| Small settings, flags, IDs, calibration values | **Preferences/NVS** |
| Removable storage using a generic SPI SD module | **SD** |
| Higher-throughput/native SD interface | **SD_MMC** |
| Desktop/unit tests | **MemoryFileStorage / MemoryKeyValueStorage** |

For new ESP32 applications, start with **LittleFS** when you need files and **Preferences/NVS** when you only need small settings.

## Installation

Core Persistence:

```ini
lib_deps =
    espressio-development-platform/ESPressio-Persistence@^0.2.0
```

Core/host interfaces:

```cpp
#include <ESPressio_Persistence.hpp>
```

ESP32 concrete backends:

```cpp
#include <ESPressio_ESP32Persistence.hpp>
```

### Optional Serializable integration

Typed persistence is deliberately opt-in. Add Serializable alongside Persistence:

```ini
lib_deps =
    espressio-development-platform/ESPressio-Persistence@^0.2.0
    espressio-development-platform/ESPressio-Serializable@^0.10.3
```

Then include:

```cpp
#include <ESPressio_Persistence_Serializable.hpp>
```

Persistence does **not** list Serializable as a mandatory package dependency. Applications that only need raw file/key-value storage do not acquire it.

# Typed Serializable persistence

A Serializable object does not need storage-specific methods. Its normal ESPressio Serializable schema remains authoritative:

```cpp
class DeviceConfiguration final
    : public ESPressio::Serializable::Serializable<DeviceConfiguration> {

    ESPRESSIO_SERIALIZABLE_TYPE(DeviceConfiguration)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)

private:
    uint32_t _sampleRate = 48000;
    std::string _deviceName = "camera-a";
    bool _enabled = true;

public:
    ESPRESSIO_SERIALIZABLE_PROPERTIES(
        ESPRESSIO_PROPERTY("sampleRate", _sampleRate),
        ESPRESSIO_PROPERTY("deviceName", _deviceName),
        ESPRESSIO_PROPERTY("enabled", _enabled)
    )
};
```

## Save/load through LittleFS

```cpp
LittleFSStorage storage(false);
storage.Initialize();

DeviceConfiguration configuration;

auto saved = SaveSerializable(
    storage,
    "/device-config.espb",
    configuration
);

DeviceConfiguration restored;
auto loaded = LoadSerializable(
    storage,
    "/device-config.espb",
    restored
);
```

The persisted representation is ESPressio Serializable's **ESPB BinaryArchive**. This is compact, representation-neutral at the model layer, carries schema-version information, and retains the tree/archive path required for Serializable schema migration.

File writes automatically use `AtomicFileStore` when the selected backend advertises rename support. A backend without rename still works: the typed layer falls back to ordinary replace semantics instead of rejecting the storage implementation.

## The same type through Preferences/NVS

```cpp
PreferencesStorage storage("camera");
storage.Initialize();

DeviceConfiguration configuration;

SaveSerializable(storage, "device", configuration);

DeviceConfiguration restored;
LoadSerializable(storage, "device", restored);
```

There is no NVS-specific method on `DeviceConfiguration` and no filesystem-specific method either. Only the locator changes from a file path to a key.

## Backend-independent application code

When the application requires file semantics:

```cpp
template<typename T>
SerializablePersistenceResult SaveConfig(
    IFileStorage& storage,
    const T& configuration
) {
    return SaveSerializable(
        storage,
        "/configuration.espb",
        configuration
    );
}
```

The caller can supply LittleFS, SPIFFS, FFat, SD, SD_MMC or `MemoryFileStorage`.

For key/value semantics:

```cpp
template<typename T>
SerializablePersistenceResult SaveConfig(
    IKeyValueStorage& storage,
    const T& configuration
) {
    return SaveSerializable(storage, "configuration", configuration);
}
```

The caller can supply Preferences/NVS or `MemoryKeyValueStorage`.

## Result and diagnostics

Typed operations return `SerializablePersistenceResult` rather than collapsing every failure into a Boolean.

```cpp
auto result = LoadSerializable(storage, "/config.espb", config);

if (!result) {
    Serial.printf(
        "typed persistence=%s storage=%s\n",
        SerializablePersistenceStatusName(result.Status),
        StorageStatusName(result.Storage)
    );

    for (const auto& issue : result.Deserialization.Issues()) {
        Serial.printf(
            "schema issue: %s - %s\n",
            issue.Path.c_str(),
            issue.Message.c_str()
        );
    }
}
```

Typed status values distinguish:

- invalid locator/arguments;
- underlying storage failure;
- serialization failure;
- configured payload-size limit violations;
- malformed/truncated persisted ESPB data; and
- schema/deserialization/validation failures.

The underlying `StorageStatus` is retained for conditions such as `NotFound`, `NoSpace`, `PermissionDenied` and `CorruptData`.

## Bounded decoding

Persistence defaults to a 64 KiB maximum typed payload so corrupt or inappropriate persistent data cannot casually trigger an unbounded allocation on an embedded device.

```cpp
SerializablePersistenceOptions options;
options.MaximumPayloadBytes = 8 * 1024;
options.DecodeLimits.MaximumDepth = 12;
options.DecodeLimits.MaximumTotalNodes = 512;

LoadSerializable(storage, "/config.espb", config, options);
```

Serializable's BinaryArchive decode limits remain available for depth, node count, object members, arrays, property names and strings.

## Schema evolution

Persisted state commonly survives firmware upgrades. The default typed persistence representation therefore uses `BinaryArchive`, not the lower-overhead direct-binary fast path.

`BinaryArchive` reconstructs Serializable's tree representation, allowing `DeserializeDetailed()` to apply declared aliases, defaults, validation and structural schema migrations before populating the current object.

This design intentionally favours long-lived persisted-data correctness over shaving the final allocation from a short-lived wire packet.

# Raw persistence APIs

Typed persistence sits above the existing byte-oriented interfaces. Nothing in 0.2.0 removes or changes the 0.1.x raw APIs.

## Reliable configuration file with LittleFS

```cpp
LittleFSStorage storage(false);  // false: never format automatically

if (storage.Initialize() == StorageStatus::Success) {
    AtomicFileStore atomic(storage);
    const char json[] = R"({"mode":"camera","enabled":true})";

    atomic.Replace(
        "/config.json",
        reinterpret_cast<const uint8_t*>(json),
        sizeof(json) - 1
    );
}
```

`AtomicFileStore` writes a temporary file, moves the old value to a backup, promotes the new value, and attempts rollback if promotion fails. Final power-loss guarantees still depend on the filesystem and physical medium.

## Bounded raw read

```cpp
uint8_t buffer[128];
std::size_t bytesRead = 0;

storage.Read(
    "/config.json",
    0,
    buffer,
    sizeof(buffer),
    bytesRead
);
```

The raw contract does not force a `String`, `std::vector`, JSON document or heap allocation on low-level callers.

## Preferences/NVS raw values

```cpp
PreferencesStorage settings("camera");
settings.Initialize();

uint32_t exposureCounter = 42;
settings.Write(
    "counter",
    reinterpret_cast<const uint8_t*>(&exposureCounter),
    sizeof(exposureCounter)
);
```

Use Preferences when data naturally looks like `key -> small value`; do not invent pseudo-filesystem paths inside NVS.

## External SD over SPI

```cpp
SDStorage sd(5); // chip-select pin
if (sd.Initialize() == StorageStatus::Success) {
    // Same IFileStorage API as LittleFS.
}
```

## Native SD/MMC

```cpp
SDMMCStorage sdmmc(true); // one-bit bus mode
sdmmc.Initialize();
```

# Capability discovery

```cpp
if (HasCapability(storage.GetCapabilities(), StorageCapability::Removable)) {
    // Media disappearance is an expected runtime condition.
}

if (HasCapability(storage.GetCapabilities(), StorageCapability::Rename)) {
    AtomicFileStore atomic(storage);
}
```

Capabilities describe hierarchical storage, key/value storage, directories, rename, append, removable media, capacity reporting and atomic-replacement suitability.

# Format-on-failure policy

LittleFS, SPIFFS and FFat constructors accept `formatOnFailure`:

```cpp
LittleFSStorage conservative(false);
LittleFSStorage recoverByFormatting(true);
```

The default is deliberately **false**. Mount failure should not silently destroy persistent data.

# Testing application logic on a desktop

```cpp
MemoryFileStorage storage;
storage.Initialize();

DeviceConfiguration configuration;
SaveSerializable(storage, "/config.espb", configuration);
```

The same typed calls can be exercised against `MemoryKeyValueStorage`, allowing domain persistence logic and schema evolution to be tested without hardware.

# Architecture

```text
Serializable object
        |
        | optional typed integration
        v
ESPB BinaryArchive
        |
        +--------------------------+
        |                          |
 IFileStorage               IKeyValueStorage
        |                          |
LittleFS/SPIFFS/          Preferences/NVS
FFat/SD/SD_MMC            MemoryKeyValue
MemoryFile
```

Dependency direction remains deliberate:

```text
Persistence core
    -> no required ESPressio dependencies

Persistence Serializable integration
    - - -> ESPressio Serializable >= 0.10.3 < 1.0.0
```

Serializable remains lower-order and knows nothing about Persistence or storage media.

See [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

# Reliability principles

1. Mount/initialization failure is visible to callers.
2. Automatic destructive formatting is opt-in.
3. Raw read operations are bounded by caller-provided buffers.
4. Typed reads are bounded by explicit payload/decode limits.
5. Partial writes are never reported as success.
6. Filesystem and key/value semantics remain distinct.
7. Application code can be tested against non-hardware backends.
8. Atomic file replacement is explicit and capability-aware.
9. Persisted Serializable data retains schema-evolution support.
10. Optional integrations do not force dependencies onto core-only consumers.

# Tests

Host tests cover lifecycle, validation, capabilities, bounded reads, replace/append behaviour, stat/rename/remove, directory/listing behaviour, atomic replacement, key/value sizing and insufficient-buffer behaviour.

The optional Serializable suite additionally verifies:

- file-backed typed round-trip;
- key/value typed round-trip;
- atomic file cleanup;
- non-rename fallback;
- missing persisted values;
- malformed payload detection;
- configured payload limits; and
- initialization/argument failures.

CI also compiles the ESP32 surface with ESPressio Serializable 0.10.3 and validates typed calls against LittleFS and Preferences/NVS.

See `examples/SerializablePersistence/` for the complete ESP32 example.

# License

Apache License 2.0. See [LICENSE](LICENSE).
