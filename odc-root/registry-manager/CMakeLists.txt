add_executable(odc-registry-manager
    main.cpp
    registry_manager.cpp
    ws_server.cpp
    webhook_client.cpp
    data_store.cpp
    ws_cert_generator.cpp
    cert_revocation.cpp
)

target_link_libraries(odc-registry-manager PRIVATE
    odc-common
    CLI11::CLI11
    asio::asio
    OpenSSL::SSL
)

target_compile_features(odc-registry-manager PRIVATE cxx_std_20)

install(TARGETS odc-registry-manager RUNTIME DESTINATION bin)
install(FILES config/registry-manager.yaml DESTINATION /etc/odc-root)
