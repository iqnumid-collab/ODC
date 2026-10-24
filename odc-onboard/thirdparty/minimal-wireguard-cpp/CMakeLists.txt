cmake_minimum_required(VERSION 3.16)
project(minimal-wireguard-cpp LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# --- Dependency paths (from wireguard-auto build) ---
set(WG_AUTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../wireguard-auto" CACHE PATH "Path to wireguard-auto build tree")
set(SPDLOG_INCLUDE  "${WG_AUTO_DIR}/external/lib/include")
set(SPDLOG_LIB      "${WG_AUTO_DIR}/external/lib/libspdlog.a")
set(BOOST_INCLUDE   "${WG_AUTO_DIR}/external/boost_1_88_0")
set(SODIUM_INCLUDE  "${WG_AUTO_DIR}/external/libsodium-stable/output/include")
set(SODIUM_LIB      "${WG_AUTO_DIR}/external/libsodium-stable/output/lib/libsodium.a")

# --- Imported targets ---
add_library(spdlog STATIC IMPORTED)
set_target_properties(spdlog PROPERTIES IMPORTED_LOCATION "${SPDLOG_LIB}")
target_include_directories(spdlog INTERFACE "${SPDLOG_INCLUDE}")

add_library(sodium STATIC IMPORTED)
set_target_properties(sodium PROPERTIES IMPORTED_LOCATION "${SODIUM_LIB}")
target_include_directories(sodium INTERFACE "${SODIUM_INCLUDE}")

# --- wg-tools C library ---
add_library(wg-tools STATIC
    third_party/wg-tools/genkey.c
    third_party/wg-tools/pubkey.c
    third_party/wg-tools/encoding.c
    third_party/wg-tools/curve25519.c
    third_party/wg-tools/sec_store.c
)
target_include_directories(wg-tools
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wg-tools
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wg-tools/uapi/linux
)
target_compile_options(wg-tools PRIVATE -fPIC -O2)
target_compile_definitions(wg-tools PRIVATE NO_FILE_ENCRYPTYION)
target_link_libraries(wg-tools PRIVATE sodium)

# --- Client static library ---
add_library(wgclient STATIC
    src/client.cpp
    src/communication.cpp
    src/configuration.cpp
    src/sodium_ae.cpp
    src/parser.cpp
    src/common.cpp
)
target_include_directories(wgclient
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE "${BOOST_INCLUDE}"
)
target_compile_definitions(wgclient PRIVATE AUTHENTICATED_ENCRYPTION SPDLOG_COMPILED_LIB)
target_link_libraries(wgclient PUBLIC wg-tools spdlog sodium)
