cmake_minimum_required(VERSION 3.18)
project(odc-root VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── CPM ───────────────────────────────────────────────────────────────────────
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/_cpm/CPM_${CPM_VERSION}.cmake")
if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")
  message(STATUS "Downloading CPM.cmake")
  file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.5/CPM.cmake
    "${CPM_DOWNLOAD_LOCATION}"
  )
endif()
include("${CPM_DOWNLOAD_LOCATION}")

# ── CPM dependencies (all built from source for cross-compilation) ────────────

CPMAddPackage(
  NAME yaml-cpp
  GITHUB_REPOSITORY jbeder/yaml-cpp
  GIT_TAG 0.8.0
  OPTIONS "YAML_CPP_BUILD_TESTS OFF" "YAML_CPP_BUILD_TOOLS OFF"
)

CPMAddPackage(
  NAME jsoncpp
  GITHUB_REPOSITORY open-source-parsers/jsoncpp
  GIT_TAG 1.9.6
  OPTIONS "JSONCPP_WITH_TESTS OFF" "JSONCPP_WITH_POST_BUILD_UNITTEST OFF"
)

CPMAddPackage(
  NAME asio
  GITHUB_REPOSITORY chriskohlhoff/asio
  GIT_TAG asio-1-30-2
)
if(TARGET asio::asio)
elseif(TARGET asio)
  add_library(asio::asio ALIAS asio)
endif()

CPMAddPackage(
  NAME lz4
  GITHUB_REPOSITORY lz4/lz4
  GIT_TAG v1.10.0
  OPTIONS "LZ4_BUILD_CLI OFF" "LZ4_BUILD_LEGACY_LZ4C OFF"
)
if(TARGET lz4::lz4)
elseif(TARGET lz4_static)
  add_library(lz4::lz4 ALIAS lz4_static)
endif()

CPMAddPackage(
  NAME fmt
  GITHUB_REPOSITORY fmtlib/fmt
  GIT_TAG 10.2.1
  OPTIONS "FMT_DOC OFF" "FMT_TEST OFF" "FMT_INSTALL ON"
)

# Force spdlog to find CPM's fmt instead of system fmt
set(fmt_DIR "${fmt_BINARY_DIR}" CACHE PATH "fmt config directory" FORCE)

CPMAddPackage(
  NAME spdlog
  GITHUB_REPOSITORY gabime/spdlog
  GIT_TAG v1.14.1
  OPTIONS "SPDLOG_FMT_EXTERNAL ON" "SPDLOG_BUILD_SHARED OFF" "SPDLOG_BUILD_EXAMPLE OFF"
)

CPMAddPackage(
  NAME CLI11
  GITHUB_REPOSITORY CLIUtils/CLI11
  GIT_TAG v2.4.2
)

CPMAddPackage(
  NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  GIT_TAG v3.11.3
)

# libsodium — required by odc-hook VPN subsystem (Noise protocol, AEAD)
find_package(sodium QUIET)
if(NOT sodium_FOUND)
    # Check for system library via pkg-config or standard paths
    find_path(sodium_INCLUDE_DIR sodium.h)
    find_library(sodium_LIBRARY sodium)
    if(sodium_INCLUDE_DIR AND sodium_LIBRARY)
        message(STATUS "libsodium found: ${sodium_LIBRARY} (system)")
        add_library(sodium::sodium UNKNOWN IMPORTED)
        set_target_properties(sodium::sodium PROPERTIES
            IMPORTED_LOCATION "${sodium_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${sodium_INCLUDE_DIR}"
        )
    else()
        message(FATAL_ERROR "libsodium not found. Install libsodium-dev or set sodium_DIR.")
    endif()
endif()
# Always create the namespaced alias, regardless of how sodium was found
if(TARGET sodium AND NOT TARGET sodium::sodium)
    add_library(sodium::sodium ALIAS sodium)
elseif(TARGET sodium_static AND NOT TARGET sodium::sodium)
    add_library(sodium::sodium ALIAS sodium_static)
endif()

