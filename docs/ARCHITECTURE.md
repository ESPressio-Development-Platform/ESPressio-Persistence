# Architecture

ESPressio Persistence keeps storage mechanics below domain serialization and application policy.

```text
application/domain objects
        |
        | optional typed persistence
        v
ESPressio Serializable / ESPB BinaryArchive
        |
        +-------------------------------+
        |                               |
  IFileStorage                    IKeyValueStorage
        |                               |
 filesystem media                  NVS-like stores
```

## Design boundaries

- `IStorageBackend` owns lifecycle, readiness, capability discovery and statistics.
- `IFileStorage` owns hierarchical byte storage semantics.
- `IKeyValueStorage` owns namespaced key/value byte storage semantics.
- `AtomicFileStore` is a policy/helper layered above `IFileStorage`, not a filesystem assumption hidden inside `Write()`.
- Concrete ESP32 adapters translate Arduino framework APIs into these contracts.
- Host-memory adapters allow application persistence logic to be tested without hardware.
- `ESPressio_Persistence_Serializable.hpp` is an optional adapter layered above the raw storage interfaces.
- Serializable remains lower-order: it defines object schema/representation facilities and knows nothing about Persistence.

## Typed persistence representation

The initial typed integration uses Serializable's ESPB `BinaryArchive` rather than the direct-binary fast path.

This is deliberate for persisted data. `BinaryArchive` reconstructs the tree representation used by `DeserializeDetailed()`, allowing aliases, defaults, validation and declared structural migrations to be applied when firmware reads data written by an earlier schema version.

```text
Serializable object
        |
        v
BinaryArchive (ESPB)
        |
        v
byte payload
        |
        v
IFileStorage / IKeyValueStorage
```

The storage backend never interprets the object schema. LittleFS, Preferences/NVS, SD and memory backends all receive the same opaque byte payload.

## Reliability boundaries

Typed persistence adds bounded full-payload materialization above the low-level bounded interfaces. `SerializablePersistenceOptions::MaximumPayloadBytes` defaults to 64 KiB, while Serializable's `BinaryArchiveDecodeLimits` constrain nested structure during decode.

For file backends, save operations prefer `AtomicFileStore` whenever rename is supported. File backends without rename remain valid and fall back to ordinary replacement. Key/value stores use their native replacement semantics.

`SerializablePersistenceResult` retains both integration-level status and underlying `StorageStatus`, and carries structured Serializable deserialization diagnostics where schema/validation errors occur.

Checksums beyond ESPB structural validation, generation journals, encryption and application-specific recovery policy remain appropriate future layers above these contracts.
