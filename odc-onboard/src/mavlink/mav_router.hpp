#pragma once

#include "mavlink/mav_events.hpp"
#include "mavlink/mav_config.hpp"
#include "core/event_bus.hpp"
#include "core/event.hpp"
#include "thread_orchestrator/orchestrator.hpp"

#include <nlohmann/json.hpp>
#include <mutex>
#include <vector>
#include <string>
#include <atomic>
#include <memory>

namespace odc {

class MavRouterImpl;

struct MavRouterStatus {
    bool running{false};
    unsigned long tcp_server_port{0};
    bool dedup_enabled{false};
    unsigned long sniffer_sysid{0};
    int endpoint_count{0};
    int connected_count{0};
    std::string extender_id;
    bool is_extender{false};
};

struct MavEndpointInfo {
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
};

struct MavRoutingStats {
    uint64_t total_routed{0};
    uint64_t total_filtered{0};
    uint64_t total_dedup_dropped{0};
    uint64_t total_errors{0};
};

class MavRouter {
public:
    MavRouter(const MavlinkConfig& config, EventBus& bus, const std::string& extender_id = "");
    ~MavRouter();

    MavRouter(const MavRouter&) = delete;
    MavRouter& operator=(const MavRouter&) = delete;

    void run(const orch::CancellationToken& token);

    MavRouterStatus get_status() const;
    std::vector<MavEndpointInfo> get_endpoints() const;
    MavRoutingStats get_stats() const;
    bool start_log();
    bool stop_log();

    std::string extender_id() const;
    bool is_extender() const;

private:
    std::unique_ptr<MavRouterImpl> impl_;
};

}
