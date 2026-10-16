# CMakeLists.txt - build for the antinomia debugger and MCP server.
# Module: build (CMake).
# Owns: targets, the utility static lib, the exe, tests, and the format gate.
# Depends: CMake 3.25 or newer, a C11 and C++17 compiler. Rules in .rules/rules.md.
cmake_minimum_required(VERSION 3.25)

project(antinomia
    VERSION 0.1.0
    DESCRIPTION "AI driven debugger for reverse engineering applications, libraries, or whatever you want"
    LANGUAGES C CXX)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Oldest policy version allowed")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RE_ENABLE_DISASM "Fetch Capstone for later disasm panes (unused in Phase 0)" OFF)
option(RE_ENABLE_CAPSTONE "Fetch Capstone (requires RE_ENABLE_DISASM)" OFF)
option(RE_ENABLE_DEBUG_WIN "Compile Windows debug backend stub" ON)
option(RE_ENABLE_DEBUG_LINUX "Compile Linux debug backend stub" ON)
option(RE_WERROR "Treat warnings as errors" ON)
if(NOT RE_ENABLE_DISASM)
    set(RE_ENABLE_CAPSTONE OFF CACHE BOOL "Capstone requires RE_ENABLE_DISASM" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
    find_program(RE_LLVM_AR NAMES llvm-ar llvm-ar-20 llvm-ar-19 llvm-ar-18 llvm-ar-17)
    find_program(RE_LLVM_RANLIB NAMES llvm-ranlib llvm-ranlib-20 llvm-ranlib-19
        llvm-ranlib-18 llvm-ranlib-17)
    if(RE_LLVM_AR)
        set(CMAKE_AR "${RE_LLVM_AR}" CACHE FILEPATH "" FORCE)
        set(CMAKE_C_COMPILER_AR "${RE_LLVM_AR}" CACHE FILEPATH "" FORCE)
        set(CMAKE_CXX_COMPILER_AR "${RE_LLVM_AR}" CACHE FILEPATH "" FORCE)
    endif()
    if(RE_LLVM_RANLIB)
        set(CMAKE_RANLIB "${RE_LLVM_RANLIB}" CACHE FILEPATH "" FORCE)
        set(CMAKE_C_COMPILER_RANLIB "${RE_LLVM_RANLIB}" CACHE FILEPATH "" FORCE)
        set(CMAKE_CXX_COMPILER_RANLIB "${RE_LLVM_RANLIB}" CACHE FILEPATH "" FORCE)
    endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(re_flags INTERFACE)
target_compile_features(re_flags INTERFACE c_std_11)
if(MSVC)
    target_compile_options(re_flags INTERFACE
        /W4
        /permissive- /Zc:__cplusplus
        $<$<BOOL:${RE_WERROR}>:/WX>
        /utf-8)
else()
    target_compile_options(re_flags INTERFACE
        -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual
        $<$<BOOL:${RE_WERROR}>:-Werror>)
endif()

file(GLOB_RECURSE RE_UTIL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/utils/*.c)
file(GLOB_RECURSE RE_UTIL_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/utils/*.h)

add_library(re_utils STATIC ${RE_UTIL_SOURCES} ${RE_UTIL_HEADERS})
target_include_directories(re_utils PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(re_utils PRIVATE re_flags)

if(MSVC)
    set_property(TARGET re_utils PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_utils PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(re_utils PUBLIC m)
endif()

file(GLOB_RECURSE RE_FEATURE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/features/*.c)
file(GLOB_RECURSE RE_FEATURE_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/features/*.h)

add_library(re_core STATIC ${RE_FEATURE_SOURCES} ${RE_FEATURE_HEADERS})
target_include_directories(re_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(re_core PUBLIC re_utils PRIVATE re_flags)
if(MSVC)
    set_property(TARGET re_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(re_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(RE_ENABLE_DISASM)
    target_compile_definitions(re_core PUBLIC RE_ENABLE_DISASM=1)
endif()
if(WIN32 AND RE_ENABLE_DEBUG_WIN)
    target_compile_definitions(re_core PUBLIC RE_ENABLE_DEBUG_WIN=1)
endif()
if(UNIX AND RE_ENABLE_DEBUG_LINUX)
    target_compile_definitions(re_core PUBLIC RE_ENABLE_DEBUG_LINUX=1)
endif()

add_executable(antinomia
    src/cli/app/main.cpp
    src/cli/app/re_table.c
    src/cli/app/re_shell.c
    src/cli/cmds/re_cmds.c
    src/cli/cmds/re_cmds_bp.c
    src/cli/cmds/re_cmds_inspect.c
    src/cli/cmds/re_cmds_mod.c
    src/cli/cmds/re_cmds_thr.c
    src/cli/cmds/re_cmds_expr.c
    src/cli/screen/re_input.c
    src/cli/screen/re_focus.c
    src/cli/screen/re_draw.c
    src/cli/screen/re_term.c
    src/cli/screen/re_gui.c
    src/cli/screen/re_cpu.c
    src/cli/screen/re_cpu_panes.c
    src/mcp/re_mcp.cpp
    src/mcp/re_mcp_tools.cpp)
target_link_libraries(antinomia PRIVATE re_core re_flags)
find_package(Threads REQUIRED)
target_link_libraries(antinomia PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(antinomia PRIVATE comdlg32 ws2_32)
endif()
if(MSVC)
    set_property(TARGET antinomia PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(antinomia PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

find_program(RE_CLANG_FORMAT
    NAMES clang-format
    HINTS "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin")
find_program(RE_PYTHON NAMES python3 python)

file(GLOB_RECURSE RE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/tests/fixtures/*.c)

if(RE_CLANG_FORMAT)
    add_custom_target(re_format
        COMMAND ${RE_CLANG_FORMAT} -i --style=file ${RE_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Formatting all sources with clang-format")
    add_test(NAME format
        COMMAND ${RE_CLANG_FORMAT} -n --Werror --style=file ${RE_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
else()
    message(WARNING "clang-format not found, the format gate is disabled")
endif()

if(RE_PYTHON)
    add_custom_target(re_check
        COMMAND ${RE_PYTHON} ${CMAKE_SOURCE_DIR}/scripts/check.py ${CMAKE_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running the project rule gate")
    add_test(NAME style
        COMMAND ${RE_PYTHON} ${CMAKE_SOURCE_DIR}/scripts/check.py ${CMAKE_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
else()
    message(WARNING "python not found, the style gate is disabled")
endif()

enable_testing()
add_subdirectory(tests)
