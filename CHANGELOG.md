# Changelog

All notable changes to this project are documented in this file.

The structure follows Keep a Changelog and Semantic Versioning principles.

## [0.3.2] - 2026-08-24

### Changed

- Updated typed Serializable persistence validation to ESPressio Serializable `0.11.3`.
- Updated protected Serializable persistence validation to ESPressio Security `0.4.2` through Serializable's optional Security integration.
- Preserved ESPressio Observable `3.0.2` for Security integration validation.
- Updated package and Arduino metadata for Persistence `0.3.2`.
- Reconciled stale README and dependency documentation that still described the 0.3.0 generation while package metadata had already advanced to 0.3.1.
- Updated host and ESP32 integration CI to released dependency tags only.

### Architecture

- Persistence core remains dependency-free.
- Serializable integration remains opt-in.
- Protected Serializable persistence continues to consume Security indirectly through Serializable's protection API; Persistence does not acquire a direct Security dependency.

### Compatibility

- No Persistence storage API, backend contract, typed-persistence API, protected-persistence API, atomic replacement behaviour, schema behaviour, or persisted representation changes.
- Existing ESPB and ESDP persisted data remain compatible.

### Tracking

- Closes #7.

## [0.3.0] - 2026-08-23

### Added

- Added optional `ESPressio_Persistence_Serializable_Security.hpp` integration.
- Added protected `SaveSerializable()` / `LoadSerializable()` overloads for every `IFileStorage` implementation.
- Added protected `SaveSerializable()` / `LoadSerializable()` overloads for every `IKeyValueStorage` implementation.
- Added `ProtectedSerializablePersistenceResult`, preserving storage status separately from Serializable/Security protection and deserialization status.
- Preserved automatic `AtomicFileStore` use for protected file saves where rename is supported, with an explicit ordinary-replace fallback option.
- Added protected file and key/value round-trip tests, authenticated-context failure coverage, atomic cleanup verification and unprotected compatibility coverage.
- Extended ESP32 CI compile validation to protected LittleFS and Preferences/NVS typed persistence.

### Design

- Persistence does not implement encryption and does not depend directly on cryptographic algorithms.
- The protected integration consumes `SerializationProtectionConfig` from Serializable 0.11.x; Serializable delegates protection to Security's `IDataProtector`.
- Core byte storage and ordinary typed persistence remain usable without Security.

### Compatibility

- Backward-compatible interface extension from 0.2.0 to 0.3.0.
- Existing unprotected `SaveSerializable()` / `LoadSerializable()` calls remain unchanged.

### Tracking

- Implements #3.

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
- Dependency documentation records ESPressio Serializable as an optional integration.

### Compatibility

- Existing 0.1.x storage interfaces and concrete backends remain source-compatible.
- ESPressio Serializable remains optional; core-only Persistence consumers acquire no new required ESPressio dependency.

## [0.1.0] - 2026-08-23

### Added

- Initial ESPressio Persistence architecture.
- Capability-aware `IStorageBackend`, `IFileStorage` and `IKeyValueStorage` contracts.
- Bounded byte-oriented read/write interfaces.
- `AtomicFileStore` best-effort replacement helper.
- Host `MemoryFileStorage` and `MemoryKeyValueStorage` implementations.
- ESP32 LittleFS, SPIFFS, FFat, Preferences/NVS, SD/SPI and SD_MMC backends.
- Backend-selection documentation, host unit tests and ESP32 compile validation.
