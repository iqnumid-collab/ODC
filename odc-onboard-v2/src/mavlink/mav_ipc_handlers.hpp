#pragma once

#include "mavlink/mav_router.hpp"
#include "ipc/method_registry.hpp"
#include <nlohmann/json.hpp>

namespace odc {

inline void register_mavlink_methods(ipc::MethodRegistry& registry, MavRouter& router) {
    registry.register_method("mavlink.status", [&router](const nlohmann::json&) {
        auto s = router.get_status();
        nlohmann::json j;
        j["running"] = s.running;
        j["tcp_server_port"] = s.tcp_server_port;
        j["dedup_enabled"] = s.dedup_enabled;
        j["sniffer_sysid"] = s.sniffer_sysid;
        j["endpoint_count"] = s.endpoint_count;
        j["connected_count"] = s.connected_count;
        return j;
    });

    registry.register_method("mavlink.endpoints", [&router](const nlohmann::json&) {
        auto eps = router.get_endpoints();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& ep : eps) {
            nlohmann::json j;
            j["name"] = ep.name;
            j["type"] = ep.type;
            j["device_or_address"] = ep.device_or_address;
            j["port"] = ep.port;
            j["connected"] = ep.connected;
            j["messages_in"] = ep.messages_in;
            j["messages_out"] = ep.messages_out;
            j["messages_lost"] = ep.messages_lost;
            j["bytes_in"] = ep.bytes_in;
            j["bytes_out"] = ep.bytes_out;
            arr.push_back(j);
        }
        return arr;
    });

    registry.register_method("mavlink.stats", [&router](const nlohmann::json&) {
        auto s = router.get_stats();
        nlohmann::json j;
        j["total_routed"] = s.total_routed;
        j["total_filtered"] = s.total_filtered;
        j["total_dedup_dropped"] = s.total_dedup_dropped;
        j["total_errors"] = s.total_errors;
        return j;
    });

    registry.register_method("mavlink.start_log", [&router](const nlohmann::json&) {
        bool ok = router.start_log();
        return nlohmann::json{{"success", ok}};
    });

    registry.register_method("mavlink.stop_log", [&router](const nlohmann::json&) {
        bool ok = router.stop_log();
        return nlohmann::json{{"success", ok}};
    });
}

}
