# run_edu_crash_smoke.cmake - edu_crash must terminate abnormally (fault).
# Module: test (CMake).
# Owns: run edu_crash and require a non-zero or crash-style exit.
# Depends: EXE path to the edu_crash binary.
if(NOT DEFINED EXE)
    message(FATAL_ERROR "EXE is required")
endif()
execute_process(
    COMMAND "${EXE}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(out MATCHES "edu_crash: educational intentional fault")
    # Fault after the banner is the success path; any non-zero/crash rc is OK.
    if(rc STREQUAL "0")
        message(FATAL_ERROR "edu_crash exited 0 (expected fault): ${out}\n${err}")
    endif()
    message(STATUS "fixture_edu_crash ok (rc=${rc})")
else()
    message(FATAL_ERROR "edu_crash missing educational banner: ${out}\n${err}")
endif()
