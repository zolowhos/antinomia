# gen_phase3_state_demo.cmake - substitute fixture path into Pack C demo script.
# Module: test (CMake).
# Owns: copy phase3_state_demo.re.in with @INFERIOR@ replaced.
# Depends: IN, TEMPLATE, OUT cache variables from tests/CMakeLists.txt.

if(NOT IN OR NOT TEMPLATE OR NOT OUT)
  message(FATAL_ERROR "IN, TEMPLATE, OUT required")
endif()

file(READ "${TEMPLATE}" text)
string(REPLACE "@INFERIOR@" "${IN}" text "${text}")
file(WRITE "${OUT}" "${text}")
