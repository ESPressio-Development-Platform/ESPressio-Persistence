#pragma once

#include <ESPressio_Persistence.hpp>

#if defined(ARDUINO_ARCH_ESP32)
#include <backends/esp32/ESPressio_LittleFSStorage.hpp>
#include <backends/esp32/ESPressio_SPIFFSStorage.hpp>
#include <backends/esp32/ESPressio_FFatStorage.hpp>
#include <backends/esp32/ESPressio_PreferencesStorage.hpp>
#include <backends/esp32/ESPressio_SDStorage.hpp>
#include <backends/esp32/ESPressio_SDMMCStorage.hpp>
#endif
