#pragma once

#include "core/vpn_monitor.hpp"
#include "core/event_bus.hpp"

#include <caf/event_based_actor.hpp>
#include <caf/behavior.hpp>

namespace odc {

class vpn_monitor_actor_impl : public caf::event_based_actor {
public:
    explicit vpn_monitor_actor_impl(caf::actor_config& cfg, VpnInterfaceMonitor& monitor)
        : event_based_actor(cfg), monitor_(monitor) {}

    caf::behavior make_behavior() override {
        return {
            [this](mon_tick) {
                auto snapshot = monitor_.snapshot_all();
                for (const auto& iface : snapshot) {
                    (void)iface;
                }
            },
            [this](const std::string& cmd) -> std::vector<std::pair<std::string, std::string>> {
                if (cmd == "list") {
                    return monitor_.get_tracked_interfaces();
                }
                return {};
            }
        };
    }

private:
    VpnInterfaceMonitor& monitor_;
};

inline auto make_vpn_monitor_actor(caf::actor_system& sys, VpnInterfaceMonitor& monitor) {
    return sys.spawn<vpn_monitor_actor_impl>(std::ref(monitor));
}

}
