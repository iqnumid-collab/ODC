find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

add_executable(test_minimal_mavrouter
    test_endpoint.cpp
    test_mainloop.cpp
    test_udev_monitor.cpp
    test_device_config_loader.cpp
    test_routing.cpp
)

target_include_directories(test_minimal_mavrouter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
    ${CMAKE_SOURCE_DIR}/modules/mavlink_c_library_v2/ardupilotmega
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(test_minimal_mavrouter PRIVATE
    mavrouter
    common
    GTest::GTest
    GTest::Main
    Threads::Threads
    ${LIB_M}
    ${LIB_RT}
    ${LIBUDEV_LIBRARIES}
)

target_compile_options(test_minimal_mavrouter PRIVATE
    "-include${CMAKE_BINARY_DIR}/config.h"
    "-include${CMAKE_BINARY_DIR}/build_config.h"
)

add_test(NAME unit_tests COMMAND test_minimal_mavrouter)

# Hardware tests (only built if enabled)
if(BUILD_HARDWARE_TESTS)
    add_executable(test_hardware
        hardware/test_mavlink_device.cpp
    )

    target_include_directories(test_hardware PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
        ${CMAKE_SOURCE_DIR}/modules/mavlink_c_library_v2/ardupilotmega
        ${CMAKE_BINARY_DIR}
    )

    target_link_libraries(test_hardware PRIVATE
        mavrouter
        common
        GTest::GTest
        GTest::Main
        Threads::Threads
        ${LIB_M}
        ${LIB_RT}
        ${LIBUDEV_LIBRARIES}
    )

    target_compile_options(test_hardware PRIVATE
        "-include${CMAKE_BINARY_DIR}/config.h"
        "-include${CMAKE_BINARY_DIR}/build_config.h"
    )

    add_test(NAME hardware_tests COMMAND test_hardware)
endif()
