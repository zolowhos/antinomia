# CMakeLists.txt - in-tree debug inferior fixtures for Phase 1 and edu demos.
# Module: test (CMake).
# Owns: breakpoint_me, loop, and educational ELF/PE dummy executables.
# Depends: re_flags; Threads on UNIX for edu_threads. Binaries must always build.

function(re_add_fixture name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE re_flags ${ARGN})
    if(MSVC)
        set_property(TARGET ${name}
            PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_options(${name} PRIVATE -Wl,--export-dynamic)
    endif()
endfunction()

re_add_fixture(breakpoint_me)
re_add_fixture(loop)
re_add_fixture(edu_hello_spin)
re_add_fixture(edu_crash)

find_package(Threads REQUIRED)
re_add_fixture(edu_threads Threads::Threads)

# Smoke: binaries exist and exit cleanly with a bounded iteration count.
add_test(NAME fixture_breakpoint_me
    COMMAND breakpoint_me 3)
add_test(NAME fixture_loop
    COMMAND loop 4)
add_test(NAME fixture_edu_hello_spin
    COMMAND edu_hello_spin 3)

# edu_crash must fault; accept non-zero / signalled exit from the OS harness.
add_test(NAME fixture_edu_crash
    COMMAND ${CMAKE_COMMAND}
        -DEXE=$<TARGET_FILE:edu_crash>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_edu_crash_smoke.cmake)

# Threads fixture never exits; existence-only check via --help-style no-op is
# unavailable, so we only require the binary to link (covered by ALL build).
# Optional short-lived process probe would hang; skip runtime smoke here.

# Headless launch -> BP -> regs -> step -> detach is registered from tests/
# CMakeLists.txt (Linux live edu_fixture_smoke; Windows when step/regs land).
