# ESPressio Persistence

A capability-aware persistence foundation for the ESPressio Development Platform.

ESPressio Persistence gives application code a stable way to store and retrieve data without coupling domain logic to LittleFS, SPIFFS, FAT, SD cards or ESP32 Preferences/NVS.

## Current version — 0.1.0

The initial release establishes the storage contracts, host-memory test implementations, atomic file replacement helper and optional ESP32 Arduino backends.

## Why Persistence is split into two storage concepts

A filesystem and NVS are not the same thing. Pretending they are leads to leaky APIs and surprising behavior.

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

This makes consuming code depend on the **kind of storage behavior it actually needs**, not on a particular flash library.

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

The M5StickC Plus2-class devices used by the ESPressio laboratory have internal flash but no integrated SD socket, so LittleFS and Preferences are the most immediately useful backends there. SD support is included so the same application architecture can move to ESP32 boards with integrated or externally attached storage without replacing its persistence-facing code.

See [docs/BACKENDS.md](docs/BACKENDS.md) for more detail.

## Installation

```ini
lib_deps =
    espressio-development-platform/ESPressio-Persistence@^0.1.0
```

For development directly from GitHub:

```ini
lib_deps =
    https://github.com/ESPressio-Development-Platform/ESPressio-Persistence.git
```

Core/host interfaces:

```cpp
#include <ESPressio_Persistence.hpp>
```

ESP32 concrete backends:

```cpp
#include <ESPressio_ESP32Persistence.hpp>
```

## Example: reliable configuration file with LittleFS

```cpp
#include <ESPressio_ESP32Persistence.hpp>

using namespace ESPressio::Persistence;

LittleFSStorage storage(false);  // false: never format automatically

void setup() {
    if (storage.Initialize() != StorageStatus::Success) {
        // Mount failure is explicit; decide what your application should do.
        return;
    }

    AtomicFileStore atomic(storage);
    const char json[] = R"({"mode":"camera","enabled":true})";

    StorageStatus status = atomic.Replace(
        "/config.json",
        reinterpret_cast<const uint8_t*>(json),
        sizeof(json) - 1
    );
}
```

Why use `AtomicFileStore`? A direct replace can leave a damaged file if power disappears during the update. The helper writes a temporary file, moves the old value to a backup, promotes the new value, and rolls back when promotion fails.

This is deliberately described as **best-effort atomic replacement**: final power-loss guarantees depend on the filesystem and physical medium. A future record layer can add checksums, generations and recovery scanning above this primitive.

## Example: bounded read without allocating a whole file

```cpp
uint8_t buffer[128];
std::size_t bytesRead = 0;

StorageStatus status = storage.Read(
    "/config.json",
    0,                 // byte offset
    buffer,
    sizeof(buffer),
    bytesRead
);
```

The low-level contract does not force a `String`, `std::vector`, JSON document or heap allocation on the caller. This is intentional for embedded reliability and allows future streaming serializers to sit directly above the interface.

## Example: Preferences/NVS for small settings

```cpp
#include <ESPressio_ESP32Persistence.hpp>

using namespace ESPressio::Persistence;

PreferencesStorage settings("camera");

void setup() {
    if (settings.Initialize() != StorageStatus::Success) {
        return;
    }

    uint32_t exposureCounter = 42;
    settings.Write(
        "counter",
        reinterpret_cast<const uint8_t*>(&exposureCounter),
        sizeof(exposureCounter)
    );

    uint32_t restored = 0;
    std::size_t bytesRead = 0;
    settings.Read(
        "counter",
        reinterpret_cast<uint8_t*>(&restored),
        sizeof(restored),
        bytesRead
    );
}
```

Use Preferences when the data naturally looks like `key -> small value`. Do not create pseudo-filesystem path conventions inside NVS just to make it resemble a filesystem.

## Example: code that does not care which filesystem is underneath

