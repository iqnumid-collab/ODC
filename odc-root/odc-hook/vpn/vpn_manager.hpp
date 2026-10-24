#pragma once

#include "vpn_types.hpp"
#include <nlohmann/json.hpp>
#include <asio.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace odc::hook {

class WireGuardClient;
class OpenVPNClient;

class VpnClientManager {
public:
    using EventSender = std::function<void(const nlohmann::json&)>;

    explicit VpnClientManager(asio::io_context& io);
    ~VpnClientManager();

    void set_event_sender(EventSender sender);

    void connect(const std::string& conn_id, const nlohmann::json& msg);
    void disconnect(const std::string& conn_id);
    nlohmann::json get_status(const std::string& conn_id) const;

private:
    asio::io_context& io_;
    EventSender send_event_;
    mutable std::mutex mutex_;

    struct Entry {
        std::string id;
        std::string vpn_type;
        VpnState state{VpnState::Idle};
        std::shared_ptr<WireGuardClient> wg;
        std::shared_ptr<OpenVPNClient> ovpn;
        VpnStats stats;
    };
    std::unordered_map<std::string, Entry> clients_;

    void publish(const nlohmann::json& msg);
    wg::WgDeviceConfig parse_wireguard_config(const nlohmann::json& cfg);
    OpenVPNConfig parse_openvpn_config(const nlohmann::json& cfg);
};

} // namespace odc::hook
