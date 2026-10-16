# run_mcp_tools_list.cmake - verify MCP tools/list advertises every debug verb.
# Module: test (CMake).
# Owns: end-to-end validation of MCP initialize and tools/list.
# Depends: antinomia executable and the MCP fixture. Writes capture to a file
# so Windows CreateProcess argv limits do not truncate the ~40KB tools/list.
if(NOT DEFINED ANTINOMIA OR NOT DEFINED INPUT)
    message(FATAL_ERROR "ANTINOMIA and INPUT are required")
endif()
get_filename_component(_bindir "${ANTINOMIA}" DIRECTORY)
set(CAPTURE "${_bindir}/mcp_tools_list.out")
execute_process(
    COMMAND "${ANTINOMIA}" mcp
    INPUT_FILE "${INPUT}"
    OUTPUT_FILE "${CAPTURE}"
    ERROR_VARIABLE error_output
    RESULT_VARIABLE mcp_rc)
if(NOT mcp_rc STREQUAL "0")
    message(FATAL_ERROR "MCP exited with status ${mcp_rc}: ${error_output}")
endif()
set(PY "${CMAKE_CURRENT_LIST_DIR}/mcp_tools_check.py")
find_program(RE_PYTHON NAMES python3 python REQUIRED)
execute_process(
    COMMAND "${RE_PYTHON}" "${PY}" list "${CAPTURE}"
    RESULT_VARIABLE py_rc
    OUTPUT_VARIABLE py_out
    ERROR_VARIABLE py_err)
if(NOT py_rc STREQUAL "0")
    message(FATAL_ERROR "MCP tools/list check failed: ${py_err}${py_out}")
endif()
