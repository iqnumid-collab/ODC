#pragma once

#include "mavlink/mav_router.hpp"
#include "mavlink/mav_config.hpp"
#include "core/event_bus.hpp"
#include "core/event.hpp"
#include "thread_orchestrator/orchestrator.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace odc {

class MavExtenderManager {
public:
    MavExtenderManager(
        EventBus& bus,
        orch::ThreadOrchestrator& orchestrator,
        const MavlinkConfig& base_config
    )
        : bus_(bus), orchestrator_(orchestrator), base_config_(base_config),
          auto_enabled_(base_config.extender_auto.enabled),
          udp_port_(base_config.extender_auto.udp_port) {}

    ~MavExtenderManager() {
        stop_all();
    }

    MavExtenderManager(const MavExtenderManager&) = delete;
    MavExtenderManager& operator=(const MavExtenderManager&) = delete;

    void start() {
        iface_sub_ = bus_.subscribe([this](const Event& evt) {
            if (auto* iface_evt = std::get_if<VpnInterfaceEvent>(&evt)) {
                on_vpn_interface_event(*iface_evt);
            }
        });

        state_sub_ = bus_.subscribe([this](const Event& evt) {
            if (auto* state_evt = std::get_if<VpnStateChangeEvent>(&evt)) {
                on_vpn_state_change(*state_evt);
            }
        });
    }

    void stop_all() {
        if (iface_sub_) {
            bus_.unsubscribe(iface_sub_);
            iface_sub_ = 0;
        }
        if (state_sub_) {
            bus_.unsubscribe(state_sub_);
            state_sub_ = 0;
        }

        std::unique_lock lock(mutex_);
        std::vector<std::string> keys;
        for (const auto& [key, _] : extenders_) {
            keys.push_back(key);
        }
        lock.unlock();

        for (const auto& key : keys) {
            teardown_extender(key);
        }
    }

    void on_vpn_interface_event(const VpnInterfaceEvent& evt) {
        switch (evt.action) {
            case VpnInterfaceAction::AddressAdded:
                if (auto_enabled_ && !evt.ip_address.empty() && !evt.ipv6) {
                    spawn_extender(evt.connection_id, evt.interface_name, evt.ip_address);
                }
                break;

            case VpnInterfaceAction::AddressRemoved:
                if (!evt.ipv6) {
                    teardown_extender(evt.connection_id);
                }
                break;

            case VpnInterfaceAction::InterfaceDeleted:
            case VpnInterfaceAction::InterfaceDown:
                teardown_extender(evt.connection_id);
                break;

            case VpnInterfaceAction::InterfaceCreated:
            case VpnInterfaceAction::InterfaceUp:
                break;
        }
    }

    void on_vpn_state_change(const VpnStateChangeEvent& evt) {
        if (evt.new_state == VpnState::Disconnected ||
            evt.new_state == VpnState::Error) {
            teardown_extender(evt.connection_id);
        }
    }

    struct ExtenderInfo {
        std::string extender_id;
        std::string vpn_connection_id;
        std::string vpn_interface;
        std::string target_ip;
        uint16_t target_port{14580};
        bool active{false};
        uint64_t messages_in{0};
        uint64_t messages_out{0};
    };

    std::vector<ExtenderInfo> list_extenders() const {
        std::unique_lock lock(mutex_);
        std::vector<ExtenderInfo> result;
        for (const auto& [key, entry] : extenders_) {
            ExtenderInfo info = entry.info;

            if (entry.router) {
                auto endpoints = entry.router->get_endpoints();
                for (const auto& ep : endpoints) {
                    info.messages_in += ep.messages_in;
                    info.messages_out += ep.messages_out;
                }
            }

            result.push_back(std::move(info));
        }
        return result;
    }

    bool stop_extender(const std::string& conn_id) {
        teardown_extender(conn_id);
        return true;
    }

    bool start_extender(const std::string& conn_id, const std::string& iface, const std::string& ip) {
        spawn_extender(conn_id, iface, ip);
        return true;
    }

    bool remove_extender(const std::string& conn_id) {
        teardown_extender(conn_id);
        return true;
    }

    void set_auto_enabled(bool enabled) {
        auto_enabled_ = enabled;
    }

    bool is_auto_enabled() const { return auto_enabled_; }

