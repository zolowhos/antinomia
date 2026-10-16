# gen_phase3_ai_demo.cmake - substitute fixture path into Pack E demo script.
# Module: test (CMake).
# Owns: write phase3_ai_demo_ready.re from template + inferior path.
# Depends: configure-time IN/TEMPLATE/OUT cache vars from tests/CMakeLists.txt.
if(NOT IN OR NOT TEMPLATE OR NOT OUT)
  message(FATAL_ERROR "gen_phase3_ai_demo: need -DIN= -DTEMPLATE= -DOUT=")
endif()
file(READ "${TEMPLATE}" _txt)
string(REPLACE "@INFERIOR@" "${IN}" _txt "${_txt}")
file(WRITE "${OUT}" "${_txt}")
