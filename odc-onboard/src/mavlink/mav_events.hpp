#pragma once

#include <chrono>
#include <string>
#include <cstdint>

namespace odc {

struct MavRouterStatusEvent {
    bool running{false};
    unsigned long tcp_server_port{0};
    bool dedup_enabled{false};
    unsigned long sniffer_sysid{0};
    int endpoint_count{0};
    int connected_count{0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct MavEndpointStatusEvent {
    std::string name;
    std::string type;
    std::string device_or_address;
    int port{0};
    bool connected{false};
    uint64_t messages_in{0};
    uint64_t messages_out{0};
    uint64_t messages_lost{0};
    uint64_t bytes_in{0};
    uint64_t bytes_out{0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct MavRoutingStatsEvent {
    uint64_t total_routed{0};
    uint64_t total_filtered{0};
    uint64_t total_dedup_dropped{0};
    uint64_t total_errors{0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct MavLogStatusEvent {
    bool logging{false};
    std::string log_file;
    uint64_t log_size_bytes{0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

}
