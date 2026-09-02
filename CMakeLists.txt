cmake_minimum_required(VERSION 3.16)
project(ESPressioPersistence LANGUAGES CXX)

set(
    ESPRESSIO_SYSTEM_SOURCE_DIR
    ""
    CACHE PATH
    "Path to the ESPressio-System working copy used by coordinated host tests"
)

add_library(ESPressioPersistence INTERFACE)
target_include_directories(ESPressioPersistence INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(ESPRESSIO_SYSTEM_SOURCE_DIR)
    target_include_directories(
        ESPressioPersistence
        INTERFACE ${ESPRESSIO_SYSTEM_SOURCE_DIR}/src
    )
endif()
target_compile_features(ESPressioPersistence INTERFACE cxx_std_17)

option(ESPRESSIO_PERSISTENCE_BUILD_TESTS "Build ESPressio Persistence host tests" ON)
if(ESPRESSIO_PERSISTENCE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
