cmake_minimum_required(VERSION 3.16)
project(HexaRtspCompressor VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static linking configuration
option(BUILD_FULLY_STATIC "Build fully static binary (requires static GStreamer libs)" OFF)

if(BUILD_FULLY_STATIC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    message(STATUS "Building fully static binary")
else()
    message(STATUS "Building with shared libstdc++/libgcc")
endif()

set(BUILD_SHARED_LIBS OFF)

# Thread orchestrator (header-only)
add_subdirectory(thirdparty/thread-orchestrator)

# CLI11 (header-only CLI parser)
add_subdirectory(thirdparty/CLI11)

# Find GStreamer via pkg-config
find_package(PkgConfig REQUIRED)

pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0
    gstreamer-pbutils-1.0
    gstreamer-sdp-1.0
    gstreamer-rtp-1.0
    gstreamer-rtsp-1.0
    gstreamer-rtsp-server-1.0
)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/RtspAnalyzer.cpp
    src/RtspBridge.cpp
    src/HardwareInfo.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GST_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${GST_LDFLAGS}
    thread_orchestrator
    CLI11
    pthread
    gcc_s
)

target_compile_options(${PROJECT_NAME} PRIVATE
    ${GST_CFLAGS_OTHER}
    -Wall -Wextra -Wpedantic
)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
