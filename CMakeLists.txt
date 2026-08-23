cmake_minimum_required(VERSION 3.16)
project(ESPressioPersistence LANGUAGES CXX)

add_library(ESPressioPersistence INTERFACE)
target_include_directories(ESPressioPersistence INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(ESPressioPersistence INTERFACE cxx_std_17)

option(ESPRESSIO_PERSISTENCE_BUILD_TESTS "Build ESPressio Persistence host tests" ON)
if(ESPRESSIO_PERSISTENCE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
