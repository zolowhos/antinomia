# run_phase3_ai_demo.cmake - run Pack E AI helper demo script under antinomia.
# Module: test (CMake).
# Owns: invoke antinomia script and require successful Pack E cmds.
# Depends: ANTINOMIA and SCRIPT cache vars from add_test.
if(NOT ANTINOMIA OR NOT SCRIPT)
  message(FATAL_ERROR "run_phase3_ai_demo: need -DANTINOMIA= -DSCRIPT=")
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
  message(FATAL_ERROR "phase3_ai_demo missing script JSON envelope (rc=${rc})\n${out}\n${err}")
endif()
string(FIND "${out}" "\"cmd\":\"explain\",\"ok\":true" e_pos)
string(FIND "${out}" "\"cmd\":\"summarize_ex\",\"ok\":true" s_pos)
string(FIND "${out}" "\"cmd\":\"suggest_bp\",\"ok\":true" b_pos)
string(FIND "${out}" "\"cmd\":\"kill\",\"ok\":true" k_pos)
string(FIND "${out}" "\"failed\":0" f_pos)
if(e_pos EQUAL -1 OR s_pos EQUAL -1 OR b_pos EQUAL -1 OR k_pos EQUAL -1 OR f_pos EQUAL -1)
  message(FATAL_ERROR
    "phase3_ai_demo missing successful explain/summarize_ex/suggest_bp/kill (rc=${rc})\n${out}\n${err}")
endif()
# Detached summarize_ex by code (no live session required).
execute_process(
  COMMAND "${ANTINOMIA}" summarize_ex 11
  RESULT_VARIABLE src
  OUTPUT_VARIABLE sout
  ERROR_VARIABLE serr
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
string(FIND "${sout}" "\"tool\":\"summarize_ex\"" t2)
string(FIND "${sout}" "SIGSEGV" n2)
if(NOT src EQUAL 0 OR t2 EQUAL -1 OR n2 EQUAL -1)
  message(FATAL_ERROR "detached summarize_ex malformed (rc=${src})\n${sout}\n${serr}")
endif()
message(STATUS "phase3_ai_demo progressed through Pack E steps (rc=${rc})")
