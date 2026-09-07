add_executable(espressio_persistence_tests persistence_tests.cpp)
target_link_libraries(espressio_persistence_tests PRIVATE ESPressioPersistence)
add_test(NAME espressio_persistence_tests COMMAND espressio_persistence_tests)

if(ESPRESSIO_LOGGING_SOURCE_DIR AND ESPRESSIO_TIMING_SOURCE_DIR AND ESPRESSIO_SYSTEM_SOURCE_DIR)
    add_executable(espressio_persistent_log_sink_tests persistent_log_sink_tests.cpp)
    target_link_libraries(espressio_persistent_log_sink_tests PRIVATE ESPressioPersistence)
    add_test(NAME espressio_persistent_log_sink_tests COMMAND espressio_persistent_log_sink_tests)
endif()

if(ESPRESSIO_LOGGING_SOURCE_DIR AND ESPRESSIO_TASK_SOURCE_DIR AND ESPRESSIO_SYSTEM_SOURCE_DIR)
    add_executable(espressio_persistent_log_worker_compile_tests persistent_log_worker_compile_tests.cpp)
    target_link_libraries(espressio_persistent_log_worker_compile_tests PRIVATE ESPressioPersistence)
    add_test(
        NAME espressio_persistent_log_worker_compile_tests
        COMMAND espressio_persistent_log_worker_compile_tests
    )
endif()
