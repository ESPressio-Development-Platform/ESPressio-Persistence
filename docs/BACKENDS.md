# Persistence backend guide

## File-oriented backends

| Backend | Medium | Best fit | Notes |
| --- | --- | --- | --- |
| `LittleFSStorage` | internal SPI flash | normal application files/configuration | Recommended default for new ESP32 file persistence. |
| `SPIFFSStorage` | internal SPI flash | SPI flash filesystem applications | Requires a matching SPIFFS partition. |
| `FFatStorage` | internal flash FAT | FAT semantics / larger file-oriented stores | Requires a compatible FFat partition. |
| `SDStorage` | external SD over SPI | removable/high-capacity data | Works with generic SPI SD modules; configure CS pin. |
| `SDMMCStorage` | external/integrated SD over SDMMC | higher-throughput removable data | Requires suitable SDMMC-capable pins/hardware. |

All file backends expose bounded reads, replace/append writes, metadata, one-level directory listing, directory operations and rename through `IFileStorage`.

## Key/value backend

`PreferencesStorage` wraps ESP32 Preferences/NVS and implements `IKeyValueStorage`. Use it for small configuration values, flags, identifiers and compact binary records where a hierarchical filesystem would be unnecessary.

ESP32 NVS limits namespace names and keys to 15 characters. `PreferencesStorage` validates those limits and reports `InvalidArgument` before invoking the backend.

## Host/test backends

`MemoryFileStorage` and `MemoryKeyValueStorage` implement the same public contracts without Arduino dependencies. They are useful for host tests, simulation, dependency injection and application-level persistence tests. `MemoryFileStorage` deliberately mirrors filesystem behavior for one-level directory listing and refuses to remove non-empty directories.

## Atomicity and power loss

`AtomicFileStore` now requires `AtomicReplace`, `DurableFileSync` and `DurableDirectorySync` plus matching implementations of `ReplaceFileAtomically`, `SyncFile` and `SyncDirectory`. A rename-only backend is rejected. `AtomicFileRecordStore` additionally requires `BoundedOperations`; declaring a capability requires backend-specific evidence, not an optimistic default.

`MemoryFileStorage` is volatile and does not advertise these guarantees. Its general storage behavior remains useful for tests. The test-only `DurableFileModel` uses separate live/durable namespaces and data to model interrupted publication; it is not a production backend or hardware durability certification.

ESP32 filesystem/NVS implementations must be audited and implement a proven durable/bounded path in their platform migration tranche. Ordinary Preferences/NVS Write semantics are insufficient evidence. If a backend cannot prove native atomicity, its platform composition needs a bounded journal/double-record implementation over an appropriate durable medium. Unsupported backends fail initialization; no replacement fallback weakens P4 or Command/State durability.
