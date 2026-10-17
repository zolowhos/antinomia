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

add_executable(re_test_cfg test_cfg.c
    ${CMAKE_SOURCE_DIR}/src/cli/screen/keys/re_keys.c
    ${CMAKE_SOURCE_DIR}/src/cli/screen/re_input.c)
target_include_directories(re_test_cfg PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(re_test_cfg PRIVATE re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_cfg
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_cfg PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME cfg COMMAND re_test_cfg)

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
    ${CMAKE_SOURCE_DIR}/src/cli/screen/re_cpu_panes.c
    ${CMAKE_SOURCE_DIR}/src/cli/screen/re_cpu_paint.c)
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

add_executable(re_test_ai test_ai.c)
target_link_libraries(re_test_ai PRIVATE re_core re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_ai
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_ai PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME ai COMMAND re_test_ai)

add_executable(re_test_script test_script.c)
target_link_libraries(re_test_script PRIVATE re_core re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_script
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_script PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME script COMMAND re_test_script)

add_executable(re_test_disasm_p2 test_disasm_p2.c)
target_link_libraries(re_test_disasm_p2 PRIVATE re_core re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_disasm_p2
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_disasm_p2 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME disasm_p2 COMMAND re_test_disasm_p2)

add_executable(re_test_perf_p4 test_perf_p4.c
    ${CMAKE_SOURCE_DIR}/src/cli/screen/re_cpu.c
    ${CMAKE_SOURCE_DIR}/src/cli/screen/re_cpu_panes.c
    ${CMAKE_SOURCE_DIR}/src/cli/screen/re_cpu_paint.c)
