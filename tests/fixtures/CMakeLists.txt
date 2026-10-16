# CMakeLists.txt - in-tree debug inferior fixtures for Phase 1.
# Module: test (CMake).
# Owns: breakpoint_me and loop executables built on Linux and Windows.
# Depends: re_flags. No debugger engine required; binaries must always build.

add_executable(breakpoint_me breakpoint_me.c)
target_link_libraries(breakpoint_me PRIVATE re_flags)
if(MSVC)
    set_property(TARGET breakpoint_me
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(breakpoint_me PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX AND NOT APPLE)
    target_link_options(breakpoint_me PRIVATE -Wl,--export-dynamic)
endif()

add_executable(loop loop.c)
target_link_libraries(loop PRIVATE re_flags)
if(MSVC)
    set_property(TARGET loop
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(loop PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX AND NOT APPLE)
    target_link_options(loop PRIVATE -Wl,--export-dynamic)
endif()

# Smoke: binaries exist and exit cleanly with a bounded iteration count.
add_test(NAME fixture_breakpoint_me
    COMMAND breakpoint_me 3)
add_test(NAME fixture_loop
    COMMAND loop 4)

# Full launch -> BP -> regs -> step -> detach path waits on the live engine.
add_test(NAME DISABLED_debug_launch_bp_step_detach
    COMMAND ${CMAKE_COMMAND} -E echo
        "skipped until debug engine lands")
set_tests_properties(DISABLED_debug_launch_bp_step_detach PROPERTIES DISABLED TRUE)
