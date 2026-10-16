# run_smoke_status.cmake - status must emit schema antinomia/1 JSON.
# Module: test (CMake).
# Owns: smoke check for the status stub command.
# Depends: antinomia executable.
if(NOT DEFINED ANTINOMIA)
    message(FATAL_ERROR "ANTINOMIA is required")
endif()
execute_process(
    COMMAND "${ANTINOMIA}" status
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE run_rc)
if(NOT run_rc STREQUAL "0")
    message(FATAL_ERROR "status exited ${run_rc}: ${err}\n${out}")
endif()
if(NOT out MATCHES "antinomia/1")
    message(FATAL_ERROR "status missing schema: ${out}")
endif()
if(NOT out MATCHES "linux-ptrace|windows-debug|linux-stub|windows-stub|stub")
    message(FATAL_ERROR "status missing backend name: ${out}")
endif()
if(NOT out MATCHES "\"tid\"")
    message(FATAL_ERROR "status missing tid: ${out}")
endif()
if(NOT out MATCHES "last_event")
    message(FATAL_ERROR "status missing last_event: ${out}")
endif()
