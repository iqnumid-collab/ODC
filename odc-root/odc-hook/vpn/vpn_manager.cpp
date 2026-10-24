#include "vpn_manager.hpp"
#include "wireguard_client.hpp"
#include "openvpn_client.hpp"

#include <spdlog/spdlog.h>
#include <sodium.h>

namespace odc::hook {

VpnClientManager::VpnClientManager(asio::io_context& io)
    : io_(io)
{}

VpnClientManager::~VpnClientManager() {
    for (auto& [id, entry] : clients_) {
        if (entry.wg) entry.wg->stop();
        if (entry.ovpn) entry.ovpn->stop();
    }
}

void VpnClientManager::set_event_sender(EventSender sender) {
    send_event_ = std::move(sender);
}

void VpnClientManager::publish(const nlohmann::json& msg) {
    if (send_event_) send_event_(msg);
}

wg::WgDeviceConfig VpnClientManager::parse_wireguard_config(const nlohmann::json& cfg) {
    wg::WgDeviceConfig wcfg;

    std::string b64_key = cfg.value("private_key", "");
    if (!b64_key.empty()) {
        size_t bin_len = 0;
        if (sodium_base642bin(wcfg.private_key.data(), wg::WG_KEY_LEN,
                              b64_key.c_str(), b64_key.size(),
                              nullptr, &bin_len, nullptr,
                              sodium_base64_VARIANT_ORIGINAL) != 0) {
            SPDLOG_WARN("VpnManager: failed to decode WG private key");
        }
    }

    wcfg.listen_port = cfg.value("listen_port", 0);

    for (const auto& p : cfg.value("peers", nlohmann::json::array())) {
        wg::WgPeerConfig pc;
        std::string pub_b64 = p.value("public_key", "");
        if (!pub_b64.empty()) {
            sodium_base642bin(pc.public_key.data(), wg::WG_KEY_LEN,
                              pub_b64.c_str(), pub_b64.size(),
                              nullptr, nullptr, nullptr,
                              sodium_base64_VARIANT_ORIGINAL);
        }
        std::string psk_b64 = p.value("preshared_key", "");
        if (!psk_b64.empty()) {
            sodium_base642bin(pc.preshared_key.data(), wg::WG_KEY_LEN,
                              psk_b64.c_str(), psk_b64.size(),
                              nullptr, nullptr, nullptr,
                              sodium_base64_VARIANT_ORIGINAL);
        }
        pc.endpoint = p.value("endpoint", "");
        pc.persistent_keepalive_interval = p.value("persistent_keepalive_interval", 25);

        for (const auto& ip : p.value("allowed_ips", nlohmann::json::array())) {
            pc.allowed_ips.push_back(ip.get<std::string>());
        }
        wcfg.peers.push_back(std::move(pc));
    }

    auto tun = cfg.value("tun", nlohmann::json::object());
    wcfg.tun_name = tun.value("name", "odc-wg");
    wcfg.tun_address = tun.value("address", "10.0.0.2");
    wcfg.tun_prefix_len = tun.value("prefix_len", 24);
    wcfg.tun_mtu = tun.value("mtu", 1420);

    return wcfg;
}

OpenVPNConfig VpnClientManager::parse_openvpn_config(const nlohmann::json& cfg) {
    OpenVPNConfig ocfg;
    ocfg.config_path = cfg.value("config_path", "");
    ocfg.credentials_path = cfg.value("credentials_path", "");
    ocfg.interface_name = cfg.value("interface_name", "odc-ovpn");
    ocfg.max_retries = cfg.value("max_retries", 5);
    ocfg.connect_timeout_sec = cfg.value("connect_timeout_sec", 30);
    ocfg.route_safety_block_default = cfg.value("route_safety_block_default", true);
    return ocfg;
}

void VpnClientManager::connect(const std::string& conn_id, const nlohmann::json& msg) {
    std::lock_guard lock(mutex_);

    auto& entry = clients_[conn_id];
    entry.id = conn_id;
    entry.vpn_type = msg.value("vpn_type", "wireguard");
    entry.state = VpnState::Idle;

    if (entry.vpn_type == "wireguard") {
        auto wcfg = parse_wireguard_config(msg.value("config", nlohmann::json::object()));
        entry.wg = std::make_shared<WireGuardClient>();

        entry.wg->on_state_change([this, conn_id](VpnState s, std::string err) {
            std::lock_guard lk(mutex_);
            auto it = clients_.find(conn_id);
            if (it != clients_.end()) it->second.state = s;
            publish({
                {"type", "vpn.state_change"},
                {"connection_id", conn_id},
                {"vpn_type", "wireguard"},
                {"old_state", to_string(it != clients_.end() ? it->second.state : VpnState::Idle)},
                {"new_state", to_string(s)},
                {"error", err},
                {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
            });
        });

        entry.wg->on_stats([this, conn_id](VpnStats st) {
            publish({
                {"type", "vpn.stats"},
                {"connection_id", conn_id},
                {"bytes_in", st.bytes_in},
                {"bytes_out", st.bytes_out},
                {"packets_in", st.packets_in},
                {"packets_out", st.packets_out},
                {"duration_sec", st.duration_sec}
            });
        });

        entry.wg->start(wcfg, io_);
        entry.state = entry.wg->state();

    } else if (entry.vpn_type == "openvpn") {
        auto ocfg = parse_openvpn_config(msg.value("config", nlohmann::json::object()));
        entry.ovpn = std::make_shared<OpenVPNClient>();

        entry.ovpn->on_state_change([this, conn_id](VpnState s, std::string err) {
            std::lock_guard lk(mutex_);
            auto it = clients_.find(conn_id);
            if (it != clients_.end()) it->second.state = s;
            publish({
                {"type", "vpn.state_change"},
                {"connection_id", conn_id},
                {"vpn_type", "openvpn"},
                {"old_state", "idle"},
                {"new_state", to_string(s)},
                {"error", err},
                {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
            });
        });

        entry.ovpn->on_stats([this, conn_id](VpnStats st) {
            publish({
                {"type", "vpn.stats"},
                {"connection_id", conn_id},
                {"bytes_in", st.bytes_in},
                {"bytes_out", st.bytes_out},
                {"packets_in", st.packets_in},
                {"packets_out", st.packets_out},
                {"duration_sec", st.duration_sec}
            });
        });

        entry.ovpn->start(ocfg, io_);
        entry.state = entry.ovpn->state();
    }

    SPDLOG_INFO("VpnManager: connect {} type={} state={}", conn_id, entry.vpn_type,
                to_string(entry.state));
}

void VpnClientManager::disconnect(const std::string& conn_id) {
    std::lock_guard lock(mutex_);
    auto it = clients_.find(conn_id);
    if (it == clients_.end()) return;

    if (it->second.wg) it->second.wg->stop();
    if (it->second.ovpn) it->second.ovpn->stop();

    publish({
        {"type", "vpn.state_change"},
        {"connection_id", conn_id},
        {"vpn_type", it->second.vpn_type},
        {"new_state", "disconnected"}
    });

    clients_.erase(it);
    SPDLOG_INFO("VpnManager: disconnected {}", conn_id);
}

nlohmann::json VpnClientManager::get_status(const std::string& conn_id) const {
    std::lock_guard lock(mutex_);
    auto it = clients_.find(conn_id);
    if (it == clients_.end()) {
        return {{"connection_id", conn_id}, {"exists", false}};
    }

    const auto& e = it->second;
    return {
        {"connection_id", conn_id},
        {"exists", true},
        {"vpn_type", e.vpn_type},
        {"state", to_string(e.state)},
        {"stats", {
            {"bytes_in", e.stats.bytes_in},
            {"bytes_out", e.stats.bytes_out},
            {"reconnect_count", e.stats.reconnect_count}
        }}
    };
}

} // namespace odc::hook
