# Changelog

All notable changes to this project are documented in this file.

The structure follows Keep a Changelog and Semantic Versioning principles.

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
