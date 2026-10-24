#pragma once

#include "vpn/vpn_config.hpp"
#include "vpn/vpn_connection.hpp"
#include "vpn/wireguard_connection.hpp"
#include "vpn/openvpn_connection.hpp"
#include "core/event_bus.hpp"
#include "core/vpn_monitor.hpp"

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace odc {

struct VpnConnectionInfo {
    std::string id;
    VpnType type;
    VpnState state;
    VpnConnectionStats stats;
    VpnHealthStatus health;
    std::string config_path;
    bool enabled;
    std::string name;
};

class VpnManager {
public:
    explicit VpnManager(EventBus& bus, const std::filesystem::path& config_path = "/etc/odc-onboard/config.json",
                        VpnInterfaceMonitor* monitor = nullptr)
        : event_bus_(bus), config_path_(config_path), monitor_(monitor) {}

    bool load_config() {
        auto cfg = load_app_config(config_path_);
        if (!cfg) {
            return false;
        }

        app_config_ = *cfg;

        auto collisions = detect_collisions(*app_config_);
        if (!collisions.empty()) {
            return false;
        }

        std::unique_lock lock(mutex_);

        std::unordered_map<std::string, ConnectionEntry> new_connections;

        for (const auto& wg : app_config_->vpn.wireguard_connections) {
            if (wg.enabled) {
                auto it = connections_.find(wg.id);
                if (it != connections_.end()) {
                    new_connections[wg.id] = std::move(it->second);
                } else {
                    auto conn = std::make_unique<WireGuardConnection>(wg);
                    conn->set_event_bus(&event_bus_);
                    new_connections[wg.id] = {
                        .connection = std::move(conn),
                        .type = VpnType::WireGuard,
                        .enabled = wg.enabled
                    };
                    if (monitor_) {
                        monitor_->track_connection(wg.id, "wg-" + wg.id);
                    }
                }
            }
        }

        for (const auto& ovpn : app_config_->vpn.openvpn_connections) {
            if (ovpn.enabled) {
                auto it = connections_.find(ovpn.id);
                if (it != connections_.end()) {
                    new_connections[ovpn.id] = std::move(it->second);
                } else {
                    auto conn = std::make_unique<OpenVpnConnection>(ovpn);
                    conn->set_event_bus(&event_bus_);
                    new_connections[ovpn.id] = {
                        .connection = std::move(conn),
                        .type = VpnType::OpenVPN,
                        .enabled = ovpn.enabled
                    };
                    if (monitor_) {
                        monitor_->track_connection(ovpn.id, "ovpn-" + ovpn.id);
                    }
                }
            }
        }

        for (auto& [id, entry] : connections_) {
            if (new_connections.find(id) == new_connections.end()) {
                entry.connection->stop();
            }
        }

        connections_ = std::move(new_connections);

        return true;
    }

    bool save_config() {
        if (!app_config_) {
            return false;
        }
        return save_app_config(*app_config_, config_path_);
    }

    bool start_connection(const std::string& id) {
        std::shared_lock lock(mutex_);
        auto it = connections_.find(id);
        if (it == connections_.end()) {
            return false;
        }
        return it->second.connection->start();
    }

    bool stop_connection(const std::string& id) {
        std::shared_lock lock(mutex_);
        auto it = connections_.find(id);
        if (it == connections_.end()) {
            return false;
        }
        return it->second.connection->stop();
    }

    bool restart_connection(const std::string& id) {
        std::shared_lock lock(mutex_);
        auto it = connections_.find(id);
        if (it == connections_.end()) {
            return false;
        }
        return it->second.connection->restart();
    }

    bool remove_connection(const std::string& id) {
        std::unique_lock lock(mutex_);
        auto it = connections_.find(id);
        if (it == connections_.end()) {
            return false;
        }

        it->second.connection->stop();

        if (monitor_) {
            monitor_->untrack_connection(id);
        }

        connections_.erase(it);

        if (app_config_) {
            auto& wg = app_config_->vpn.wireguard_connections;
            wg.erase(std::remove_if(wg.begin(), wg.end(),
                [&id](const auto& c) { return c.id == id; }), wg.end());

            auto& ovpn = app_config_->vpn.openvpn_connections;
            ovpn.erase(std::remove_if(ovpn.begin(), ovpn.end(),
                [&id](const auto& c) { return c.id == id; }), ovpn.end());

            save_config();
        }

        return true;
    }

    bool add_wireguard(const WireGuardConnectionConfig& cfg) {
        std::unique_lock lock(mutex_);

        if (connections_.count(cfg.id)) {
            return false;
        }
        for (const auto& [id, _] : connections_) {
            if (id == cfg.id) return false;
        }

        auto conn = std::make_unique<WireGuardConnection>(cfg);
        conn->set_event_bus(&event_bus_);
        connections_[cfg.id] = {
            .connection = std::move(conn),
            .type = VpnType::WireGuard,
            .enabled = cfg.enabled
        };

        if (monitor_) {
            lock.unlock();
            monitor_->track_connection(cfg.id, "wg-" + cfg.id);
        }

        if (app_config_) {
            app_config_->vpn.wireguard_connections.push_back(cfg);
            save_config();
        }

        return true;
    }

    bool add_openvpn(const OpenVPNConnectionConfig& cfg) {
        std::unique_lock lock(mutex_);

        if (connections_.count(cfg.id)) {
            return false;
        }

        auto conn = std::make_unique<OpenVpnConnection>(cfg);
        conn->set_event_bus(&event_bus_);
        connections_[cfg.id] = {
            .connection = std::move(conn),
            .type = VpnType::OpenVPN,
            .enabled = cfg.enabled
        };

        if (monitor_) {
            lock.unlock();
            monitor_->track_connection(cfg.id, "ovpn-" + cfg.id);
        }

        if (app_config_) {
            app_config_->vpn.openvpn_connections.push_back(cfg);
            save_config();
        }

        return true;
    }

    std::vector<VpnConnectionInfo> list_connections() const {
        std::shared_lock lock(mutex_);
        std::vector<VpnConnectionInfo> result;

        for (const auto& [id, entry] : connections_) {
            VpnConnectionInfo info{
                .id = id,
                .type = entry.type,
                .state = entry.connection->state(),
                .stats = entry.connection->stats(),
                .health = entry.connection->health(),
                .enabled = entry.enabled
            };

            if (app_config_) {
                for (const auto& wg : app_config_->vpn.wireguard_connections) {
                    if (wg.id == id) {
                        info.config_path = wg.config_path.string();
                        info.name = wg.name;
                        break;
                    }
                }
                for (const auto& ovpn : app_config_->vpn.openvpn_connections) {
                    if (ovpn.id == id) {
                        info.config_path = ovpn.config_path.string();
                        info.name = ovpn.name;
                        break;
                    }
                }
            }

            result.push_back(std::move(info));
        }

        return result;
    }

    std::optional<VpnConnectionInfo> get_connection(const std::string& id) const {
        std::shared_lock lock(mutex_);
        auto it = connections_.find(id);
        if (it == connections_.end()) {
            return std::nullopt;
        }

        VpnConnectionInfo info{
            .id = id,
            .type = it->second.type,
            .state = it->second.connection->state(),
            .stats = it->second.connection->stats(),
            .health = it->second.connection->health(),
            .enabled = it->second.enabled
        };

        return info;
    }

    void stop_all() {
        std::shared_lock lock(mutex_);
        for (auto& [id, entry] : connections_) {
            entry.connection->stop();
        }
    }

    const AppConfig* config() const { return app_config_ ? &*app_config_ : nullptr; }

    void update_interface_name(const std::string& conn_id, const std::string& iface_name) {
        if (monitor_) {
            monitor_->track_connection(conn_id, iface_name);
        }
    }

    static std::vector<std::string> validate_config_collisions(const AppConfig& cfg) {
        return detect_collisions(cfg);
    }

private:
    static std::vector<std::string> detect_collisions(const AppConfig& cfg) {
        std::vector<std::string> collisions;
        std::unordered_set<std::string> ids;
        std::unordered_set<std::string> config_paths;

        for (const auto& wg : cfg.vpn.wireguard_connections) {
            if (!wg.enabled) continue;
            if (!ids.insert(wg.id).second) {
                collisions.push_back("Duplicate WireGuard connection ID: " + wg.id);
            }
            std::string path_str = wg.config_path.string();
            if (!config_paths.insert(path_str).second) {
                collisions.push_back("Duplicate config file path: " + path_str);
            }
        }

        for (const auto& ovpn : cfg.vpn.openvpn_connections) {
            if (!ovpn.enabled) continue;
            if (!ids.insert(ovpn.id).second) {
                collisions.push_back("Duplicate OpenVPN connection ID: " + ovpn.id);
            }
            std::string path_str = ovpn.config_path.string();
            if (!config_paths.insert(path_str).second) {
                collisions.push_back("Duplicate config file path: " + path_str);
            }
        }

        return collisions;
    }

    struct ConnectionEntry {
        std::unique_ptr<VpnConnection> connection;
        VpnType type;
        bool enabled;
    };

    EventBus& event_bus_;
    std::filesystem::path config_path_;
    VpnInterfaceMonitor* monitor_{nullptr};
    std::optional<AppConfig> app_config_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ConnectionEntry> connections_;
};

}
