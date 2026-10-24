cmake_minimum_required(VERSION 3.18)
project(odc-onboard VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# ── System packages ───────────────────────────────────────────────────────────
find_package(spdlog REQUIRED)
find_package(CLI11 REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(fmt REQUIRED)

# ── Thread orchestrator (local header-only) ───────────────────────────────────
add_library(thread_orchestrator INTERFACE)
target_include_directories(thread_orchestrator INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)
target_compile_features(thread_orchestrator INTERFACE cxx_std_20)

# ── WireGuard netlink (header-only wrapper around kernel netlink) ──────────────

# ── OpenVPN3 dependencies (CPM cached) ────────────────────────────────────────
set(CPM_SOURCE_CACHE $ENV{HOME}/.cache/CPM)
set(CPM_DOWNLOAD_VERSION 0.40.2)
set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
    file(DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
        "${CPM_DOWNLOAD_LOCATION}"
    )
endif()
include("${CPM_DOWNLOAD_LOCATION}")

CPMAddPackage(
    NAME asio
    GITHUB_REPOSITORY chriskohlhoff/asio
    GIT_TAG asio-1-30-2
)
if(NOT TARGET asio::asio)
    add_library(asio::asio INTERFACE IMPORTED)
    set_target_properties(asio::asio PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${asio_SOURCE_DIR}/asio/include")
endif()

CPMAddPackage(
    NAME jsoncpp
    GITHUB_REPOSITORY open-source-parsers/jsoncpp
    GIT_TAG 1.9.6
    OPTIONS
        "JSONCPP_WITH_TESTS OFF"
        "JSONCPP_WITH_POST_BUILD_UNITTEST OFF"
        "JSONCPP_WITH_PKGCONFIG_SUPPORT OFF"
        "JSONCPP_WITH_CMAKE_PACKAGE OFF"
        "JSONCPP_LIB_BUILD_STATIC ON"
        "BUILD_SHARED_LIBS OFF"
)

CPMAddPackage(
    NAME lz4
    GITHUB_REPOSITORY lz4/lz4
    GIT_TAG v1.10.0
)
if(NOT TARGET lz4::lz4)
    add_library(lz4_lib STATIC
        "${lz4_SOURCE_DIR}/lib/lz4.c"
        "${lz4_SOURCE_DIR}/lib/lz4hc.c"
        "${lz4_SOURCE_DIR}/lib/lz4frame.c"
        "${lz4_SOURCE_DIR}/lib/xxhash.c")
    target_include_directories(lz4_lib PUBLIC "${lz4_SOURCE_DIR}/lib")
    add_library(lz4::lz4 ALIAS lz4_lib)
endif()

# ── OpenVPN3 (local) ──────────────────────────────────────────────────────────
add_library(openvpn3_client STATIC
    thirdparty/minimal-openvpn3/client/ovpncli.cpp
    thirdparty/minimal-openvpn3/openvpn/crypto/data_epoch.cpp
    thirdparty/minimal-openvpn3/openvpn/openssl/xkey/xkey_helper.c
    thirdparty/minimal-openvpn3/openvpn/openssl/xkey/xkey_provider.c
)
target_include_directories(openvpn3_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/minimal-openvpn3
    ${asio_SOURCE_DIR}/asio/include
    ${jsoncpp_SOURCE_DIR}/include
)
target_compile_features(openvpn3_client PUBLIC cxx_std_20)
target_compile_definitions(openvpn3_client PUBLIC
    ASIO_STANDALONE USE_ASIO HAVE_LZ4 HAVE_JSONCPP NOMINMAX USE_OPENSSL
)
target_link_libraries(openvpn3_client PUBLIC
    asio::asio fmt jsoncpp_static lz4::lz4
    OpenSSL::SSL OpenSSL::Crypto Threads::Threads
)

# ── CAF 0.17.6 (system or local) ──────────────────────────────────────────────
set(CAF_LIB_DIR "/usr/lib/x86_64-linux-gnu" CACHE STRING "Directory holding libcaf_*.so")
set(CAF_INC_DIR "/usr/include" CACHE STRING "Directory holding caf/ headers")
if(NOT EXISTS "${CAF_LIB_DIR}/libcaf_core.so")
    set(CAF_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build-deps/usr/lib/x86_64-linux-gnu")
    set(CAF_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build-deps/usr/include")
endif()

add_library(caf_core SHARED IMPORTED)
set_target_properties(caf_core PROPERTIES
    IMPORTED_LOCATION "${CAF_LIB_DIR}/libcaf_core.so"
    INTERFACE_INCLUDE_DIRECTORIES "${CAF_INC_DIR}"
)

add_library(caf_io SHARED IMPORTED)
set_target_properties(caf_io PROPERTIES
    IMPORTED_LOCATION "${CAF_LIB_DIR}/libcaf_io.so"
    INTERFACE_INCLUDE_DIRECTORIES "${CAF_INC_DIR}"
    INTERFACE_LINK_LIBRARIES "caf_core"
)

# ── RTSP Compressor (minimal-cmpr, optional — requires GStreamer) ─────────────
set(ODC_HAS_RTSP_COMPRESSOR OFF CACHE BOOL "Enable RTSP compressor support")
pkg_check_modules(GST_COMPRESSOR gstreamer-rtsp-server-1.0)
if(GST_COMPRESSOR_FOUND)
    pkg_check_modules(GST_APP gstreamer-app-1.0)
    pkg_check_modules(GST_VIDEO gstreamer-video-1.0)
    pkg_check_modules(GST_RTP gstreamer-rtp-1.0)
    pkg_check_modules(GST_SDP gstreamer-sdp-1.0)
    pkg_check_modules(GST_PBUTILS gstreamer-pbutils-1.0)
    if(GST_APP_FOUND AND GST_VIDEO_FOUND AND GST_RTP_FOUND)
        set(ODC_HAS_RTSP_COMPRESSOR ON CACHE BOOL "Enable RTSP compressor support" FORCE)
        message(STATUS "RTSP compressor: ENABLED (GStreamer ${GST_COMPRESSOR_VERSION})")

        add_library(cmpr STATIC
            thirdparty/minimal-cmpr/src/RtspBridge.cpp
            thirdparty/minimal-cmpr/src/RtspAnalyzer.cpp
            thirdparty/minimal-cmpr/src/HardwareInfo.cpp
        )
        target_include_directories(cmpr PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/minimal-cmpr/include
            ${GST_COMPRESSOR_INCLUDE_DIRS}
            ${GST_APP_INCLUDE_DIRS}
            ${GST_VIDEO_INCLUDE_DIRS}
            ${GST_RTP_INCLUDE_DIRS}
            ${GST_SDP_INCLUDE_DIRS}
            ${GST_PBUTILS_INCLUDE_DIRS}
        )
        target_compile_features(cmpr PUBLIC cxx_std_20)
        target_compile_definitions(cmpr PUBLIC ODC_HAS_RTSP_COMPRESSOR=1)
        target_compile_options(cmpr PRIVATE
            ${GST_COMPRESSOR_CFLAGS_OTHER}
            ${GST_APP_CFLAGS_OTHER}
            ${GST_VIDEO_CFLAGS_OTHER}
        )
        target_link_libraries(cmpr PUBLIC
            thread_orchestrator
            Threads::Threads
            ${GST_COMPRESSOR_LDFLAGS}
            ${GST_APP_LDFLAGS}
            ${GST_VIDEO_LDFLAGS}
            ${GST_RTP_LDFLAGS}
            ${GST_SDP_LDFLAGS}
            ${GST_PBUTILS_LDFLAGS}
        )
    else()
        message(STATUS "RTSP compressor: DISABLED (missing GStreamer app/video/rtp plugins)")
    endif()
else()
    message(STATUS "RTSP compressor: DISABLED (GStreamer rtsp-server not found)")
endif()

# ── MAVLink Router (minimal-mavrouter) ────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED libudev)
pkg_check_modules(GUDEV REQUIRED gudev-1.0)
pkg_check_modules(GLIB REQUIRED glib-2.0 gobject-2.0 gio-2.0)

set(DEFAULT_DEVICE_CONFIG_DIR "/etc/odc-onboard/mavlink/devices" CACHE STRING "Default device config directory")
set(BUILD_TESTS OFF CACHE BOOL "Build test suite")
set(BUILD_HARDWARE_TESTS OFF CACHE BOOL "Build hardware tests")

add_subdirectory(thirdparty/minimal-mavrouter ${CMAKE_CURRENT_BINARY_DIR}/mavrouter_build EXCLUDE_FROM_ALL)

# ── Main binary ───────────────────────────────────────────────────────────────
add_executable(odc-onboard
    src/main.cpp
    src/mavlink/mav_router.cpp
    src/qmi/qmi_modem_controller.cpp
    src/qmi/qmi_state_machine.cpp
    src/qmi/qmi_services.cpp
    src/qmi/qmi_net_iface.cpp
    src/qmi/qmi_modem_info.cpp
    src/qmi/qmi_hw_info.cpp
    src/qmi/qmi_watcher.cpp
)

set(QMI_LIB_TARGET "qmi-glib")
if(NOT EXISTS "/usr/lib/x86_64-linux-gnu/libqmi-glib.so")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build-deps/usr/lib/x86_64-linux-gnu/libqmi-glib.so")
        set(QMI_LIB_TARGET "${CMAKE_CURRENT_SOURCE_DIR}/build-deps/usr/lib/x86_64-linux-gnu/libqmi-glib.so")
    endif()
endif()

target_link_libraries(odc-onboard PRIVATE
    spdlog::spdlog
    caf_core
    caf_io
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    fmt
    thread_orchestrator
    openvpn3_client
    mavrouter
    sodium
    Threads::Threads
    ${GLIB_LIBRARIES}
    ${GUDEV_LIBRARIES}
    ${QMI_LIB_TARGET}
)

if(ODC_HAS_RTSP_COMPRESSOR)
    target_link_libraries(odc-onboard PRIVATE cmpr)
endif()

set(QMI_INC_DIR "/usr/include/libqmi-glib" CACHE STRING "Directory holding libqmi-glib headers")
if(NOT EXISTS "${QMI_INC_DIR}/libqmi-glib.h")
    set(QMI_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build-deps/usr/include/libqmi-glib")
endif()

target_include_directories(odc-onboard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/minimal-mavrouter/modules/mavlink_c_library_v2/ardupilotmega
    ${GLIB_INCLUDE_DIRS}
    ${GUDEV_INCLUDE_DIRS}
    ${QMI_INC_DIR}
)

target_compile_options(odc-onboard PRIVATE
    -Wall -Wextra -Wpedantic -Wno-sign-compare
)

# ── Install target ────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS odc-onboard DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY configs/ DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/odc-onboard/vpn
    FILES_MATCHING PATTERN "*.conf" PATTERN "*.ovpn")
install(FILES odc-onboard.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)
