cmake_minimum_required(VERSION 3.14)
project(minimal-mavrouter VERSION 3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Find dependencies
find_package(Threads REQUIRED)
find_library(LIB_M m REQUIRED)
find_library(LIB_RT rt REQUIRED)

# Compiler feature checks
include(CheckCXXCompilerFlag)
include(CheckSymbolExists)

check_cxx_compiler_flag("-Waddress-of-packed-member" HAVE_WADDRESS_OF_PACKED_MEMBER)
check_symbol_exists(aio_init "aio.h" HAVE_DECL_AIO_INIT)

# Generate config.h
configure_file(src/config.h.in "${CMAKE_BINARY_DIR}/config.h" @ONLY)

# Generate build_config.h
set(DEFAULT_DEVICE_CONFIG_DIR "/etc/mavlink-router/devices" CACHE STRING "Default device config directory")
configure_file(src/build_config.h.in "${CMAKE_BINARY_DIR}/build_config.h" @ONLY)

# Find pkg-config for libudev
find_package(PkgConfig REQUIRED)

# Generate git_version.h
set(GIT_VERSION_RAW "${PROJECT_VERSION}")
find_package(Git QUIET)
if(GIT_FOUND)
    # Try this directory first, then fall back to original mavlink-router
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty=+ --match "v*"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DESCRIBE
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
    )
    if(NOT (GIT_DESCRIBE_RESULT EQUAL 0 AND GIT_DESCRIBE))
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --dirty=+ --match "v*"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../mavlink-router
            OUTPUT_VARIABLE GIT_DESCRIBE2
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE GIT_DESCRIBE_RESULT2
        )
        if(GIT_DESCRIBE_RESULT2 EQUAL 0 AND GIT_DESCRIBE2)
            set(GIT_DESCRIBE "${GIT_DESCRIBE2}")
            set(GIT_DESCRIBE_RESULT 0)
        endif()
    endif()
    if(GIT_DESCRIBE_RESULT EQUAL 0 AND GIT_DESCRIBE)
        set(GIT_VERSION_RAW "${GIT_DESCRIBE}")
    else()
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../mavlink-router
            OUTPUT_VARIABLE GIT_SHORT_HASH
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE GIT_REV_RESULT
        )
        if(GIT_REV_RESULT EQUAL 0 AND GIT_SHORT_HASH)
            set(GIT_VERSION_RAW "${GIT_SHORT_HASH}")
        endif()
    endif()
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_BINARY_DIR}/git_version.h"
    @ONLY
)

# Warning flags (translated from meson.build)
add_compile_options(
    $<$<COMPILE_LANGUAGE:C>:-Wundef>
    $<$<COMPILE_LANGUAGE:C>:-Wformat=2>
    $<$<COMPILE_LANGUAGE:C>:-Wlogical-op>
    $<$<COMPILE_LANGUAGE:C>:-Wsign-compare>
    $<$<COMPILE_LANGUAGE:C>:-Wformat-security>
    $<$<COMPILE_LANGUAGE:C>:-Wmissing-include-dirs>
    $<$<COMPILE_LANGUAGE:C>:-Wformat-nonliteral>
    $<$<COMPILE_LANGUAGE:C>:-Wold-style-definition>
    $<$<COMPILE_LANGUAGE:C>:-Wpointer-arith>
    $<$<COMPILE_LANGUAGE:C>:-Winit-self>
    $<$<COMPILE_LANGUAGE:C>:-Wfloat-equal>
    $<$<COMPILE_LANGUAGE:C>:-Wredundant-decls>
    $<$<COMPILE_LANGUAGE:C>:-Wmissing-declarations>
    $<$<COMPILE_LANGUAGE:C>:-Wmissing-noreturn>
    $<$<COMPILE_LANGUAGE:C>:-Wshadow>
    $<$<COMPILE_LANGUAGE:C>:-Wendif-labels>
    $<$<COMPILE_LANGUAGE:C>:-Wstrict-aliasing=3>
    $<$<COMPILE_LANGUAGE:C>:-Wwrite-strings>
    $<$<COMPILE_LANGUAGE:CXX>:-Waddress-of-packed-member>
    $<$<COMPILE_LANGUAGE:CXX>:-Wundef>
    $<$<COMPILE_LANGUAGE:CXX>:-Wformat=2>
    $<$<COMPILE_LANGUAGE:CXX>:-Wlogical-op>
    $<$<COMPILE_LANGUAGE:CXX>:-Wsign-compare>
    $<$<COMPILE_LANGUAGE:CXX>:-Wformat-security>
    $<$<COMPILE_LANGUAGE:CXX>:-Wmissing-include-dirs>
    $<$<COMPILE_LANGUAGE:CXX>:-Wformat-nonliteral>
    $<$<COMPILE_LANGUAGE:CXX>:-Wpointer-arith>
    $<$<COMPILE_LANGUAGE:CXX>:-Winit-self>
    $<$<COMPILE_LANGUAGE:CXX>:-Wfloat-equal>
    $<$<COMPILE_LANGUAGE:CXX>:-Wredundant-decls>
    $<$<COMPILE_LANGUAGE:CXX>:-Wmissing-declarations>
    $<$<COMPILE_LANGUAGE:CXX>:-Wmissing-noreturn>
    $<$<COMPILE_LANGUAGE:CXX>:-Wshadow>
    $<$<COMPILE_LANGUAGE:CXX>:-Wendif-labels>
    $<$<COMPILE_LANGUAGE:CXX>:-Wstrict-aliasing=3>
    $<$<COMPILE_LANGUAGE:CXX>:-Wwrite-strings>
)

# Include directories (global)
include_directories(
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/mavlink_c_library_v2/ardupilotmega
)

# Force-include config.h
add_compile_options("-include" "${CMAKE_BINARY_DIR}/config.h")

option(BUILD_HARDWARE_TESTS "Build hardware tests (requires real device)" OFF)
option(BUILD_TESTS "Build test suite" ON)

add_subdirectory(src/common)
add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
