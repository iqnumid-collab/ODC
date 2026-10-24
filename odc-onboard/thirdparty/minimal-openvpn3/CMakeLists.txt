cmake_minimum_required(VERSION 3.20)
project(OpenVPN3Core VERSION 3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

option(USE_MBEDTLS "Use mbedTLS instead of OpenSSL" OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ── FetchContent dependencies ────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
    GIT_SHALLOW    ON)

FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        11.1.4
    GIT_SHALLOW    ON)

FetchContent_Declare(jsoncpp
    GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
    GIT_TAG        1.9.6
    GIT_SHALLOW    ON)

FetchContent_Declare(lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG        v1.10.0
    GIT_SHALLOW    ON)

# asio — header-only, non-standard layout
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
endif()
if(NOT TARGET asio::asio)
    add_library(asio::asio INTERFACE IMPORTED)
    set_target_properties(asio::asio PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${asio_SOURCE_DIR}/asio/include")
endif()

# fmt
FetchContent_MakeAvailable(fmt)

# jsoncpp
set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_CMAKE_PACKAGE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(jsoncpp)

# lz4 — build from lib/ sources directly
FetchContent_GetProperties(lz4)
if(NOT lz4_POPULATED)
    FetchContent_Populate(lz4)
endif()
if(NOT TARGET lz4::lz4)
    add_library(lz4_lib STATIC
        "${lz4_SOURCE_DIR}/lib/lz4.c"
        "${lz4_SOURCE_DIR}/lib/lz4hc.c"
        "${lz4_SOURCE_DIR}/lib/lz4frame.c"
        "${lz4_SOURCE_DIR}/lib/xxhash.c")
    target_include_directories(lz4_lib PUBLIC "${lz4_SOURCE_DIR}/lib")
    add_library(lz4::lz4 ALIAS lz4_lib)
endif()

# ── SSL backend ──────────────────────────────────────────────────────────────
find_package(PkgConfig QUIET)
if(USE_MBEDTLS)
    if(PkgConfig_FOUND)
        pkg_search_module(MBEDTLS IMPORTED_TARGET mbedtls3.6 mbedtls)
    endif()
    if(MBEDTLS_FOUND)
        pkg_search_module(MBEDCRYPTO REQUIRED IMPORTED_TARGET mbedcrypto3.6 mbedcrypto)
        pkg_search_module(MBEDX509 REQUIRED IMPORTED_TARGET mbedx5093.6 mbedx509)
        set(SSL_TARGETS PkgConfig::MBEDTLS PkgConfig::MBEDCRYPTO PkgConfig::MBEDX509)
    else()
        find_package(MbedTLS REQUIRED)
        set(SSL_TARGETS MbedTLS::mbedtls MbedTLS::mbedcrypto MbedTLS::mbedx509)
    endif()
else()
    find_package(OpenSSL REQUIRED)
    set(SSL_TARGETS OpenSSL::SSL OpenSSL::Crypto)
endif()

find_package(Threads REQUIRED)

# ── Sources ──────────────────────────────────────────────────────────────────
set(OVPN3_SOURCES
    client/ovpncli.cpp
    openvpn/crypto/data_epoch.cpp)

if(NOT USE_MBEDTLS)
    list(APPEND OVPN3_SOURCES
        openvpn/openssl/xkey/xkey_helper.c
        openvpn/openssl/xkey/xkey_provider.c)
endif()

# ── Static library ───────────────────────────────────────────────────────────
add_library(openvpn3 STATIC ${OVPN3_SOURCES})

target_include_directories(openvpn3 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)

target_compile_definitions(openvpn3 PUBLIC
    ASIO_STANDALONE USE_ASIO HAVE_LZ4 HAVE_JSONCPP NOMINMAX)

if(USE_MBEDTLS)
    target_compile_definitions(openvpn3 PUBLIC USE_MBEDTLS)
else()
    target_compile_definitions(openvpn3 PUBLIC USE_OPENSSL)
endif()

if(WIN32)
    target_compile_definitions(openvpn3 PUBLIC
        _WIN32_WINNT=0x0600 TAP_WIN_COMPONENT_ID=tap0901
        _CRT_SECURE_NO_WARNINGS ASIO_DISABLE_LOCAL_SOCKETS)
endif()

target_link_libraries(openvpn3 PUBLIC
    asio::asio fmt::fmt jsoncpp_static lz4::lz4
    ${SSL_TARGETS} Threads::Threads)

if(WIN32)
    target_link_libraries(openvpn3 PUBLIC
        fwpuclnt iphlpapi wininet setupapi rpcrt4 wtsapi32 ws2_32 wsock32)
endif()

if(APPLE)
    find_library(CF CoreFoundation)
    find_library(IOKIT IOKit)
    find_library(CS CoreServices)
    find_library(SC SystemConfiguration)
    target_link_libraries(openvpn3 PUBLIC ${CF} ${IOKIT} ${CS} ${SC})
endif()

# ── Compiler options ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(openvpn3 PRIVATE /bigobj /MP /W3 /wd4200 /wd4146)
else()
    target_compile_options(openvpn3 PRIVATE -Wall -Wsign-compare -Wnon-virtual-dtor)
    if(MINGW)
        target_compile_options(openvpn3 PRIVATE -Wa,-mbig-obj)
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(openvpn3 PRIVATE -Wno-maybe-uninitialized)
    endif()
endif()

# ── Install ──────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS openvpn3 ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY openvpn/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/openvpn
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(FILES client/ovpncli.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/client)
