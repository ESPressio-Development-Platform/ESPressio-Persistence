# Getting Started

For raw portable storage contracts and in-memory test backends:

```cpp
#include <ESPressio_Persistence.hpp>
```

Typed Serializable persistence is explicitly opt-in:

```cpp
#include <ESPressio_Persistence_Serializable.hpp>
```

Protected typed persistence is another explicit layer:

```cpp
#include <ESPressio_Persistence_Serializable_Security.hpp>
```

## Raw storage

```cpp
MemoryFileStorage storage;
storage.Initialize();

uint8_t buffer[128];
std::size_t bytesRead = 0;
storage.Read("/config.bin", 0, buffer, sizeof(buffer), bytesRead);
```

## Typed storage

```cpp
SaveSerializable(storage, "/config.espb", configuration);
LoadSerializable(storage, "/config.espb", restored);
```

The same high-level operation works with file or key/value implementations satisfying the appropriate Persistence contract.

## Platform composition

Hardware-specific backends are supplied by the target platform package. Application/domain code should depend on `IFileStorage` or `IKeyValueStorage` rather than concrete filesystem/SDK types.

## Next steps

- [Storage Backends and Capabilities](Storage-Backends-and-Capabilities)
- [Typed Serializable Persistence](Typed-Serializable-Persistence)
- [Reliability and Failure Handling](Reliability-and-Failure-Handling)