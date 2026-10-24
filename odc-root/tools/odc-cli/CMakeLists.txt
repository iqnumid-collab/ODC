add_executable(odc-cli
    main.cpp
    ${CMAKE_SOURCE_DIR}/odc-hook/ws_client.cpp
)

target_include_directories(odc-cli PRIVATE
    ${CMAKE_SOURCE_DIR}/odc-hook
)

target_link_libraries(odc-cli PRIVATE
    odc-common
    CLI11::CLI11
    asio::asio
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_features(odc-cli PRIVATE cxx_std_20)

if(WIN32)
    target_link_libraries(odc-cli PRIVATE ws2_32)
endif()

install(TARGETS odc-cli RUNTIME DESTINATION bin)
