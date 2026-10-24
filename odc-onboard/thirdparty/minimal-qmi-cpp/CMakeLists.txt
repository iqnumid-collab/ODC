cmake_minimum_required(VERSION 3.16)
project(ultima-qmi C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Build the ported static libraries (glib, libmbim, libqmi) ──
find_package(PkgConfig REQUIRED)
add_subdirectory(deps)

# ── Additional deps for the controller ──
pkg_check_modules(GUDEV REQUIRED IMPORTED_TARGET gudev-1.0)

# ── Thread orchestrator (header-only, from sibling project) ──
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../thread-orchestrator thread-orchestrator)

# ── Kafka library (from sibling project, same pattern as minimal-vpn-agent) ──
set(KAFKA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../minimal-librdkafka-cpp/src")
add_library(kafka-lib STATIC
    ${KAFKA_SRC_DIR}/kafka/connection/tcp_connection.cpp
    ${KAFKA_SRC_DIR}/kafka/connection/tls_connection.cpp
    ${KAFKA_SRC_DIR}/kafka/protocol/protocol.cpp
    ${KAFKA_SRC_DIR}/kafka/producer/producer.cpp
    ${KAFKA_SRC_DIR}/kafka/consumer/consumer.cpp
)
target_include_directories(kafka-lib PUBLIC ${KAFKA_SRC_DIR})
# kafka-lib was written for C++20 (same as minimal-librdkafka-cpp's own CMakeLists)
target_compile_features(kafka-lib PUBLIC cxx_std_20)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(kafka-lib PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

# ── Protobuf codegen ──────────────────────────────────────────
find_package(Protobuf REQUIRED)

set(PROTO_SRC_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto)
file(MAKE_DIRECTORY ${PROTO_SRC_DIR})

set(PROTO_FILE   ${CMAKE_CURRENT_SOURCE_DIR}/proto/qmi_agent.proto)
set(PROTO_GEN_CC ${PROTO_SRC_DIR}/qmi_agent.pb.cc)
set(PROTO_GEN_H  ${PROTO_SRC_DIR}/qmi_agent.pb.h)

add_custom_command(
    OUTPUT ${PROTO_GEN_CC} ${PROTO_GEN_H}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
        --cpp_out=${PROTO_SRC_DIR}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating protobuf sources from qmi_agent.proto"
)

add_library(qmi-proto STATIC ${PROTO_GEN_CC})
target_include_directories(qmi-proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(qmi-proto PUBLIC ${Protobuf_LIBRARIES})
target_include_directories(qmi-proto PUBLIC ${Protobuf_INCLUDE_DIRS})

# ── Controller executable ──
add_executable(ultima-qmi-controller
    src/main.cpp
    src/controller.cpp
    src/state_machine.cpp
    src/qmi_services.cpp
    src/net_iface.cpp
    src/config.cpp
    src/backoff.cpp
    src/modem_info.cpp
    src/hw_info.cpp
    src/udev_monitor.cpp
    src/kafka/event_publisher.cpp
    src/kafka/command_handler.cpp
)

target_include_directories(ultima-qmi-controller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(ultima-qmi-controller PRIVATE
    GLIB_STATIC_COMPILATION
    GOBJECT_STATIC_COMPILATION
    GIO_STATIC_COMPILATION
    GMODULE_STATIC_COMPILATION
)

target_link_libraries(ultima-qmi-controller PRIVATE
    qmi-glib
    mbim-glib
    gio-2.0
    gobject-2.0
    gmodule-2.0
    gthread-2.0
    glib-2.0
    PkgConfig::GUDEV
    kafka-lib
    qmi-proto
    thread_orchestrator
)
