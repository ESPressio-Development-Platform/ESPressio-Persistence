# Architecture

ESPressio Persistence keeps storage mechanics below domain serialization and cryptographic policy.

```text
application/domain object
        |
        v
ESPressio Serializable
        |
        +---- ordinary ESPB --------------------+
        |                                       |
        +---- optional authenticated protection |
                    |                            |
                    v                            |
             ESPressio Security                  |
                    |                            |
                    +----------------------------+
                                                 |
                          +----------------------+
                          |                      |
                    IFileStorage          IKeyValueStorage
                          |                      |
                  filesystem media          NVS-like stores
```

## Design boundaries

- `IStorageBackend` owns lifecycle, readiness, capability discovery and statistics.
- `IFileStorage` owns hierarchical byte-storage semantics.
- `IKeyValueStorage` owns namespaced key/value byte-storage semantics.
- `AtomicFileStore` is a policy/helper layered above `IFileStorage`.
- Concrete ESP32 adapters translate platform storage APIs into ESPressio contracts.
- Host-memory adapters allow application persistence logic to be tested without hardware.
- `ESPressio_Persistence_Serializable.hpp` adds optional unprotected typed persistence.
- `ESPressio_Persistence_Serializable_Security.hpp` adds optional protected typed persistence.
- Persistence never selects ciphers, manages keys or performs encryption itself.

## Typed persistence representation

Typed persistence uses Serializable's ESPB `BinaryArchive`, retaining tree-based migration/default/alias/validation support for records that outlive the firmware version that produced them.

Unprotected:

```text
Serializable object
    -> BinaryArchive / ESPB
    -> storage bytes
```

Protected:

```text
Serializable object
    -> BinaryArchive / ESPB
    -> SerializationProtectionConfig
    -> Security::IDataProtector
    -> authenticated protected bytes
    -> storage
```

Loading reverses this order. Authentication/unprotection occurs before BinaryArchive decoding or model deserialization.

## Dependency boundaries

```text
Persistence core
    -> none

Persistence typed integration
    - - -> Serializable

Persistence protected typed integration
    - - -> Serializable
            - - -> Security
```

Persistence has no direct cryptographic dependency. Serializable remains responsible for representation and delegates protection through its optional Security integration.

## Reliability boundaries

- Typed payloads are bounded by explicit archive limits.
- Protected payloads authenticate before parsing.
- File saves prefer `AtomicFileStore` when rename is supported.
- File backends without rename fall back to ordinary replacement when requested.
- Key/value stores use their backend replacement semantics.
- Result types retain storage failure separately from serialization/security failure.
- Power-loss durability still depends on the physical filesystem/media; protection guarantees confidentiality/integrity, not transactional storage by itself.
