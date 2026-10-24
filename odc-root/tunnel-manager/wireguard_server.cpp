#include "wireguard_server.hpp"
#include "odc/logging.hpp"

#include <cmath>
#include <sstream>
#include <thread>

namespace odc {

namespace {

std::string sockaddr_to_endpoint(const struct sockaddr_storage& ss) {
    if (ss.ss_family == AF_INET) {
        const auto* sin = reinterpret_cast<const struct sockaddr_in*>(&ss);
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
        return std::string(ip) + ":" + std::to_string(ntohs(sin->sin_port));
    }
    if (ss.ss_family == AF_INET6) {
        const auto* sin6 = reinterpret_cast<const struct sockaddr_in6*>(&ss);
        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
        return "[" + std::string(ip) + "]:" + std::to_string(ntohs(sin6->sin6_port));
    }
    return "";
}

} // namespace

WireGuardServer::WireGuardServer(const WgServerConfig& config)
    : config_(config) {
}

WireGuardServer::~WireGuardServer() {
    stop();
}

bool WireGuardServer::start() {
    std::lock_guard lock(mutex_);
    if (running_) return true;

    auto logger = get_logger("wg_server");

    try {
        netlink_ = std::make_unique<WgNetlink>();
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to initialize netlink: {}", e.what());
        return false;
    }

    if (!netlink_->create_interface(config_.interface_name)) {
        SPDLOG_LOGGER_WARN(logger, "WireGuard interface '{}' exists — recreating",
                            config_.interface_name);
        netlink_->delete_interface(config_.interface_name);
        if (!netlink_->create_interface(config_.interface_name)) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to create WireGuard interface '{}'",
                                config_.interface_name);
            netlink_.reset();
            return false;
        }
    }

    std::string decoded_key;
    {
        static const std::string base64_chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        decoded_key.reserve(config_.private_key.size());
        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; ++i) T[static_cast<unsigned char>(base64_chars[i])] = i;

        int val = 0, valb = -8;
        for (unsigned char c : config_.private_key) {
            if (c == '=') break;
            if (T[c] == -1) continue;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                decoded_key.push_back(static_cast<char>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
    }

    uint8_t private_key_buf[WG_KEY_LEN] = {};
    size_t key_len = std::min(decoded_key.size(), static_cast<size_t>(WG_KEY_LEN));
    std::memcpy(private_key_buf, decoded_key.data(), key_len);

    std::vector<WgPeer> peers;
    if (!netlink_->set_device(config_.interface_name, private_key_buf,
                              config_.listen_port, peers)) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to set WireGuard device '{}'",
                            config_.interface_name);
        netlink_->delete_interface(config_.interface_name);
        netlink_.reset();
        return false;
    }

    if (!netlink_->bring_interface_up(config_.interface_name)) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to bring up interface '{}'",
                            config_.interface_name);
        netlink_->delete_interface(config_.interface_name);
        netlink_.reset();
        return false;
    }

    std::string server_ip = allocate_ip();
    size_t slash = config_.subnet.find('/');
    int prefix_len = 24;
    if (slash != std::string::npos) {
        prefix_len = std::stoi(config_.subnet.substr(slash + 1));
    }

    if (!netlink_->add_address(config_.interface_name, server_ip, prefix_len, false)) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to assign address {} to {}",
                            server_ip, config_.interface_name);
        netlink_->delete_interface(config_.interface_name);
        netlink_.reset();
        allocated_ips_.erase(server_ip);
        return false;
    }

    allocated_ips_.insert(server_ip);

    running_ = true;

    SPDLOG_LOGGER_INFO(logger, "WireGuard server '{}' started on port {}, subnet {}, server IP {}",
                       config_.interface_name, config_.listen_port, config_.subnet, server_ip);
    return true;
}