target_include_directories(re_test_perf_p4 PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(re_test_perf_p4 PRIVATE re_core re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_perf_p4
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_perf_p4 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME perf_p4 COMMAND re_test_perf_p4)

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
add_executable(re_test_sym_p2 test_sym_p2.c)
target_link_libraries(re_test_sym_p2 PRIVATE re_core re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_sym_p2
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_sym_p2 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_executable(re_test_trace_p2 test_trace_p2.c)
target_link_libraries(re_test_trace_p2 PRIVATE re_core re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_trace_p2
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_trace_p2 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_executable(re_test_patch_p2 test_patch_p2.c)
target_link_libraries(re_test_patch_p2 PRIVATE re_core re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_patch_p2
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_patch_p2 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME patch_p2 COMMAND re_test_patch_p2)
add_executable(re_test_os_p2 test_os_p2.c)
target_link_libraries(re_test_os_p2 PRIVATE re_core re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_os_p2
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_os_p2 PRIVATE _CRT_SECURE_NO_WARNINGS)
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
    add_executable(dwarf_src fixtures/dwarf_src.c)
    target_link_libraries(dwarf_src PRIVATE re_flags)
    # DWARF 4 line tables (v1 parser); DWARF 5 directory forms are deferred.
    target_compile_options(dwarf_src PRIVATE -gdwarf-4 -g -O0)
    add_test(NAME sym_p2
        COMMAND re_test_sym_p2 $<TARGET_FILE:dwarf_src>)
    set_tests_properties(sym_p2 PROPERTIES
        ENVIRONMENT "RE_TEST_DWARF=$<TARGET_FILE:dwarf_src>")
    add_test(NAME trace_p2
        COMMAND re_test_trace_p2 $<TARGET_FILE:loop_inferior>)
    set_tests_properties(trace_p2 PROPERTIES
        ENVIRONMENT "RE_TEST_INFERIOR=$<TARGET_FILE:loop_inferior>")
    add_test(NAME os_p2
        COMMAND re_test_os_p2 $<TARGET_FILE:loop_inferior>)
    set_tests_properties(os_p2 PROPERTIES
        ENVIRONMENT "RE_TEST_INFERIOR=$<TARGET_FILE:loop_inferior>")
    # Phase 2 competitive gate demo script (CLI) on Linux ELF fixture.
    set(RE_PHASE2_TRACE_OUT "${CMAKE_CURRENT_BINARY_DIR}/phase2_demo_trace.json")
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/phase2_demo_ready.re
        COMMAND ${CMAKE_COMMAND}
            -DIN=$<TARGET_FILE:loop_inferior>
            -DTRACEOUT=${RE_PHASE2_TRACE_OUT}
            -DTEMPLATE=${CMAKE_SOURCE_DIR}/scripts/phase2_demo.re.in
            -DOUT=${CMAKE_CURRENT_BINARY_DIR}/phase2_demo_ready.re
            -P ${CMAKE_CURRENT_SOURCE_DIR}/gen_phase2_demo.cmake
        DEPENDS loop_inferior ${CMAKE_SOURCE_DIR}/scripts/phase2_demo.re.in
            ${CMAKE_CURRENT_SOURCE_DIR}/gen_phase2_demo.cmake
        COMMENT "Generate Phase 2 demo script with fixture path"
        VERBATIM)
    add_custom_target(phase2_demo_script ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/phase2_demo_ready.re)
    add_test(NAME phase2_demo
        COMMAND ${CMAKE_COMMAND}
            -DANTINOMIA=$<TARGET_FILE:antinomia>
            -DSCRIPT=${CMAKE_CURRENT_BINARY_DIR}/phase2_demo_ready.re
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_phase2_demo.cmake)
    set_tests_properties(phase2_demo PROPERTIES FIXTURES_REQUIRED "")
    # Phase 3 Pack C: state snapshot + mem.page + trace.query cursors.
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/phase3_state_demo_ready.re
        COMMAND ${CMAKE_COMMAND}
            -DIN=$<TARGET_FILE:loop_inferior>
            -DTEMPLATE=${CMAKE_SOURCE_DIR}/scripts/phase3_state_demo.re.in
            -DOUT=${CMAKE_CURRENT_BINARY_DIR}/phase3_state_demo_ready.re
            -P ${CMAKE_CURRENT_SOURCE_DIR}/gen_phase3_state_demo.cmake
        DEPENDS loop_inferior ${CMAKE_SOURCE_DIR}/scripts/phase3_state_demo.re.in
            ${CMAKE_CURRENT_SOURCE_DIR}/gen_phase3_state_demo.cmake
        COMMENT "Generate Phase 3 Pack C demo script with fixture path"
        VERBATIM)
    add_custom_target(phase3_state_demo_script ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/phase3_state_demo_ready.re)
    add_test(NAME phase3_state_demo
        COMMAND ${CMAKE_COMMAND}
            -DANTINOMIA=$<TARGET_FILE:antinomia>
            -DSCRIPT=${CMAKE_CURRENT_BINARY_DIR}/phase3_state_demo_ready.re
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_phase3_state_demo.cmake)
    # Phase 3 Pack D: triage-live + headless JSON exit.
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/phase3_headless_ci_ready.re
        COMMAND ${CMAKE_COMMAND}
            -DIN=$<TARGET_FILE:loop_inferior>
            -DTEMPLATE=${CMAKE_SOURCE_DIR}/scripts/phase3_headless_ci.re.in
            -DOUT=${CMAKE_CURRENT_BINARY_DIR}/phase3_headless_ci_ready.re
            -P ${CMAKE_CURRENT_SOURCE_DIR}/gen_phase3_headless_ci.cmake
        DEPENDS loop_inferior ${CMAKE_SOURCE_DIR}/scripts/phase3_headless_ci.re.in
            ${CMAKE_CURRENT_SOURCE_DIR}/gen_phase3_headless_ci.cmake
        COMMENT "Generate Phase 3 Pack D headless CI script with fixture path"
        VERBATIM)
    add_custom_target(phase3_headless_ci_script ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/phase3_headless_ci_ready.re)
    add_test(NAME phase3_headless_ci
        COMMAND ${CMAKE_COMMAND}
            -DANTINOMIA=$<TARGET_FILE:antinomia>
            -DSCRIPT=${CMAKE_CURRENT_BINARY_DIR}/phase3_headless_ci_ready.re
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_phase3_headless_ci.cmake)
    # Phase 3 Pack E: explain / suggest_bp / summarize_ex.
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/phase3_ai_demo_ready.re
        COMMAND ${CMAKE_COMMAND}
            -DIN=$<TARGET_FILE:loop_inferior>
            -DTEMPLATE=${CMAKE_SOURCE_DIR}/scripts/phase3_ai_demo.re.in
            -DOUT=${CMAKE_CURRENT_BINARY_DIR}/phase3_ai_demo_ready.re
            -P ${CMAKE_CURRENT_SOURCE_DIR}/gen_phase3_ai_demo.cmake
        DEPENDS loop_inferior ${CMAKE_SOURCE_DIR}/scripts/phase3_ai_demo.re.in
            ${CMAKE_CURRENT_SOURCE_DIR}/gen_phase3_ai_demo.cmake
        COMMENT "Generate Phase 3 Pack E AI demo script with fixture path"
        VERBATIM)
    add_custom_target(phase3_ai_demo_script ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/phase3_ai_demo_ready.re)
    add_test(NAME phase3_ai_demo
        COMMAND ${CMAKE_COMMAND}
            -DANTINOMIA=$<TARGET_FILE:antinomia>
            -DSCRIPT=${CMAKE_CURRENT_BINARY_DIR}/phase3_ai_demo_ready.re
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_phase3_ai_demo.cmake)
    # Phase 3 gate: pure MCP stdio agent path (no TUI).
    add_test(NAME phase3_gate_mcp
        COMMAND ${CMAKE_COMMAND}
            -DANTINOMIA=$<TARGET_FILE:antinomia>
            -DINFERIOR=$<TARGET_FILE:loop_inferior>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_phase3_gate_mcp.cmake)
    # Phase 3 gate: side-listen owner + mcp --attach (CI stand-in for TUI).
    add_test(NAME phase3_gate_side
        COMMAND ${CMAKE_COMMAND}
            -DANTINOMIA=$<TARGET_FILE:antinomia>
            -DINFERIOR=$<TARGET_FILE:loop_inferior>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_phase3_gate_side.cmake)
else()
    add_test(NAME debug COMMAND re_test_debug)
    add_test(NAME debug_p2 COMMAND re_test_debug_p2)
    add_test(NAME sym_p2 COMMAND re_test_sym_p2)
    add_test(NAME trace_p2 COMMAND re_test_trace_p2)
    add_test(NAME os_p2 COMMAND re_test_os_p2)
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

add_test(NAME smoke_mcp_call
    COMMAND ${CMAKE_COMMAND}
        -DANTINOMIA=$<TARGET_FILE:antinomia>
        -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/mcp_tools_call.jsonl
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_mcp_tools_call.cmake)

add_test(NAME smoke_ida_shims
    COMMAND ${CMAKE_COMMAND}
        -DANTINOMIA=$<TARGET_FILE:antinomia>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_smoke_ida_shims.cmake)

add_executable(re_test_dbg_lock test_dbg_lock.c)
target_link_libraries(re_test_dbg_lock PRIVATE re_core re_utils re_flags)
if(MSVC)
    set_property(TARGET re_test_dbg_lock
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_test_dbg_lock PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME dbg_lock COMMAND re_test_dbg_lock)

add_test(NAME smoke_mcp_side_attach
    COMMAND ${CMAKE_COMMAND}
        -DANTINOMIA=$<TARGET_FILE:antinomia>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_mcp_side_attach.cmake)
