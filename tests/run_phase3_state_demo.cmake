# run_phase3_state_demo.cmake - headless Pack C gate: state / mem.page / trace.query.
# Module: test (CMake).
# Owns: invoke configured phase3_state_demo.re and require successful key cmds.
# Depends: ANTINOMIA, SCRIPT from tests/CMakeLists.txt.

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
  message(FATAL_ERROR "phase3_state_demo missing script JSON envelope (rc=${rc})\n${out}\n${err}")
endif()

# Require successful Pack C endpoints (ok:true), not merely that the cmds ran.
string(FIND "${out}" "\"cmd\":\"state\",\"ok\":true" state_pos)
string(FIND "${out}" "\"cmd\":\"mem.page\",\"ok\":true" mem_pos)
string(FIND "${out}" "\"cmd\":\"trace.query\",\"ok\":true" tq_pos)
string(FIND "${out}" "\"cmd\":\"kill\",\"ok\":true" kill_pos)
string(FIND "${out}" "\"failed\":0" fail_pos)
if(state_pos EQUAL -1 OR mem_pos EQUAL -1 OR tq_pos EQUAL -1 OR kill_pos EQUAL -1 OR
   fail_pos EQUAL -1)
  message(FATAL_ERROR
    "phase3_state_demo missing successful state/mem.page/trace.query/kill (rc=${rc})\n${out}\n${err}")
endif()

# Detached state still emits the agent-safe snapshot envelope.
execute_process(
  COMMAND "${ANTINOMIA}" state --limit 4
  RESULT_VARIABLE src
  OUTPUT_VARIABLE sout
  ERROR_VARIABLE serr
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
string(FIND "${sout}" "\"tool\":\"state\"" st2)
string(FIND "${sout}" "\"breakpoints\"" bp2)
string(FIND "${sout}" "\"disasm\"" d2)
string(FIND "${sout}" "\"log\"" l2)
if(NOT src EQUAL 0 OR st2 EQUAL -1 OR bp2 EQUAL -1 OR d2 EQUAL -1 OR l2 EQUAL -1)
  message(FATAL_ERROR "detached state snapshot malformed (rc=${src})\n${sout}\n${serr}")
endif()

message(STATUS "phase3_state_demo progressed through Pack C steps (rc=${rc})")
