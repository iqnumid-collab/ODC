# Common source files (cross-platform)
set(ODC_COMMON_SOURCES
    src/logging.cpp
    src/yaml_config.cpp
    src/sqlite_db.cpp
    src/cert_manager.cpp
    src/http_client.cpp
)

# IPC sources (Linux/macOS only — Unix domain sockets)
if(NOT WIN32)
    list(APPEND ODC_COMMON_SOURCES
        src/ipc_server.cpp
        src/ipc_client.cpp
    )
endif()

add_library(odc-common STATIC ${ODC_COMMON_SOURCES})

target_include_directories(odc-common PUBLIC include)

target_link_libraries(odc-common PUBLIC
    spdlog::spdlog
    fmt::fmt
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    SQLite3::SQLite3
    asio::asio
)

target_compile_features(odc-common PUBLIC cxx_std_20)

# Platform-specific sources
if(WIN32)
    target_sources(odc-common PRIVATE src/platform_windows.cpp)
    target_link_libraries(odc-common PUBLIC ws2_32)
elseif(APPLE)
    target_sources(odc-common PRIVATE src/platform_macos.cpp)
else()
    target_sources(odc-common PRIVATE src/platform_linux.cpp)
endif()
