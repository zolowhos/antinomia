# run_smoke_ida_shims.cmake - dual-OS smoke for honest ida-mcp stubs + triage usage.
# Module: test (CMake).
# Owns: decompile/get_xrefs stubs and detached triage-live error envelope.
# Depends: ANTINOMIA from tests/CMakeLists.txt.

if(NOT ANTINOMIA)
  message(FATAL_ERROR "ANTINOMIA required")
endif()

execute_process(
  COMMAND "${ANTINOMIA}" decompile
  RESULT_VARIABLE drc
  OUTPUT_VARIABLE dout
  ERROR_VARIABLE derr
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
string(FIND "${dout}" "\"schema\":\"antinomia/1\"" sch)
string(FIND "${dout}" "\"implemented\":false" stub)
string(FIND "${dout}" "\"shim\":\"ida-mcp\"" shim)
if(sch EQUAL -1 OR stub EQUAL -1 OR shim EQUAL -1 OR drc EQUAL 0)
  message(FATAL_ERROR "decompile stub failed (rc=${drc})\n${dout}\n${derr}")
endif()

execute_process(
  COMMAND "${ANTINOMIA}" get_xrefs_to
  RESULT_VARIABLE xrc
  OUTPUT_VARIABLE xout
  ERROR_VARIABLE xerr
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
string(FIND "${xout}" "\"implemented\":false" xstub)
if(xstub EQUAL -1 OR xrc EQUAL 0)
  message(FATAL_ERROR "get_xrefs_to stub failed (rc=${xrc})\n${xout}\n${xerr}")
endif()

execute_process(
  COMMAND "${ANTINOMIA}" triage-live
  RESULT_VARIABLE trc
  OUTPUT_VARIABLE tout
  ERROR_VARIABLE terr
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
string(FIND "${tout}" "\"tool\":\"triage-live\"" tl)
string(FIND "${tout}" "\"ok\":false" bad)
if(tl EQUAL -1 OR bad EQUAL -1 OR trc EQUAL 0)
  message(FATAL_ERROR "detached triage-live should fail (rc=${trc})\n${tout}\n${terr}")
endif()

message(STATUS "smoke_ida_shims ok")
