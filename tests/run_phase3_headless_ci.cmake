# run_phase3_headless_ci.cmake - Pack D gate: headless script JSON exit + triage.
# Module: test (CMake).
# Owns: invoke configured phase3_headless_ci.re via headless; require exit:0.
# Depends: ANTINOMIA, SCRIPT from tests/CMakeLists.txt.

if(NOT ANTINOMIA OR NOT SCRIPT)
  message(FATAL_ERROR "ANTINOMIA and SCRIPT required")
endif()

execute_process(
  COMMAND "${ANTINOMIA}" headless "${SCRIPT}"
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

string(FIND "${out}" "\"tool\":\"headless\"" tool_pos)
string(FIND "${out}" "\"exit\":0" exit_pos)
string(FIND "${out}" "\"cmd\":\"triage-live\",\"ok\":true" triage_pos)
string(FIND "${out}" "\"cmd\":\"kill\",\"ok\":true" kill_pos)
string(FIND "${out}" "\"failed\":0" fail_pos)
if(tool_pos EQUAL -1 OR exit_pos EQUAL -1 OR triage_pos EQUAL -1 OR kill_pos EQUAL -1 OR
   fail_pos EQUAL -1 OR NOT rc EQUAL 0)
  message(FATAL_ERROR
    "phase3_headless_ci missing headless exit:0 / triage-live / kill (rc=${rc})\n${out}\n${err}")
endif()

# Detached triage must fail cleanly with schema antinomia/1 (no live target).
execute_process(
  COMMAND "${ANTINOMIA}" triage-live
  RESULT_VARIABLE trc
  OUTPUT_VARIABLE tout
  ERROR_VARIABLE terr
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
string(FIND "${tout}" "\"schema\":\"antinomia/1\"" sch)
string(FIND "${tout}" "\"tool\":\"triage-live\"" tl)
string(FIND "${tout}" "\"ok\":false" bad)
if(sch EQUAL -1 OR tl EQUAL -1 OR bad EQUAL -1 OR trc EQUAL 0)
  message(FATAL_ERROR "detached triage-live should fail with schema (rc=${trc})\n${tout}\n${terr}")
endif()

# Honest decompile stub never invents pseudocode.
execute_process(
  COMMAND "${ANTINOMIA}" decompile
  RESULT_VARIABLE drc
  OUTPUT_VARIABLE dout
  ERROR_VARIABLE derr
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
string(FIND "${dout}" "\"implemented\":false" stub)
string(FIND "${dout}" "\"shim\":\"ida-mcp\"" shim)
if(stub EQUAL -1 OR shim EQUAL -1 OR drc EQUAL 0)
  message(FATAL_ERROR "decompile stub malformed (rc=${drc})\n${dout}\n${derr}")
endif()

message(STATUS "phase3_headless_ci Pack D gate passed (rc=${rc})")
