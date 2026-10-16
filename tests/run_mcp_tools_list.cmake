# run_mcp_tools_list.cmake - verify MCP tools/list advertises debugger stubs.
# Module: test (CMake).
# Owns: end-to-end validation of MCP initialize and tools/list.
# Depends: antinomia executable and the MCP fixture.
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
string(REPLACE "\r\n" "\n" output "${output}")
string(REGEX MATCHALL "[^\n]+" frames "${output}")
list(LENGTH frames frame_count)
if(frame_count LESS 2)
    message(FATAL_ERROR "expected initialize and tools/list responses, got ${frame_count}")
endif()
list(GET frames 1 tools_frame)
string(JSON tool_count ERROR_VARIABLE json_error LENGTH "${tools_frame}" result tools)
if(NOT json_error STREQUAL "NOTFOUND")
    message(FATAL_ERROR "tools/list response is not valid JSON: ${json_error}")
endif()
set(required_tools status init launch attach detach kill wait_event run g cont step regs
    set_regs d u mem_read mem_write threads modules)
foreach(want IN LISTS required_tools)
    set(found 0)
    math(EXPR last_index "${tool_count} - 1")
    foreach(index RANGE 0 ${last_index})
        string(JSON tool_name ERROR_VARIABLE json_error GET
            "${tools_frame}" result tools ${index} name)
        if("x${tool_name}" STREQUAL "x${want}")
            set(found 1)
            break()
        endif()
    endforeach()
    if(NOT found)
        message(FATAL_ERROR "tools/list missing ${want}")
    endif()
endforeach()
