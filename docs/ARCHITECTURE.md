# Architecture

ESPressio Persistence keeps storage mechanics below domain serialization and application policy.

```text
application/domain objects
        |
        | future typed document/record layer
        v
+-------------------------------+
| IFileStorage | IKeyValueStorage|
+-------------------------------+
       |               |
 filesystem media    NVS-like stores
```

## Design boundaries

- `IStorageBackend` owns lifecycle, readiness, capability discovery and statistics.
- `IFileStorage` owns hierarchical byte storage semantics.
- `IKeyValueStorage` owns namespaced key/value byte storage semantics.
- `AtomicFileStore` is a policy/helper layered above `IFileStorage`, not a filesystem assumption hidden inside `Write()`.
- Concrete ESP32 adapters translate Arduino framework APIs into these contracts.
- Host-memory adapters allow application persistence logic to be tested without hardware.

Serialization format, schema migration, checksums, encryption and application-specific validation intentionally remain above the raw storage boundary.
