# run_mcp_tools_call.cmake - verify MCP tools/call antinomia/1 structured errors.
# Module: test (CMake).
# Owns: status success and usage/notfound error JSON over MCP stdio.
# Depends: antinomia executable and the MCP call fixture. Uses Python JSON.
if(NOT DEFINED ANTINOMIA OR NOT DEFINED INPUT)
    message(FATAL_ERROR "ANTINOMIA and INPUT are required")
endif()
execute_process(
    COMMAND "${ANTINOMIA}" mcp
    INPUT_FILE "${INPUT}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output
    RESULT_VARIABLE mcp_rc)
if(NOT mcp_rc STREQUAL "0")
    message(FATAL_ERROR "MCP exited with status ${mcp_rc}: ${error_output}")
endif()
set(PY "${CMAKE_CURRENT_LIST_DIR}/mcp_tools_check.py")
find_program(RE_PYTHON NAMES python3 python REQUIRED)
execute_process(
    COMMAND "${RE_PYTHON}" "${PY}" call "${output}"
    RESULT_VARIABLE py_rc
    OUTPUT_VARIABLE py_out
    ERROR_VARIABLE py_err)
if(NOT py_rc STREQUAL "0")
    message(FATAL_ERROR "MCP tools/call check failed: ${py_err}${py_out}")
endif()
