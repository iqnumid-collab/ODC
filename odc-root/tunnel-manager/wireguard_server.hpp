#pragma once

#include "wg_netlink.hpp"

#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace odc {

struct WgServerConfig {
    std::string interface_name;
    uint16_t listen_port{51820};
    std::string private_key;
    std::string subnet{"10.0.0.0/24"};
    int mtu{1420};
    uint16_t keepalive_sec{25};
    int poll_interval_sec{5};
};

struct WgPeerInfo {
    std::string public_key;
    std::string allowed_ip;
    std::string endpoint_addr;
    int64_t last_handshake_sec{0};
    uint64_t bytes_rx{0};
    uint64_t bytes_tx{0};
};

class WireGuardServer {
public:
    explicit WireGuardServer(const WgServerConfig& config);
    ~WireGuardServer();

    WireGuardServer(const WireGuardServer&) = delete;
    WireGuardServer& operator=(const WireGuardServer&) = delete;

    bool start();
    void stop();
    bool is_running() const { return running_; }

    bool add_peer(const std::string& public_key, const std::string& allowed_ip);
    bool remove_peer(const std::string& public_key);
    WgPeerInfo get_peer_info(const std::string& public_key) const;
    std::vector<WgPeerInfo> list_peers();
    std::string allocate_ip();
    void release_ip(const std::string& ip);
    bool is_ip_available(const std::string& ip) const;

    using PeerCallback = std::function<void(const WgPeerInfo& peer)>;
    void set_on_peer_connected(PeerCallback cb) { on_peer_connected_ = std::move(cb); }
    void set_on_peer_disconnected(PeerCallback cb) { on_peer_disconnected_ = std::move(cb); }

    void poll_peers();

private:
    std::vector<std::string> build_ip_pool() const;
    static uint32_t ip_to_uint(const std::string& ip);
    static std::string uint_to_ip(uint32_t addr);

    WgServerConfig config_;
    std::unique_ptr<WgNetlink> netlink_;
    bool running_{false};

    std::unordered_set<std::string> allocated_ips_;
    std::unordered_set<std::string> known_peer_keys_;
    std::unordered_map<std::string, bool> peer_was_connected_;  // key → was connected
    // Authoritative peer set for REPLACE_PEERS merges (kernel GET can lag/miss).
    std::unordered_map<std::string, std::string> configured_peers_; // pubkey → allowed_ip

    PeerCallback on_peer_connected_;
    PeerCallback on_peer_disconnected_;
    mutable std::mutex mutex_;
};

} // namespace odc