void WireGuardServer::stop() {
    std::lock_guard lock(mutex_);
    if (!running_) return;

    running_ = false;
    auto logger = get_logger("wg_server");

    if (netlink_) {
        netlink_->delete_interface(config_.interface_name);
        netlink_.reset();
    }

    allocated_ips_.clear();
    known_peer_keys_.clear();
    peer_was_connected_.clear();
    configured_peers_.clear();

    SPDLOG_LOGGER_INFO(logger, "WireGuard server '{}' stopped", config_.interface_name);
}

bool WireGuardServer::add_peer(const std::string& public_key, const std::string& allowed_ip) {
    std::lock_guard lock(mutex_);
    if (!running_ || !netlink_) return false;

    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; ++i) T[static_cast<unsigned char>(base64_chars[i])] = i;

    std::string ip_only = allowed_ip;
    size_t slash = allowed_ip.find('/');
    if (slash != std::string::npos) {
        ip_only = allowed_ip.substr(0, slash);
    }
    {
        struct in_addr tmp{};
        if (inet_pton(AF_INET, ip_only.c_str(), &tmp) != 1) return false;
    }

    // set_device uses WGDEVICE_F_REPLACE_PEERS — resubmit full configured set.
    configured_peers_[public_key] = ip_only;
    std::vector<WgPeer> peers_to_set;
    for (const auto& [pk, aip_str] : configured_peers_) {
        WgPeer p;
        std::memset(p.public_key, 0, WG_KEY_LEN);
        std::string kd;
        int kval = 0, kvalb = -8;
        for (unsigned char c : pk) {
            if (c == '=') break;
            if (T[c] == -1) continue;
            kval = (kval << 6) + T[c];
            kvalb += 6;
            if (kvalb >= 0) {
                kd.push_back(static_cast<char>((kval >> kvalb) & 0xFF));
                kvalb -= 8;
            }
        }
        if (kd.size() != WG_KEY_LEN) {
            configured_peers_.erase(public_key);
            return false;
        }
        std::memcpy(p.public_key, kd.data(), WG_KEY_LEN);
        p.has_keepalive = config_.keepalive_sec > 0;
        p.persistent_keepalive_interval = config_.keepalive_sec;
        WgAllowedIP ea;
        ea.family = AF_INET;
        ea.cidr = 32;
        if (inet_pton(AF_INET, aip_str.c_str(), &ea.ip4) == 1) {
            p.allowed_ips.push_back(ea);
        }
        peers_to_set.push_back(p);
    }

    uint8_t private_key_buf[WG_KEY_LEN] = {};
    {
        std::string decoded_key;
        decoded_key.reserve(config_.private_key.size());
        int v = 0, vb = -8;
        for (unsigned char c : config_.private_key) {
            if (c == '=') break;
            if (T[c] == -1) continue;
            v = (v << 6) + T[c];
            vb += 6;
            if (vb >= 0) {
                decoded_key.push_back(static_cast<char>((v >> vb) & 0xFF));
                vb -= 8;
            }
        }
        std::memcpy(private_key_buf, decoded_key.data(),
                    std::min(decoded_key.size(), static_cast<size_t>(WG_KEY_LEN)));
    }

    if (!netlink_->set_device(config_.interface_name, private_key_buf,
                              config_.listen_port, peers_to_set)) {
        configured_peers_.erase(public_key);
        return false;
    }

    allocated_ips_.insert(ip_only);
    known_peer_keys_.insert(public_key);
    peer_was_connected_[public_key] = false;

    return true;
}

