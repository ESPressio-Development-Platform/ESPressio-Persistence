# Persistence backend guide

## File-oriented backends

| Backend | Medium | Best fit | Notes |
| --- | --- | --- | --- |
| `LittleFSStorage` | internal SPI flash | normal application files/configuration | Recommended default for new ESP32 file persistence. |
| `SPIFFSStorage` | internal SPI flash | legacy applications | Kept for compatibility; prefer LittleFS for new designs. |
| `FFatStorage` | internal flash FAT | FAT semantics / larger file-oriented stores | Requires a compatible FFat partition. |
| `SDStorage` | external SD over SPI | removable/high-capacity data | Works with generic SPI SD modules; configure CS pin. |
| `SDMMCStorage` | external/integrated SD over SDMMC | higher-throughput removable data | Requires suitable SDMMC-capable pins/hardware. |

All file backends expose bounded reads, replace/append writes, metadata, directory operations, rename and listing through `IFileStorage`.

## Key/value backend

`PreferencesStorage` wraps ESP32 Preferences/NVS and implements `IKeyValueStorage`. Use it for small configuration values, flags, identifiers and compact binary records where a hierarchical filesystem would be unnecessary.

NVS key and namespace limits are imposed by ESP-IDF/Arduino Preferences. Keep keys deliberately short and stable.

## Host/test backends

`MemoryFileStorage` and `MemoryKeyValueStorage` implement the same public contracts without Arduino dependencies. They are useful for host tests, simulation, dependency injection and application-level persistence tests.

## Atomicity and power loss

`AtomicFileStore` performs best-effort replacement using a temporary file, backup rename and rollback. This substantially reduces the risk of replacing a valid record with a partially written one, but the guarantees still depend on the underlying filesystem and flash/card behavior. A future record layer can add checksums, generations and recovery scanning above this primitive.

NVS/Preferences updates operate at key/value granularity and are the preferred choice for small settings that need transactional storage behavior.