    void set_udp_port(uint16_t port) {
        udp_port_ = port;
    }

    uint16_t get_udp_port() const { return udp_port_; }

private:
    struct ExtenderEntry {
        std::shared_ptr<MavRouter> router;
        std::unique_ptr<orch::CancellationSource> cancel;
        ExtenderInfo info;
    };

    void spawn_extender(const std::string& conn_id, const std::string& iface, const std::string& ip) {
        std::unique_lock lock(mutex_);

        auto it = extenders_.find(conn_id);
        if (it != extenders_.end() && it->second.info.active) {
            return;
        }

        if (it != extenders_.end()) {
            extenders_.erase(it);
        }

        std::string extender_id = "ext-" + conn_id;

        MavlinkConfig ext_cfg = build_extender_config(iface, ip);

        auto router = std::make_shared<MavRouter>(ext_cfg, bus_, extender_id);

        ExtenderInfo info;
        info.extender_id = extender_id;
        info.vpn_connection_id = conn_id;
        info.vpn_interface = iface;
        info.target_ip = ip;
        info.target_port = udp_port_;
        info.active = true;

        auto cancel = std::make_unique<orch::CancellationSource>();
        auto token = cancel->token();

        auto router_ptr = router;

        ExtenderEntry entry;
        entry.router = std::move(router);
        entry.cancel = std::move(cancel);
        entry.info = info;

        extenders_[conn_id] = std::move(entry);

        orchestrator_.submit_simple([router_ptr, token]() mutable {
            if (router_ptr) {
                router_ptr->run(token);
            }
        });

        lock.unlock();

        MavExtenderStatusEvent status_evt;
        status_evt.extender_id = info.extender_id;
        status_evt.vpn_connection_id = info.vpn_connection_id;
        status_evt.vpn_interface_name = info.vpn_interface;
        status_evt.target_ip = info.target_ip;
        status_evt.target_port = info.target_port;
        status_evt.connected = true;
        bus_.publish(Event{std::move(status_evt)});
    }

    void teardown_extender(const std::string& conn_id) {
        std::unique_lock lock(mutex_);

        auto it = extenders_.find(conn_id);
        if (it == extenders_.end()) {
            return;
        }

        std::string extender_id = it->second.info.extender_id;
        std::string vpn_iface = it->second.info.vpn_interface;
        std::string target_ip = it->second.info.target_ip;
        uint16_t target_port = it->second.info.target_port;

        if (it->second.cancel) {
            it->second.cancel->cancel();
        }

        extenders_.erase(it);

        lock.unlock();

        MavExtenderStatusEvent status_evt;
        status_evt.extender_id = extender_id;
        status_evt.vpn_connection_id = conn_id;
        status_evt.vpn_interface_name = vpn_iface;
        status_evt.target_ip = target_ip;
        status_evt.target_port = target_port;
        status_evt.connected = false;
        bus_.publish(Event{std::move(status_evt)});
    }

    MavlinkConfig build_extender_config(const std::string& iface, const std::string& target_ip) const {
        MavlinkConfig ext_cfg;
        ext_cfg.enabled = true;
        ext_cfg.tcp_server_port = 0;
        ext_cfg.dedup_period_sec = base_config_.dedup_period_sec;
        ext_cfg.sniffer_sysid = base_config_.sniffer_sysid;
        ext_cfg.report_stats = base_config_.report_stats;

        MavEndpointConfig upstream;
        upstream.type = "udp";
        upstream.name = "upstream_" + iface;
        upstream.address = target_ip;
        upstream.port = static_cast<int>(udp_port_);
        upstream.mode = "client";
        upstream.retry_timeout_sec = 5;
        ext_cfg.endpoints.push_back(std::move(upstream));

        for (const auto& ep : base_config_.endpoints) {
            if (ep.type == "uart") {
                ext_cfg.endpoints.push_back(ep);
            }
        }

        return ext_cfg;
    }

    EventBus& bus_;
    orch::ThreadOrchestrator& orchestrator_;
    MavlinkConfig base_config_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, ExtenderEntry> extenders_;

    EventBus::SubscriptionId iface_sub_{0};
    EventBus::SubscriptionId state_sub_{0};

    bool auto_enabled_{true};
    uint16_t udp_port_{14580};
};

}
