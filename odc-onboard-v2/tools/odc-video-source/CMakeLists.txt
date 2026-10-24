cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(odc-video-source VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

if(NOT TARGET spdlog::spdlog)
    find_package(spdlog REQUIRED)
endif()

pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0
    gstreamer-sdp-1.0
    gstreamer-rtsp-1.0
    gstreamer-rtsp-server-1.0
)

add_executable(odc-video-source
    src/main.cpp
    src/VideoSource.cpp
    src/BandwidthThrottler.cpp
)

target_include_directories(odc-video-source PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GST_INCLUDE_DIRS}
)

target_link_libraries(odc-video-source PRIVATE
    ${GST_LIBRARIES}
    spdlog::spdlog
    Threads::Threads
)

target_compile_options(odc-video-source PRIVATE
    ${GST_CFLAGS_OTHER}
    -Wall -Wextra -Wpedantic -Wno-unused-result
)

set_target_properties(odc-video-source PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# Stage launcher script next to the built binary. Sample videos (vid-src/) are
# large media kept out of version control; stage them only when present locally.
add_custom_command(TARGET odc-video-source POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/run-server.sh"
        "${CMAKE_CURRENT_BINARY_DIR}/run-server.sh"
    COMMAND chmod 755 "${CMAKE_CURRENT_BINARY_DIR}/run-server.sh"
    COMMENT "Staging odc-video-source launcher (run-server.sh)"
    VERBATIM
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vid-src")
    add_custom_command(TARGET odc-video-source POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/vid-src"
            "${CMAKE_CURRENT_BINARY_DIR}/vid-src"
        COMMENT "Staging odc-video-source sample media (vid-src/)"
        VERBATIM
    )
else()
    message(STATUS "odc-video-source: vid-src/ not present — skipping sample media staging")
endif()

include(GNUInstallDirs)
install(TARGETS odc-video-source
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/run-server.sh"
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vid-src")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vid-src/"
        DESTINATION ${CMAKE_INSTALL_BINDIR}/vid-src
        USE_SOURCE_PERMISSIONS
    )
endif()
