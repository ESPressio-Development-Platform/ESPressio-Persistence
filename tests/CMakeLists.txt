add_executable(espressio_persistence_tests persistence_tests.cpp)
target_link_libraries(espressio_persistence_tests PRIVATE ESPressioPersistence)
add_test(NAME espressio_persistence_tests COMMAND espressio_persistence_tests)

add_executable(espressio_atomic_record_tests atomic_record_tests.cpp)
target_link_libraries(espressio_atomic_record_tests PRIVATE ESPressioPersistence)
add_test(NAME espressio_atomic_record_tests COMMAND espressio_atomic_record_tests)

find_package(Threads REQUIRED)
add_executable(espressio_runtime_incarnation_tests runtime_incarnation_tests.cpp)
target_link_libraries(espressio_runtime_incarnation_tests PRIVATE ESPressioPersistence Threads::Threads)
foreach(case unsupported missing mismatch corrupt exhausted final ambiguous_old ambiguous_new failed_write concurrent success)
    add_test(NAME incarnation_${case} COMMAND espressio_runtime_incarnation_tests ${case})
endforeach()
if(UNIX)
    add_executable(espressio_runtime_incarnation_powerloss runtime_incarnation_powerloss_test.cpp)
    target_link_libraries(espressio_runtime_incarnation_powerloss PRIVATE ESPressioPersistence)
    add_test(NAME incarnation_powerloss_before_install COMMAND espressio_runtime_incarnation_powerloss)
endif()

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
