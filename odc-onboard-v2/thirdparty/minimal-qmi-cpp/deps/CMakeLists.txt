cmake_minimum_required(VERSION 3.16)
project(ultima-qmi-deps C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF)

# When used as a subdirectory, CMAKE_SOURCE_DIR points to the parent.
# Use DEPS_ROOT to always refer to this directory.
set(DEPS_ROOT ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "")
set(DEPS_BUILD ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)

# Include our cmake modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
# Find system dependencies
include(FindSystemDeps)

# Build in order
add_subdirectory(glib)
add_subdirectory(libmbim)
add_subdirectory(libqmi)
