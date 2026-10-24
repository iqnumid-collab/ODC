#pragma once

#include "core/vpn_state.hpp"

#include <string>

namespace odc::graph {

enum class ComponentRuntimeState : uint8_t {
    Stopped,
    Armed,
    Starting,
    Running,
    Stopping,
    Error
};

inline std::string_view to_string_view(ComponentRuntimeState s) {
    switch (s) {
        case ComponentRuntimeState::Stopped:   return "stopped";
        case ComponentRuntimeState::Armed:     return "armed";
        case ComponentRuntimeState::Starting:  return "starting";
        case ComponentRuntimeState::Running:   return "running";
        case ComponentRuntimeState::Stopping:  return "stopping";
        case ComponentRuntimeState::Error:     return "error";
    }
    return "unknown";
}

struct TransportBinding {
    std::string connection_id;
    std::string interface_name;
    std::string target_ip;
    int prefix_len{24};
    VpnState vpn_state{VpnState::Idle};
    bool ip_ready{false};
};

struct WanReady {
    std::string component_id;
    std::string iface_name;
    std::string ipv4_address;
    std::string ipv6_address;
    bool ready{false};
};

/** Cellular metrics pushed on the cellular.qmi `metrics` DSPatch output. */
struct CellularMetrics {
    std::string component_id;
    std::string iface_name;
    bool valid{false};
    int8_t   rssi_dbm{0};
    int16_t  lte_rsrp{0};
    float    lte_snr{0.0f};
    int      registration_state{-1};
    int      roaming{-1};
    int      connection_status{-1};
    bool     internet_reachable{false};
    int      ping_latency_ms{-1};
    std::string operator_name;
};

struct RouterRef {
    void* router{nullptr};
};

} // namespace odc::graph
