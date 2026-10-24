#pragma once

#include "vpn/vpn_manager.hpp"
#include "vpn/vpn_config.hpp"
#include "core/event_bus.hpp"

#include <caf/event_based_actor.hpp>
#include <caf/behavior.hpp>
#include <caf/allowed_unsafe_message_type.hpp>

namespace odc {

struct VpnCommandResult {
    bool success;
    std::string message;
};

}

CAF_ALLOW_UNSAFE_MESSAGE_TYPE(odc::VpnCommandResult)

namespace odc {

class vpn_manager_actor_impl : public caf::event_based_actor {
public:
    explicit vpn_manager_actor_impl(caf::actor_config& cfg, VpnManager& manager)
        : event_based_actor(cfg), manager_(manager) {}

    caf::behavior make_behavior() override {
        return {
            [this](const std::string& cmd, const std::string& id) -> VpnCommandResult {
                return handle_command(cmd, id, {}, {});
            },
            [this](const std::string& cmd, const std::string& id, const std::string& config_path) -> VpnCommandResult {
                return handle_command(cmd, id, config_path, {});
            },
            [this](mon_tick) {
                auto connections = manager_.list_connections();
                for (const auto& conn : connections) {
                    if (conn.state == VpnState::Connected) {
                        auto stats = conn.stats;
                        auto health = conn.health;
                        (void)stats;
                        (void)health;
                    }
                }
            }
        };
    }

    std::vector<VpnConnectionInfo> list_connections() {
        return manager_.list_connections();
    }

private:
    VpnManager& manager_;

    VpnCommandResult handle_command(
        const std::string& cmd,
        const std::string& id,
        const std::string& config_path,
        const std::string&
    ) {
        if (cmd == "start") {
            if (manager_.start_connection(id)) {
                return {true, "Started connection: " + id};
            }
            return {false, "Failed to start connection: " + id};
        }

        if (cmd == "stop") {
            if (manager_.stop_connection(id)) {
                return {true, "Stopped connection: " + id};
            }
            return {false, "Failed to stop connection: " + id};
        }

        if (cmd == "restart") {
            if (manager_.restart_connection(id)) {
                return {true, "Restarted connection: " + id};
            }
            return {false, "Failed to restart connection: " + id};
        }

        if (cmd == "remove") {
            if (manager_.remove_connection(id)) {
                return {true, "Removed connection: " + id};
            }
            return {false, "Failed to remove connection: " + id};
        }

        return {false, "Unknown command: " + cmd};
    }
};

inline auto make_vpn_manager_actor(caf::actor_system& sys, VpnManager& manager) {
    return sys.spawn<vpn_manager_actor_impl>(std::ref(manager));
}

}