bool WireGuardServer::remove_peer(const std::string& public_key) {
    std::lock_guard lock(mutex_);
    if (!running_ || !netlink_) return false;

    auto it = configured_peers_.find(public_key);
    if (it != configured_peers_.end()) {
        allocated_ips_.erase(it->second);
        configured_peers_.erase(it);
    }

    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; ++i) T[static_cast<unsigned char>(base64_chars[i])] = i;

    std::vector<WgPeer> remaining_peers;
    for (const auto& [pk, aip_str] : configured_peers_) {
        WgPeer p;
        std::memset(p.public_key, 0, WG_KEY_LEN);
        std::string kd;
        int kval = 0, kvalb = -8;
        for (unsigned char c : pk) {
            if (c == '=') break;
            if (T[c] == -1) continue;
            kval = (kval << 6) + T[c];
            kvalb += 6;
            if (kvalb >= 0) {
                kd.push_back(static_cast<char>((kval >> kvalb) & 0xFF));
                kvalb -= 8;
            }
        }
        if (kd.size() != WG_KEY_LEN) continue;
        std::memcpy(p.public_key, kd.data(), WG_KEY_LEN);
        p.has_keepalive = config_.keepalive_sec > 0;
        p.persistent_keepalive_interval = config_.keepalive_sec;
        WgAllowedIP ea;
        ea.family = AF_INET;
        ea.cidr = 32;
        if (inet_pton(AF_INET, aip_str.c_str(), &ea.ip4) == 1) {
            p.allowed_ips.push_back(ea);
        }
        remaining_peers.push_back(p);
    }

    uint8_t private_key_buf[WG_KEY_LEN] = {};
    {
        std::string decoded_key;
        int v = 0, vb = -8;
        for (unsigned char c : config_.private_key) {
            if (c == '=') break;
            if (T[c] == -1) continue;
            v = (v << 6) + T[c];
            vb += 6;
            if (vb >= 0) {
                decoded_key.push_back(static_cast<char>((v >> vb) & 0xFF));
                vb -= 8;
            }
        }
        std::memcpy(private_key_buf, decoded_key.data(),
                    std::min(decoded_key.size(), static_cast<size_t>(WG_KEY_LEN)));
    }

    if (!netlink_->set_device(config_.interface_name, private_key_buf,
                              config_.listen_port, remaining_peers)) {
        return false;
    }

    known_peer_keys_.erase(public_key);
    peer_was_connected_.erase(public_key);
    return true;
}

WgPeerInfo WireGuardServer::get_peer_info(const std::string& public_key) const {
    std::lock_guard lock(mutex_);
    WgPeerInfo info;
    info.public_key = public_key;

    if (!running_ || !netlink_) return info;

    auto peers = netlink_->get_device_peers(config_.interface_name);
    for (const auto& kp : peers) {
        if (kp.public_key_b64 == public_key) {
            info.allowed_ip = kp.allowed_ip;
            info.last_handshake_sec = kp.last_handshake_sec;
            info.bytes_rx = kp.rx_bytes;
            info.bytes_tx = kp.tx_bytes;
            if (kp.has_endpoint) {
                info.endpoint_addr = sockaddr_to_endpoint(kp.endpoint);
            }
            break;
        }
    }
    return info;
}

std::vector<WgPeerInfo> WireGuardServer::list_peers() {
    std::lock_guard lock(mutex_);
    std::vector<WgPeerInfo> result;

    if (!running_ || !netlink_) return result;

    auto kernel_peers = netlink_->get_device_peers(config_.interface_name);
    std::unordered_map<std::string, WgNetlink::WgPeerStats> by_key;
    for (const auto& kp : kernel_peers) {
        by_key[kp.public_key_b64] = kp;
    }

    for (const auto& [pk, ip] : configured_peers_) {
        WgPeerInfo info;
        info.public_key = pk;
        info.allowed_ip = ip;
        auto it = by_key.find(pk);
        if (it != by_key.end()) {
            info.last_handshake_sec = it->second.last_handshake_sec;
            info.bytes_rx = it->second.rx_bytes;
            info.bytes_tx = it->second.tx_bytes;
            if (it->second.has_endpoint) {
                info.endpoint_addr = sockaddr_to_endpoint(it->second.endpoint);
            }
            if (!it->second.allowed_ip.empty()) {
                info.allowed_ip = it->second.allowed_ip;
            }
        }
        result.push_back(info);
    }
    return result;
}

std::string WireGuardServer::allocate_ip() {
    auto pool = build_ip_pool();
    for (const auto& ip : pool) {
        if (allocated_ips_.find(ip) == allocated_ips_.end()) {
            allocated_ips_.insert(ip);
            return ip;
        }
    }
    return {};
}

