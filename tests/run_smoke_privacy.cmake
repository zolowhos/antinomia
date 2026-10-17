# run_smoke_privacy.cmake - privacy tool confirms telemetry-free + seed echo.
# Module: test (CMake).
# Owns: smoke check for antinomia privacy / --seed.
# Depends: antinomia executable.
if(NOT DEFINED ANTINOMIA)
    message(FATAL_ERROR "ANTINOMIA is required")
endif()
execute_process(
    COMMAND "${ANTINOMIA}" privacy
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE run_rc)
if(NOT run_rc STREQUAL "0")
    message(FATAL_ERROR "privacy exited ${run_rc}: ${err}")
endif()
if(NOT out MATCHES "antinomia/1")
    message(FATAL_ERROR "privacy missing schema: ${out}")
endif()
if(NOT out MATCHES "\"telemetry\":false")
    message(FATAL_ERROR "privacy missing telemetry:false: ${out}")
endif()
execute_process(
    COMMAND "${ANTINOMIA}" privacy --seed 7
    OUTPUT_VARIABLE out2
    ERROR_VARIABLE err2
    RESULT_VARIABLE rc2)
if(NOT rc2 STREQUAL "0")
    message(FATAL_ERROR "privacy --seed exited ${rc2}: ${err2}")
endif()
if(NOT out2 MATCHES "\"seed_set\":true")
    message(FATAL_ERROR "privacy --seed missing seed_set: ${out2}")
endif()
if(NOT out2 MATCHES "\"seed\":7")
    message(FATAL_ERROR "privacy --seed missing seed: ${out2}")
endif()
