# run_smoke_version.cmake - antinomia version must print schema antinomia/1.
# Module: test (CMake).
# Owns: smoke check for the version command.
# Depends: antinomia executable.
if(NOT DEFINED ANTINOMIA)
    message(FATAL_ERROR "ANTINOMIA is required")
endif()
execute_process(
    COMMAND "${ANTINOMIA}" version
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE run_rc)
if(NOT run_rc STREQUAL "0")
    message(FATAL_ERROR "version exited ${run_rc}: ${err}")
endif()
set(text "${out}${err}")
if(NOT text MATCHES "antinomia")
    message(FATAL_ERROR "version output missing antinomia: ${text}")
endif()
if(NOT text MATCHES "antinomia/1")
    message(FATAL_ERROR "version output missing schema antinomia/1: ${text}")
endif()