```cpp
void SaveCalibration(
    IFileStorage& storage,
    const uint8_t* bytes,
    std::size_t size
) {
    AtomicFileStore records(storage);
    records.Replace("/calibration.bin", bytes, size);
}
```

The caller can supply `LittleFSStorage`, `FFatStorage`, `SDStorage`, `SDMMCStorage`, or `MemoryFileStorage` without changing `SaveCalibration()`.

That is the central adoption goal of this library.

## External SD over SPI

```cpp
SDStorage sd(5); // CS pin

if (sd.Initialize() == StorageStatus::Success) {
    // Use through IFileStorage just like LittleFS.
}
```

The application owns the wiring/pin decision. SD is marked with the `Removable` capability so higher-level code can make appropriate decisions about media loss and retry/recovery behavior.

## Native SD/MMC

```cpp
SDMMCStorage sdmmc(true); // one-bit bus mode
sdmmc.Initialize();
```

Use this only on boards where the required SDMMC pins are available and connected appropriately.

## Capability discovery

Backends explicitly report what they support:

```cpp
if (HasCapability(storage.GetCapabilities(), StorageCapability::Removable)) {
    // Treat media disappearance as an expected runtime condition.
}

if (HasCapability(storage.GetCapabilities(), StorageCapability::Rename)) {
    AtomicFileStore atomic(storage);
}
```

Capabilities currently describe:

- hierarchical storage;
- key/value storage;
- directories;
- rename;
- append;
- removable media;
- capacity reporting;
- atomic replacement suitability.

## Storage status instead of ambiguous booleans

Operations return `StorageStatus`, distinguishing conditions such as:

- `NotInitialized`
- `InvalidArgument`
- `NotFound`
- `AlreadyExists`
- `NotSupported`
- `PermissionDenied`
- `NoSpace`
- `IoError`
- `PartialWrite`

For diagnostics:

```cpp
Serial.println(StorageStatusName(status));
```

## Format-on-failure policy

LittleFS, SPIFFS and FFat constructors accept `formatOnFailure`.

```cpp
LittleFSStorage conservative(false);
LittleFSStorage recoverByFormatting(true);
```

The default is deliberately **false**. Mount failure should not silently destroy persistent data. Applications that explicitly prefer automatic recovery may opt in.

## Testing application persistence logic on a desktop

```cpp
MemoryFileStorage storage;
storage.Initialize();

SaveCalibration(storage, bytes, length);
```

The memory backends require no Arduino framework and are the same interfaces used by the embedded implementations. This makes failure handling and domain persistence logic much easier to test before hardware is involved.

## Architecture

```text
Application/domain code
        |
        v
 IFileStorage             IKeyValueStorage
        |                       |
  +-----+------+          +-----+-----+
  |     |      |          |           |
Little FFat   SD/...   Preferences   Memory
FS
```

The core has **no required ESPressio dependencies**. This keeps Persistence low in the dependency graph and makes it safe for future consumers such as ESPressio-Web.

Future optional work is expected to add a higher-level Serializable document/record layer rather than making the raw storage interfaces understand JSON, CBOR or application schemas.

See [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md).

## Reliability principles

Persistence 0.1.0 is designed around several rules:

1. Mount/initialization failure must be visible to callers.
2. Automatic destructive formatting must be opt-in.
3. Read operations are bounded by caller-provided buffers.
4. Partial writes must not be reported as success.
5. Removable media is a capability, not an implementation accident.
6. Filesystem and key/value semantics remain distinct.
7. Application code should be testable against a non-hardware backend.
8. Atomic file replacement is explicit rather than implied by ordinary `Write()`.

## Tests

Host tests cover lifecycle, validation, capabilities, bounded reads, replace/append behavior, stat/rename/remove, directory/listing behavior, atomic replacement, key/value sizing and insufficient-buffer behavior.

CI additionally compiles the complete ESP32 backend surface so framework API drift is caught even when no physical SD card or flash partition is available to the runner.

## License

Apache License 2.0. See [LICENSE](LICENSE).
