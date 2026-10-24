find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUDEV REQUIRED libudev)

# Library target (all sources except main.cpp)
add_library(mavrouter STATIC
    autolog.cpp
    binlog.cpp
    dedup.cpp
    device_config_loader.cpp
    endpoint.cpp
    git_version.cpp
    logendpoint.cpp
    mainloop.cpp
    pollable.cpp
    timeout.cpp
    ulog.cpp
    tlog.cpp
    udev_monitor.cpp
)

target_include_directories(mavrouter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBUDEV_INCLUDE_DIRS}
)

target_link_libraries(mavrouter PUBLIC
    common
    ${LIBUDEV_LIBRARIES}
    ${LIB_M}
    ${LIB_RT}
    Threads::Threads
)

# Executable target
add_executable(mavlink-routerd main.cpp)

target_link_libraries(mavlink-routerd PRIVATE
    mavrouter
    ${LIB_M}
    ${LIB_RT}
    Threads::Threads
    ${LIBUDEV_LIBRARIES}
)
