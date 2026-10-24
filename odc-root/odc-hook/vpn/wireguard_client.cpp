#include "wireguard_client.hpp"

#if defined(__linux__)
#include "../../thirdparty/wg_netlink.hpp"
#undef WG_KEY_LEN  // wg_netlink.hpp #define conflicts with our constexpr
#else
#include "tun_device.hpp"
#include "wg_device.hpp"
#endif

#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>
#if defined(__linux__)
#include <arpa/inet.h>
#include <netdb.h>
#endif

namespace odc::hook {

#if defined(__linux__)
// ── Linux: kernel WireGuard via WgNetlink ────────────────────────────────
struct WireGuardClient::WgNetlinkAdapter {
    std::unique_ptr<odc::WgNetlink> wg;
};

WireGuardClient::WireGuardClient() : netlink_(std::make_unique<WgNetlinkAdapter>()) {}
WireGuardClient::~WireGuardClient() { stop(); }

// Resolve endpoint string to sockaddr_storage
static bool resolve_endpoint(const std::string& ep_str, struct sockaddr_storage& out) {
    auto colon = ep_str.find(':');
    if (colon == std::string::npos) return false;
    std::string host = ep_str.substr(0, colon);
    std::string port = ep_str.substr(colon + 1);

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0) return false;
    if (!res) return false;
    std::memcpy(&out, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    return true;
}

bool WireGuardClient::start(const wg::WgDeviceConfig& cfg, asio::io_context& io) {
    io_ = &io;
    cfg_ = cfg;
    running_ = true;

    try {
        set_state(VpnState::Connecting);

        netlink_->wg = std::make_unique<odc::WgNetlink>();

        SPDLOG_INFO("WireGuardClient: creating kernel WG interface {}", cfg.tun_name);
        netlink_->wg->delete_interface(cfg.tun_name);
        if (!netlink_->wg->create_interface(cfg.tun_name)) {
            throw std::runtime_error("failed to create WG interface");
        }

        // Build peer list
        std::vector<odc::WgPeer> nl_peers;
        for (const auto& p : cfg.peers) {
            odc::WgPeer np{};
            std::memcpy(np.public_key, p.public_key.data(), wg::WG_KEY_LEN);
            if (!p.preshared_key.empty()) {
                std::memcpy(np.preshared_key, p.preshared_key.data(), wg::WG_KEY_LEN);
                np.has_preshared_key = true;
            }
            if (!p.endpoint.empty()) {
                resolve_endpoint(p.endpoint, np.endpoint);
            }
            if (p.persistent_keepalive_interval > 0) {
                np.has_keepalive = true;
                np.persistent_keepalive_interval = p.persistent_keepalive_interval;
            }
            for (const auto& aip : p.allowed_ips) {
                // Parse CIDR
                auto slash = aip.find('/');
                std::string ip_str = (slash != std::string::npos) ? aip.substr(0, slash) : aip;
                int prefix = (slash != std::string::npos) ? std::stoi(aip.substr(slash + 1)) : 32;

                odc::WgAllowedIP naip{};
                if (ip_str.find(':') != std::string::npos) {
                    naip.family = AF_INET6;
                    inet_pton(AF_INET6, ip_str.c_str(), &naip.ip6);
                    naip.cidr = prefix;
                } else {
                    naip.family = AF_INET;
                    inet_pton(AF_INET, ip_str.c_str(), &naip.ip4);
                    naip.cidr = prefix;
                }
                np.allowed_ips.push_back(naip);
            }
            nl_peers.push_back(np);
        }

        // Set device config
        netlink_->wg->set_device(cfg.tun_name, cfg.private_key.data(),
                                 cfg.listen_port, nl_peers);
        netlink_->wg->add_address(cfg.tun_name, cfg.tun_address, cfg.tun_prefix_len);
        netlink_->wg->bring_interface_up(cfg.tun_name);

        set_state(VpnState::Connected);
        SPDLOG_INFO("WireGuardClient: WG interface {} up", cfg.tun_name);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("WireGuardClient: start failed: {}", e.what());
        set_state(VpnState::Error, e.what());
        return false;
    }

    stats_thread_ = std::thread(&WireGuardClient::stats_poll_loop, this);
    return true;
}

void WireGuardClient::stop() {
    if (!running_.exchange(false)) return;
    set_state(VpnState::Disconnecting);
    try {
        if (netlink_ && netlink_->wg) {
            netlink_->wg->delete_interface(cfg_.tun_name);
        }
    } catch (...) {}
    if (stats_thread_.joinable()) stats_thread_.join();
    set_state(VpnState::Disconnected);
}

#else
// ── Windows/macOS: userspace WgDevice ─────────────────────────────────────
WireGuardClient::WireGuardClient() {}
WireGuardClient::~WireGuardClient() { stop(); }

bool WireGuardClient::start(const wg::WgDeviceConfig& cfg, asio::io_context& io) {
    io_ = &io;
    cfg_ = cfg;
    running_ = true;

    try {
        set_state(VpnState::Connecting);

        TunConfig tcfg;
        tcfg.name = cfg.tun_name;
        tcfg.ip_address = cfg.tun_address;
        tcfg.prefix_len = cfg.tun_prefix_len;
        tcfg.mtu = cfg.tun_mtu;

        tun_ = std::make_unique<TunDevice>();
        if (!tun_->open(tcfg)) {
            throw std::runtime_error("failed to create TUN device");
        }
        tun_->set_ip();
        tun_->set_mtu(cfg.tun_mtu);
        tun_->bring_up();

        device_ = std::make_unique<wg::WgDevice>(io, std::move(*tun_));
        if (!device_->start(cfg)) {
            throw std::runtime_error("failed to start WgDevice");
        }

        set_state(VpnState::Connected);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("WireGuardClient: start failed: {}", e.what());
        set_state(VpnState::Error, e.what());
        return false;
    }

    stats_thread_ = std::thread(&WireGuardClient::stats_poll_loop, this);
    return true;
}

void WireGuardClient::stop() {
    if (!running_.exchange(false)) return;
    set_state(VpnState::Disconnecting);
    if (device_) device_->stop();
    if (tun_) tun_->close();
    if (stats_thread_.joinable()) stats_thread_.join();
    set_state(VpnState::Disconnected);
}
#endif

VpnStats WireGuardClient::stats() const {
    VpnStats s;
    s.bytes_in = stats_.bytes_in;
    s.bytes_out = stats_.bytes_out;
    s.packets_in = stats_.packets_in;
    s.packets_out = stats_.packets_out;
    s.reconnect_count = stats_.reconnect_count;
    return s;
}

void WireGuardClient::set_state(VpnState s, const std::string& error) {
    state_ = s;
    if (state_cb_) state_cb_(s, error);
}

void WireGuardClient::stats_poll_loop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (stats_cb_) {
            VpnStats s;
            s.bytes_in = stats_.bytes_in;
            s.bytes_out = stats_.bytes_out;
            s.reconnect_count = stats_.reconnect_count;
            stats_cb_(s);
        }
    }
}

} // namespace odc::hook
