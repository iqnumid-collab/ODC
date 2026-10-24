function(add_odc_test TARGET)
    add_executable(${TARGET} ${ARGN})
    target_link_libraries(${TARGET} PRIVATE
        odc-common
        Catch2::Catch2WithMain
        asio::asio
        OpenSSL::SSL
        OpenSSL::Crypto
        CLI11::CLI11
        spdlog::spdlog
        fmt::fmt
    )
    target_compile_features(${TARGET} PRIVATE cxx_std_20)
    target_compile_definitions(${TARGET} PRIVATE SPDLOG_COMPILED_LIB)
    target_include_directories(${TARGET} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/odc-hook
        ${CMAKE_SOURCE_DIR}/registry-manager
    )
    if(WIN32)
        target_link_libraries(${TARGET} PRIVATE ws2_32)
    endif()
    add_test(NAME ${TARGET} COMMAND ${TARGET})
endfunction()

# Existing common library tests
add_odc_test(test_common test_common.cpp
    ${CMAKE_SOURCE_DIR}/common/src/sqlite_db.cpp
    ${CMAKE_SOURCE_DIR}/common/src/yaml_config.cpp
    ${CMAKE_SOURCE_DIR}/common/src/cert_manager.cpp
    ${CMAKE_SOURCE_DIR}/common/src/ipc_server.cpp
    ${CMAKE_SOURCE_DIR}/common/src/ipc_client.cpp
    ${CMAKE_SOURCE_DIR}/common/src/logging.cpp
    ${CMAKE_SOURCE_DIR}/common/src/platform_linux.cpp
)

# Existing IPC integration test
add_odc_test(test_ipc_integration test_ipc_integration.cpp
    ${CMAKE_SOURCE_DIR}/common/src/ipc_server.cpp
    ${CMAKE_SOURCE_DIR}/common/src/ipc_client.cpp
    ${CMAKE_SOURCE_DIR}/common/src/sqlite_db.cpp
    ${CMAKE_SOURCE_DIR}/common/src/logging.cpp
    ${CMAKE_SOURCE_DIR}/common/src/platform_linux.cpp
)

# ── odc-hook unit tests ──────────────────────────────────────────────────────

# T4.1: EventBus — standalone, only needs nlohmann_json
add_odc_test(test_event_bus test_event_bus.cpp
    ${CMAKE_SOURCE_DIR}/odc-hook/event_bus.cpp
)

# T4.2: ThreadPool — needs EventBus + logging from common
add_odc_test(test_thread_pool test_thread_pool.cpp
    ${CMAKE_SOURCE_DIR}/odc-hook/event_bus.cpp
    ${CMAKE_SOURCE_DIR}/odc-hook/thread_pool.cpp
    ${CMAKE_SOURCE_DIR}/common/src/logging.cpp
    ${CMAKE_SOURCE_DIR}/common/src/platform_linux.cpp
)

# T4.3: WsClient unit tests — needs WsClient static helpers + SSL
add_odc_test(test_ws_client test_ws_client.cpp
    ${CMAKE_SOURCE_DIR}/odc-hook/ws_client.cpp
    ${CMAKE_SOURCE_DIR}/common/src/logging.cpp
    ${CMAKE_SOURCE_DIR}/common/src/platform_linux.cpp
)

# T4.4: E2E mTLS integration — needs full odc-hook + registry-manager
add_odc_test(test_hook_mtls_integration test_hook_mtls_integration.cpp
    ${CMAKE_SOURCE_DIR}/odc-hook/hook_service.cpp
    ${CMAKE_SOURCE_DIR}/odc-hook/ws_client.cpp
    ${CMAKE_SOURCE_DIR}/odc-hook/event_bus.cpp
    ${CMAKE_SOURCE_DIR}/odc-hook/thread_pool.cpp
    ${CMAKE_SOURCE_DIR}/registry-manager/ws_server.cpp
    ${CMAKE_SOURCE_DIR}/registry-manager/cert_revocation.cpp
    ${CMAKE_SOURCE_DIR}/common/src/sqlite_db.cpp
    ${CMAKE_SOURCE_DIR}/common/src/yaml_config.cpp
    ${CMAKE_SOURCE_DIR}/common/src/cert_manager.cpp
    ${CMAKE_SOURCE_DIR}/common/src/logging.cpp
    ${CMAKE_SOURCE_DIR}/common/src/platform_linux.cpp
)

# T5: CLI subprocess tests — runs odc-cli binary via popen
add_odc_test(test_cli test_cli.cpp)
