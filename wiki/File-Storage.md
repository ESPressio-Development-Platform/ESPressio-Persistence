# File Storage

`IFileStorage` models hierarchical, path-oriented storage independently of a particular filesystem implementation.

Typical capabilities include bounded reads, replace/write, append, rename, directory operations, listing and stat/metadata where advertised by the backend.

## Bounded reads

Raw reads are caller-bounded:

```cpp
uint8_t buffer[128];
std::size_t bytesRead = 0;
storage.Read(
    "/config.bin",
    0,
    buffer,
    sizeof(buffer),
    bytesRead
);
```

The caller determines the maximum amount of data admitted into memory.

## Capability-aware operations

Do not assume every file backend supports rename, append, directories, removable-media semantics or atomic replacement. Inspect `StorageCapability` before relying on optional behaviour.

## Concrete backends

Target-specific filesystem implementations belong to the platform provider package. Reusable code should normally accept `IFileStorage&` rather than a LittleFS, FAT, SD or other SDK-specific class.

## Atomic replacement

Where reliable replacement matters and rename semantics are available, use [Atomic File Replacement](Atomic-File-Replacement) rather than manually overwriting the target.