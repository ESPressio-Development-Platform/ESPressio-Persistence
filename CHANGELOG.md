# Changelog

All notable changes to this project are documented in this file.

The structure follows Keep a Changelog and Semantic Versioning principles.

## [0.2.0] - 2026-08-23

### Added

- Optional `ESPressio_Persistence_Serializable.hpp` integration layer targeting ESPressio Serializable `>= 0.10.3 < 1.0.0`.
- Generic `SaveSerializable()` / `LoadSerializable()` overloads for every `IFileStorage` implementation.
- Generic `SaveSerializable()` / `LoadSerializable()` overloads for every `IKeyValueStorage` implementation.
- ESPB `BinaryArchive` persisted representation so typed storage remains compact while retaining Serializable schema-version and structural migration support.
- `SerializablePersistenceOptions` with bounded payload size, BinaryArchive decode limits, deserialization options and atomic-file preference.
- `SerializablePersistenceResult` preserving typed-persistence status, underlying `StorageStatus`, payload size and structured Serializable deserialization diagnostics.
- Capability-aware file save policy: `AtomicFileStore` is used automatically when rename is available, with ordinary replacement fallback for file backends without rename support.
- Host tests covering typed file/key-value round trips, malformed payloads, missing values, payload limits, initialization/argument failures, atomic cleanup and non-rename fallback.
- ESP32 CI compile validation for typed persistence over LittleFS and Preferences/NVS.
- `SerializablePersistence` example demonstrating the same Serializable type persisted through both LittleFS and Preferences/NVS.
- Adoption-focused README documentation covering backend-independent typed persistence, diagnostics, decode bounds and schema evolution.

### Changed

- Package version advanced to 0.2.0 because this release extends the public Persistence interface without breaking 0.1.x consumers.
- Dependency documentation now records ESPressio Serializable as an optional integration rather than a future-only direction.

### Compatibility

- Existing 0.1.x storage interfaces and concrete backends remain source-compatible.
- ESPressio Serializable remains optional; core-only Persistence consumers acquire no new required ESPressio dependency.
- Serializable remains lower-order and does not depend on Persistence.

## [0.1.0] - 2026-08-23

### Added

- Initial ESPressio Persistence architecture.
- Capability-aware `IStorageBackend` lifecycle and diagnostics contract.
- Separate `IFileStorage` and `IKeyValueStorage` abstractions so hierarchical filesystems and NVS-style stores retain appropriate semantics.
- Bounded byte-oriented read/write interfaces suitable for embedded applications.
- Storage status, capability, statistics and directory-entry types.
- `AtomicFileStore` best-effort temporary/backup/rename replacement helper.
- Host-friendly `MemoryFileStorage` and `MemoryKeyValueStorage` implementations.
- Optional ESP32 Arduino backends for LittleFS, SPIFFS, FFat, Preferences/NVS, SD over SPI and SD_MMC.
- Backend selection documentation and consumer examples.
- Host unit tests plus ESP32 compile validation in CI.

### Compatibility

- Initial pre-release API; no previous public API exists.
