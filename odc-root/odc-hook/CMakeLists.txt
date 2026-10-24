add_executable(odc-hook
    main.cpp
    hook_service.cpp
    ws_client.cpp
    event_bus.cpp
    thread_pool.cpp
)

if(BUILD_ODC_HOOK_VPN)
    target_sources(odc-hook PRIVATE
        vpn/vpn_manager.cpp
        vpn/wireguard_client.cpp
        vpn/openvpn_client.cpp
        vpn/tun_device.cpp
        vpn/wg_noise.cpp
        vpn/wg_aead.cpp
        vpn/wg_device.cpp
        vpn/wg_peer.cpp
    )
    target_link_libraries(odc-hook PRIVATE
        openvpn3_client
        sodium::sodium
    )
    target_include_directories(odc-hook PRIVATE
        ${CMAKE_SOURCE_DIR}/thirdparty/minimal-openvpn3
    )
    target_compile_definitions(odc-hook PRIVATE ODC_HOOK_HAS_VPN=1)
endif()

target_link_libraries(odc-hook PRIVATE
    odc-common
    CLI11::CLI11
    asio::asio
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_features(odc-hook PRIVATE cxx_std_20)

if(WIN32)
    target_link_libraries(odc-hook PRIVATE ws2_32)
endif()

install(TARGETS odc-hook RUNTIME DESTINATION bin)
install(FILES config/odc-hook.yaml DESTINATION /etc/odc-root)
