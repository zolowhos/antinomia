# run_phase3_gate_mcp.cmake - Phase 3 gate: MCP stdio agent path (no TUI).
# Module: test (CMake).
# Owns: invoke phase3_gate_mcp.py against loop_inferior.
# Depends: ANTINOMIA, INFERIOR from tests/CMakeLists.txt.
if(NOT DEFINED ANTINOMIA OR NOT DEFINED INFERIOR)
    message(FATAL_ERROR "ANTINOMIA and INFERIOR are required")
endif()
if(WIN32)
    message(STATUS "phase3_gate_mcp skipped on Windows (Linux ELF fixture)")
    return()
endif()
find_program(RE_PYTHON NAMES python3 python REQUIRED)
set(PY "${CMAKE_CURRENT_LIST_DIR}/phase3_gate_mcp.py")
get_filename_component(_bindir "${ANTINOMIA}" DIRECTORY)
set(CAPTURE "${_bindir}/phase3_gate_mcp.out")
execute_process(
    COMMAND "${RE_PYTHON}" "${PY}" "${ANTINOMIA}" "${INFERIOR}" "${CAPTURE}"
    RESULT_VARIABLE py_rc
    OUTPUT_VARIABLE py_out
    ERROR_VARIABLE py_err)
if(NOT py_rc STREQUAL "0")
    message(FATAL_ERROR "phase3_gate_mcp failed: ${py_err}${py_out}")
endif()
message(STATUS "phase3_gate_mcp: ${py_out}")
