# tests/CMakeLists.txt - CTest targets for utils, TUI, fixtures, debug, and smokes.
# Module: test (CMake).
# Owns: utility and TUI suites, debug inferiors, Linux debug fixture, CLI/MCP smokes.
# Depends: re_utils, re_core, antinomia. Style and format gates live in the root.

add_subdirectory(fixtures)
add_executable(re_test_utils test_utils.c)
target_link_libraries(re_test_utils PRIVATE re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_utils
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_utils PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME utils COMMAND re_test_utils)

add_executable(re_test_tui test_tui.c)
target_link_libraries(re_test_tui PRIVATE re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_tui
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_tui PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME tui COMMAND re_test_tui)

add_executable(re_test_screen test_screen.c)
target_link_libraries(re_test_screen PRIVATE re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_screen
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_screen PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME screen COMMAND re_test_screen)

add_executable(re_test_cpu test_cpu.c
    ${CMAKE_SOURCE_DIR}/src/cli/screen/re_cpu.c
    ${CMAKE_SOURCE_DIR}/src/cli/screen/re_cpu_panes.c)
target_include_directories(re_test_cpu PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(re_test_cpu PRIVATE re_core re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_cpu
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_cpu PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME cpu COMMAND re_test_cpu)

add_executable(re_test_console test_console.c)
target_link_libraries(re_test_console PRIVATE re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_console
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_console PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME console COMMAND re_test_console)

add_executable(re_test_expr test_expr.c)
target_link_libraries(re_test_expr PRIVATE re_core re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_expr
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_expr PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME expr COMMAND re_test_expr)

add_executable(re_test_debug test_debug.c)
target_link_libraries(re_test_debug PRIVATE re_core re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_debug
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_debug PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_executable(re_test_debug_p2 test_debug_p2.c)
target_link_libraries(re_test_debug_p2 PRIVATE re_core re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_debug_p2
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_debug_p2 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX AND NOT APPLE)
    add_executable(loop_inferior fixtures/loop_inferior.c)
    target_link_libraries(loop_inferior PRIVATE re_flags dl)
    target_link_options(loop_inferior PRIVATE -Wl,--export-dynamic)
    add_test(NAME debug
        COMMAND re_test_debug $<TARGET_FILE:loop_inferior>)
    set_tests_properties(debug PROPERTIES
        ENVIRONMENT "RE_TEST_INFERIOR=$<TARGET_FILE:loop_inferior>")
    # Headless CLI/MCP session path (no TTY): launch -> BP -> regs -> step -> detach.
    add_test(NAME debug_launch_bp_step_detach
        COMMAND re_test_debug $<TARGET_FILE:loop_inferior>)
    set_tests_properties(debug_launch_bp_step_detach PROPERTIES
        ENVIRONMENT "RE_TEST_INFERIOR=$<TARGET_FILE:loop_inferior>")
    add_test(NAME debug_p2
        COMMAND re_test_debug_p2 $<TARGET_FILE:loop_inferior>)
    set_tests_properties(debug_p2 PROPERTIES
        ENVIRONMENT "RE_TEST_INFERIOR=$<TARGET_FILE:loop_inferior>")
else()
    add_test(NAME debug COMMAND re_test_debug)
    add_test(NAME debug_p2 COMMAND re_test_debug_p2)
    # Windows: enable when backend step + get_regs are live (Pack E).
    add_test(NAME DISABLED_debug_launch_bp_step_detach
        COMMAND ${CMAKE_COMMAND} -E echo
            "skipped until Windows step/regs land; see docs/yama-ptrace.md")
    set_tests_properties(DISABLED_debug_launch_bp_step_detach PROPERTIES DISABLED TRUE)
endif()

add_test(NAME smoke_version
    COMMAND ${CMAKE_COMMAND}
        -DANTINOMIA=$<TARGET_FILE:antinomia>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_smoke_version.cmake)

add_test(NAME smoke_status
    COMMAND ${CMAKE_COMMAND}
        -DANTINOMIA=$<TARGET_FILE:antinomia>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_smoke_status.cmake)

add_test(NAME smoke_mcp_tools
    COMMAND ${CMAKE_COMMAND}
        -DANTINOMIA=$<TARGET_FILE:antinomia>
        -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/mcp_tools_list.jsonl
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_mcp_tools_list.cmake)