void WireGuardServer::release_ip(const std::string& ip) {
    allocated_ips_.erase(ip);
}

bool WireGuardServer::is_ip_available(const std::string& ip) const {
    return allocated_ips_.find(ip) == allocated_ips_.end();
}

void WireGuardServer::poll_peers() {
    std::lock_guard lock(mutex_);
    if (!running_ || !netlink_) return;

    auto logger = get_logger("wg_server");
    auto peers = netlink_->get_device_peers(config_.interface_name);

    for (const auto& kp : peers) {
        bool is_connected = kp.last_handshake_sec > 0 &&
                            kp.last_handshake_sec < 180;
        bool was_connected = false;
        auto it = peer_was_connected_.find(kp.public_key_b64);
        if (it != peer_was_connected_.end()) {
            was_connected = it->second;
        }

        if (is_connected && !was_connected) {
            SPDLOG_LOGGER_INFO(logger, "Peer connected: {}", kp.public_key_b64.substr(0, 16));
            WgPeerInfo info;
            info.public_key = kp.public_key_b64;
            info.allowed_ip = kp.allowed_ip;
            info.last_handshake_sec = kp.last_handshake_sec;
            info.bytes_rx = kp.rx_bytes;
            info.bytes_tx = kp.tx_bytes;
            if (kp.has_endpoint) {
                info.endpoint_addr = sockaddr_to_endpoint(kp.endpoint);
            }
            if (on_peer_connected_) {
                on_peer_connected_(info);
            }
            peer_was_connected_[kp.public_key_b64] = true;
        } else if (!is_connected && was_connected) {
            SPDLOG_LOGGER_INFO(logger, "Peer disconnected: {}", kp.public_key_b64.substr(0, 16));
            WgPeerInfo info;
            info.public_key = kp.public_key_b64;
            info.allowed_ip = kp.allowed_ip;
            info.last_handshake_sec = kp.last_handshake_sec;
            info.bytes_rx = kp.rx_bytes;
            info.bytes_tx = kp.tx_bytes;
            if (kp.has_endpoint) {
                info.endpoint_addr = sockaddr_to_endpoint(kp.endpoint);
            }
            if (on_peer_disconnected_) {
                on_peer_disconnected_(info);
            }
            peer_was_connected_[kp.public_key_b64] = false;
        }
    }
}

std::vector<std::string> WireGuardServer::build_ip_pool() const {
    std::vector<std::string> pool;

    std::string subnet_addr = config_.subnet;
    size_t slash = subnet_addr.find('/');
    if (slash == std::string::npos) return pool;
    std::string base_ip = subnet_addr.substr(0, slash);
    int prefix_len = std::stoi(subnet_addr.substr(slash + 1));

    uint32_t base = ip_to_uint(base_ip);
    uint32_t mask = (0xFFFFFFFF << (32 - prefix_len)) & 0xFFFFFFFF;
    uint32_t network = base & mask;
    uint32_t broadcast = network | (~mask);

    for (uint32_t addr = network + 2; addr < broadcast; ++addr) {
        pool.push_back(uint_to_ip(addr));
    }

    return pool;
}

uint32_t WireGuardServer::ip_to_uint(const std::string& ip) {
    uint32_t result = 0;
    int shift = 24;
    std::istringstream iss(ip);
    std::string octet;
    while (std::getline(iss, octet, '.')) {
        result |= (static_cast<uint32_t>(std::stoi(octet)) << shift);
        shift -= 8;
    }
    return result;
}

std::string WireGuardServer::uint_to_ip(uint32_t addr) {
    std::ostringstream oss;
    oss << ((addr >> 24) & 0xFF) << '.'
        << ((addr >> 16) & 0xFF) << '.'
        << ((addr >> 8) & 0xFF) << '.'
        << (addr & 0xFF);
    return oss.str();
}

} // namespace odc