# SQLite3: use system if available, otherwise build from amalgamation via CPM
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
  message(STATUS "SQLite3 not found via find_package — building from amalgamation")
  CPMAddPackage(
    NAME sqlite3_source
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3450200.zip
    URL_HASH SHA256=bc9064412e07cffb7c1bbf28529c62999bd90c45e85ba1e0c6a61d316b9accc1
    DOWNLOAD_ONLY YES
  )
  if(NOT sqlite3_source_ADDED)
    message(FATAL_ERROR "SQLite3 unavailable: system package not found and amalgamation download failed")
  endif()
  # Build a real STATIC library (not an OBJECT alias) so that linking and
  # include-dir propagation work for every consumer of SQLite3::SQLite3.
  add_library(odc_sqlite3 STATIC ${sqlite3_source_SOURCE_DIR}/sqlite3.c)
  target_include_directories(odc_sqlite3 PUBLIC ${sqlite3_source_SOURCE_DIR})
  target_compile_definitions(odc_sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
  )
  set_target_properties(odc_sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS odc_sqlite3)
  endif()
endif()

# Final guarantee: the imported/alias target must exist before any subdirectory
# (common/, registry-manager/, odc-hook/, tests/) links against it.
#
# Some CMake FindSQLite3 variants set SQLite3_FOUND=TRUE but DO NOT create the
# SQLite3::SQLite3 imported target. In that case, synthesize it from the
# discovered include dirs and library path.
if(SQLite3_FOUND AND NOT TARGET SQLite3::SQLite3)
  message(STATUS "SQLite3 found but imported target missing — creating SQLite3::SQLite3 from found vars")
  if(NOT SQLite3_LIBRARIES OR NOT SQLite3_INCLUDE_DIRS)
    # Fall back to a plain search if the module left vars empty.
    find_library(SQLite3_LIBRARIES NAMES sqlite3)
    find_path(SQLite3_INCLUDE_DIRS NAMES sqlite3.h)
  endif()
  add_library(SQLite3::SQLite3 UNKNOWN IMPORTED)
  set_target_properties(SQLite3::SQLite3 PROPERTIES
    IMPORTED_LOCATION "${SQLite3_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${SQLite3_INCLUDE_DIRS}"
  )
endif()

if(NOT TARGET SQLite3::SQLite3)
  message(FATAL_ERROR "SQLite3::SQLite3 target was not created (find_package reported FOUND=${SQLite3_FOUND})")
endif()

CPMAddPackage(
  NAME catch2
  GITHUB_REPOSITORY catchorg/Catch2
  GIT_TAG v3.7.1
)

# ── System dependencies (only OpenSSL — the rest are CPM-managed) ─────────────
find_package(OpenSSL REQUIRED)

# ── asio target setup ─────────────────────────────────────────────────────────
if(NOT TARGET asio::asio)
  add_library(asio_asio INTERFACE)
  target_include_directories(asio_asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
  target_compile_definitions(asio_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
  add_library(asio::asio ALIAS asio_asio)
endif()

# ── Subdirectories ────────────────────────────────────────────────────────────
option(BUILD_TUNNEL_MANAGER "Build odc-tunnel-manager (Linux-only)" ON)
option(BUILD_REGISTRY_MANAGER "Build odc-registry-manager (Linux-only)" ON)
option(BUILD_ODC_CLI "Build odc-cli tool" ON)
option(BUILD_ODC_HOOK "Build odc-hook cross-platform hook client" ON)
option(BUILD_ODC_HOOK_VPN "Build VPN client support into odc-hook (requires OpenVPN3 + WireGuard crypto + libsodium)" OFF)

# OpenVPN3 core library — required by tunnel-manager in-process server and odc-hook VPN
if(BUILD_ODC_HOOK_VPN OR (BUILD_TUNNEL_MANAGER AND NOT WIN32 AND NOT APPLE))
    if(NOT TARGET openvpn3)
        add_subdirectory(thirdparty/minimal-openvpn3)
    endif()
    if(TARGET openvpn3 AND NOT TARGET openvpn3_client)
        add_library(openvpn3_client ALIAS openvpn3)
    endif()
endif()

add_subdirectory(common)

if(BUILD_TUNNEL_MANAGER AND NOT WIN32 AND NOT APPLE)
  add_subdirectory(tunnel-manager)
endif()

if(BUILD_REGISTRY_MANAGER AND NOT WIN32 AND NOT APPLE)
  add_subdirectory(registry-manager)
endif()

if(BUILD_ODC_CLI)
  add_subdirectory(tools/odc-cli)
endif()

if(BUILD_ODC_HOOK)
  add_subdirectory(odc-hook)
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
option(BUILD_TESTING "Build and run tests" ON)
if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()
