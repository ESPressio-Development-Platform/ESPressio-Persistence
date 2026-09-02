# Platform Abstractions Audit Trail

This file records Persistence changes made during the platform-abstraction tranche tracked by issue #10.

## 2026-08-27

### Platform implementation ownership
- Created working branch `feature/10-platform-storage-abstractions` from `main` before making changes.
- Kept `IStorageBackend`, `IKeyValueStorage` and `IFileStorage` in ESPressio-Persistence because they describe Persistence storage semantics rather than generic hardware primitives.
- Moved the ESP32 concrete storage implementations to ESPressio-ESP32: Preferences/NVS, LittleFS, SPIFFS, FFat, SD/SPI and SD_MMC.
- Moved the shared Arduino-ESP32 `fs::FS` file-storage base to ESPressio-ESP32.
- Removed the ESP32 backend bundle and `src/backends/esp32` implementation files from this repository.

## Preserved responsibilities
- In-memory backends remain in Persistence because they are portable test/runtime implementations rather than hardware-target integrations.
- Atomic file replacement, serialization integration, migration, security integration and persistence lifecycle remain owned by ESPressio-Persistence.

## Boundary rule

Persistence owns storage-domain contracts and policy. Concrete target implementations of those contracts belong in platform libraries such as ESPressio-ESP32.
