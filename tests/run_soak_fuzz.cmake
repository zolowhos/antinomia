# run_soak_fuzz.cmake - CI-bounded crash-free soak via scripts/soak_fuzz.sh.
# Module: test (CMake).
# Owns: invoke soak_fuzz with short wall budget for ctest.
# Depends: ANTINOMIA, INFERIOR, SOAK_SCRIPT from tests/CMakeLists.txt.

if(NOT ANTINOMIA OR NOT INFERIOR OR NOT SOAK_SCRIPT)
  message(FATAL_ERROR "ANTINOMIA, INFERIOR, SOAK_SCRIPT required")
endif()

execute_process(
  COMMAND "${SOAK_SCRIPT}"
          --antinomia "${ANTINOMIA}"
          --inferior "${INFERIOR}"
          --seconds 60
          --seed 1
          --rounds 8
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "soak_fuzz failed (rc=${rc})\n${out}\n${err}")
endif()

string(FIND "${err}" "soak: ok" ok_pos)
if(ok_pos EQUAL -1)
  message(FATAL_ERROR "soak_fuzz missing ok line\n${out}\n${err}")
endif()

message(STATUS "soak_fuzz CI-bounded pass\n${err}")
