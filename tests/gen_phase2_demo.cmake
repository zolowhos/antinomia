# gen_phase2_demo.cmake - substitute @INFERIOR@ / @TRACE_OUT@ into the demo script.
# Module: test (CMake).
# Owns: one-shot template expansion for phase2_demo_ready.re.
# Depends: IN, TRACEOUT, TEMPLATE, OUT cache variables from add_custom_command.

if(NOT IN OR NOT TEMPLATE OR NOT OUT)
  message(FATAL_ERROR "IN, TEMPLATE, and OUT required")
endif()
if(NOT TRACEOUT)
  set(TRACEOUT "/tmp/antinomia_phase2_trace.json")
endif()

file(READ "${TEMPLATE}" txt)
string(REPLACE "@INFERIOR@" "${IN}" txt "${txt}")
string(REPLACE "@TRACE_OUT@" "${TRACEOUT}" txt "${txt}")
file(WRITE "${OUT}" "${txt}")
