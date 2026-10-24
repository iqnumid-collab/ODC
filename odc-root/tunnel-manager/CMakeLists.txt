add_executable(odc-tunnel-manager
    main.cpp
    tunnel_manager.cpp
    wireguard_server.cpp
    openvpn_server.cpp
    openvpn_transport.cpp
    openvpn_tun.cpp
    openvpn_management.cpp
    session_tracker.cpp
    vpn_cert_generator.cpp
)

target_include_directories(odc-tunnel-manager PRIVATE
    ${CMAKE_SOURCE_DIR}/thirdparty
    ${CMAKE_SOURCE_DIR}/thirdparty/minimal-openvpn3
)

target_link_libraries(odc-tunnel-manager PRIVATE
    odc-common
    CLI11::CLI11
    cap
    openvpn3_client
    asio::asio
)

target_compile_features(odc-tunnel-manager PRIVATE cxx_std_20)
target_compile_definitions(odc-tunnel-manager PRIVATE
    ASIO_STANDALONE
    USE_ASIO
    USE_OPENSSL
    HAVE_LZ4
    HAVE_JSONCPP
)
# Ensure OPENVPN_LOG macros exist before any OpenVPN3 header is parsed.
target_compile_options(odc-tunnel-manager PRIVATE
    -include ${CMAKE_SOURCE_DIR}/thirdparty/minimal-openvpn3/openvpn/log/logsimple.hpp
)

install(TARGETS odc-tunnel-manager RUNTIME DESTINATION bin)
install(FILES config/tunnel-manager.yaml DESTINATION /etc/odc-root)
