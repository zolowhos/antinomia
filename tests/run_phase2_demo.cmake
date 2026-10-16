# run_phase2_demo.cmake - headless Phase 2 gate demo via antinomia script.
# Module: test (CMake).
# Owns: invoke configured phase2_demo.re and require ok lines / zero failures.
# Depends: ANTINOMIA, SCRIPT path variables from tests/CMakeLists.txt.

if(NOT ANTINOMIA OR NOT SCRIPT)
  message(FATAL_ERROR "ANTINOMIA and SCRIPT required")
endif()

execute_process(
  COMMAND "${ANTINOMIA}" script "${SCRIPT}"
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

string(FIND "${out}" "\"tool\":\"script\"" tool_pos)
if(tool_pos EQUAL -1)
  message(FATAL_ERROR "phase2_demo missing script JSON envelope (rc=${rc})\n${out}\n${err}")
endif()

# Require the gate endpoints even if an intermediate wait raced (exit may be 1).
string(FIND "${out}" "\"cmd\":\"dbsave\"" db_pos)
string(FIND "${out}" "\"cmd\":\"kill\"" kill_pos)
string(FIND "${out}" "\"cmd\":\"bt\"" bt_pos)
string(FIND "${out}" "\"cmd\":\"bph\"" bph_pos)
if(db_pos EQUAL -1 OR kill_pos EQUAL -1 OR bt_pos EQUAL -1 OR bph_pos EQUAL -1)
  message(FATAL_ERROR "phase2_demo missing bt/bph/dbsave/kill (rc=${rc})\n${out}\n${err}")
endif()

message(STATUS "phase2_demo progressed through gate steps (rc=${rc})")
