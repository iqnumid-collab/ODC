#pragma once

#include "vpn/vpn_connection.hpp"
#include "core/wg_netlink.hpp"
#include "core/route_safety.hpp"

#include <sodium.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <atomic>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <filesystem>

namespace odc {

class WireGuardConnection : public VpnConnection {
public:
    explicit WireGuardConnection(const WireGuardConnectionConfig& config)
        : config_(config), running_(false) {}

    ~WireGuardConnection() override {
        stop();
    }

    VpnType type() const override { return VpnType::WireGuard; }
    std::string id() const override { return config_.id; }

    bool start() override {
        if (state() != VpnState::Idle && state() != VpnState::Disconnected) {
            return false;
        }

        std::error_code ec;
        if (!std::filesystem::exists(config_.config_path, ec) || ec) {
            record_error("Config file not found: " + config_.config_path.string());
            return false;
        }

        set_state(VpnState::Connecting);

        try {
            std::string iface_name = "wg-" + config_.id;
            set_interface_name(iface_name);

            SPDLOG_INFO("[wg-{}] Creating WireGuard interface", config_.id);
            wg_ = std::make_unique<WgNetlink>();

            wg_->delete_interface(iface_name);
            SPDLOG_INFO("[wg-{}] Creating interface {}", config_.id, iface_name);
            if (!wg_->create_interface(iface_name)) {
                SPDLOG_ERROR("[wg-{}] Failed to create WireGuard interface", config_.id);
                record_error("Failed to create WireGuard interface");
                set_state(VpnState::Error);
                return false;
            }
            SPDLOG_INFO("[wg-{}] Interface created", config_.id);

            WgConfig parsed;
            if (!parse_config(config_.config_path.string(), parsed)) {
                record_error("Failed to parse WireGuard config");
                set_state(VpnState::Error);
                return false;
            }

            std::vector<uint8_t> private_key(WG_KEY_LEN);
            SPDLOG_INFO("[wg-{}] Private key length: {}", config_.id, parsed.private_key.length());
            if (!base64_to_key(parsed.private_key, private_key.data())) {
                SPDLOG_ERROR("[wg-{}] Invalid private key: {}", config_.id, parsed.private_key);
                record_error("Invalid private key");
                set_state(VpnState::Error);
                return false;
            }
            SPDLOG_INFO("[wg-{}] Private key decoded successfully", config_.id);

            WgPeer peer;
            if (!base64_to_key(parsed.peer_public_key, peer.public_key)) {
                record_error("Invalid peer public key");
                set_state(VpnState::Error);
                return false;
            }

            if (!parsed.preshared_key.empty()) {
                if (!base64_to_key(parsed.preshared_key, peer.preshared_key)) {
                    record_error("Invalid preshared key");
                    set_state(VpnState::Error);
                    return false;
                }
                peer.has_preshared_key = true;
            }

            if (!parsed.endpoint.empty()) {
                auto colon = parsed.endpoint.find(':');
                if (colon != std::string::npos) {
                    std::string host = parsed.endpoint.substr(0, colon);
                    uint16_t port = static_cast<uint16_t>(std::stoi(parsed.endpoint.substr(colon + 1)));
                    struct sockaddr_in* addr4 = (struct sockaddr_in*)&peer.endpoint;
                    std::memset(addr4, 0, sizeof(*addr4));
                    addr4->sin_family = AF_INET;
                    addr4->sin_port = htons(port);
                    inet_pton(AF_INET, host.c_str(), &addr4->sin_addr);
                }
            }

            if (parsed.persistent_keepalive > 0) {
                peer.persistent_keepalive_interval = parsed.persistent_keepalive;
                peer.has_keepalive = true;
            }

            for (const auto& aip_str : parsed.allowed_ips) {
                WgAllowedIP aip;
                auto slash = aip_str.find('/');
                if (slash != std::string::npos) {
                    std::string ip = aip_str.substr(0, slash);
                    aip.cidr = static_cast<uint8_t>(std::stoi(aip_str.substr(slash + 1)));
                    if (inet_pton(AF_INET, ip.c_str(), &aip.ip4) == 1) {
                        aip.family = AF_INET;
                    } else if (inet_pton(AF_INET6, ip.c_str(), &aip.ip6) == 1) {
                        aip.family = AF_INET6;
                    } else {
                        continue;
                    }
                    peer.allowed_ips.push_back(aip);
                }
            }

            if (!wg_->set_device(iface_name, private_key.data(), 0, {peer})) {
                record_error("Failed to configure WireGuard device");
                set_state(VpnState::Error);
                return false;
            }

            auto slash = parsed.address.find('/');
            if (slash != std::string::npos) {
                std::string ip = parsed.address.substr(0, slash);
                int prefix = std::stoi(parsed.address.substr(slash + 1));
                if (!wg_->add_address(iface_name, ip, prefix)) {
                    record_error("Failed to add address to interface");
                    set_state(VpnState::Error);
                    return false;
                }
                publish_interface_address(VpnInterfaceAction::AddressAdded, ip, prefix, false);
                if (!parsed.address.empty()) {
                    auto comma = parsed.address.find(',');
                    if (comma != std::string::npos) {
                        std::string ipv6_part = parsed.address.substr(comma + 1);
                        auto v6slash = ipv6_part.find('/');
                        if (v6slash != std::string::npos) {
                            std::string v6ip = ipv6_part.substr(0, v6slash);
                            int v6prefix = std::stoi(ipv6_part.substr(v6slash + 1));
                            publish_interface_address(VpnInterfaceAction::AddressAdded, v6ip, v6prefix, true);
                        }
                    }
                }
            }

            if (!wg_->bring_interface_up(iface_name)) {
                record_error("Failed to bring up WireGuard interface");
                set_state(VpnState::Error);
                return false;
            }

            // Small delay to ensure interface is fully registered
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            RouteSafetyValidator validator(config_.route_safety);

            for (const auto& aip_str : parsed.allowed_ips) {
                auto slash2 = aip_str.find('/');
                if (slash2 != std::string::npos) {
                    std::string dst = aip_str.substr(0, slash2);
                    int prefix = std::stoi(aip_str.substr(slash2 + 1));
                    bool ipv6 = (aip_str.find(':') != std::string::npos);

                    if (!validator.is_route_allowed(dst, prefix, ipv6)) {
                        SPDLOG_WARN("[wg-{}] Blocked default route {}/{}", config_.id, dst, prefix);
                        continue;
                    }

                    if (!wg_->add_route(iface_name, dst, prefix)) {
                        SPDLOG_WARN("Failed to add route {} via {}", dst, iface_name);
                    }
                }
            }

            running_ = true;
            set_state(VpnState::Connected);
            start_time_ = std::chrono::steady_clock::now();

            stats_running_.store(true);
            stats_thread_ = std::thread([this]() {
                while (stats_running_.load() && state() == VpnState::Connected) {
                    read_interface_stats();
                    update_duration();
                    for (int i = 0; i < 50 && stats_running_.load() && state() == VpnState::Connected; ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
            });

            if (config_.health_check.enabled) {
                start_health_check();
            }

            return true;
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[wg-{}] Exception in start: {}", config_.id, e.what());
            record_error(e.what());
            set_state(VpnState::Error);
            return false;
        }
    }

    bool stop() override {
        if (state() == VpnState::Idle || state() == VpnState::Disconnected) {
            return true;
        }

        set_state(VpnState::Disconnecting);
        running_ = false;
        health_check_running_ = false;
        stats_running_.store(false);

        if (health_thread_.joinable()) {
            health_thread_.join();
        }

        if (stats_thread_.joinable()) {
            stats_thread_.join();
        }

        if (wg_) {
            wg_->delete_interface(interface_name());
            wg_.reset();
        }

        update_duration();
        set_state(VpnState::Disconnected);
        return true;
    }

    bool restart() override {
        auto s = stats();
        s.reconnect_count++;
        set_state(VpnState::Reconnecting);
        stop();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return start();
    }

private:
    struct WgConfig {
        std::string private_key;
        std::string address;
        std::string dns;
        std::string peer_public_key;
        std::string preshared_key;
        std::string endpoint;
        std::vector<std::string> allowed_ips;
        uint16_t persistent_keepalive = 0;
    };

    WireGuardConnectionConfig config_;
    std::unique_ptr<WgNetlink> wg_;
    std::atomic<bool> running_{false};
    std::thread health_thread_;
    std::thread stats_thread_;
    std::atomic<bool> health_check_running_{false};
    std::atomic<bool> stats_running_{false};
    std::chrono::steady_clock::time_point start_time_;

    static bool base64_to_key(const std::string& b64, uint8_t* key) {
        size_t len = WG_KEY_LEN;
        size_t bin_len = 0;
        int ret = sodium_base642bin(key, len, b64.c_str(), b64.size(), nullptr, &bin_len, nullptr, sodium_base64_VARIANT_ORIGINAL);
        if (ret != 0 || bin_len != WG_KEY_LEN) {
            SPDLOG_ERROR("base64_to_key failed: ret={}, bin_len={}", ret, bin_len);
            return false;
        }
        return true;
    }

    static bool parse_config(const std::string& path, WgConfig& cfg) {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return false;

        std::string line;
        std::string section;
        while (std::getline(ifs, line)) {
            trim(line);
            if (line.empty() || line[0] == '#') continue;
            if (line[0] == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            trim(key);
            trim(value);

            if (section == "Interface") {
                if (key == "PrivateKey") cfg.private_key = value;
                else if (key == "Address") cfg.address = value;
                else if (key == "DNS") cfg.dns = value;
            } else if (section == "Peer") {
                if (key == "PublicKey") cfg.peer_public_key = value;
                else if (key == "PresharedKey") cfg.preshared_key = value;
                else if (key == "Endpoint") cfg.endpoint = value;
                else if (key == "AllowedIPs") {
                    std::stringstream ss(value);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        cfg.allowed_ips.push_back(item);
                    }
                } else if (key == "PersistentKeepalive") {
                    cfg.persistent_keepalive = static_cast<uint16_t>(std::stoi(value));
                }
            }
        }
        return !cfg.private_key.empty() && !cfg.peer_public_key.empty();
    }

    static void trim(std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), s.end());
    }

    bool ping_host(const std::string& host, double& latency_ms, int timeout_ms = 2000) {
        int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (sock < 0) return false;

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in dest;
        std::memset(&dest, 0, sizeof(dest));
        dest.sin_family = AF_INET;
        if (inet_pton(AF_INET, host.c_str(), &dest.sin_addr) != 1) {
            close(sock);
            return false;
        }

        struct {
            struct icmphdr hdr;
            char data[56];
        } packet;
        std::memset(&packet, 0, sizeof(packet));
        packet.hdr.type = ICMP_ECHO;
        packet.hdr.code = 0;
        packet.hdr.un.echo.id = getpid() & 0xFFFF;
        packet.hdr.un.echo.sequence = 1;
        for (size_t i = 0; i < sizeof(packet.data); i++) packet.data[i] = i;
        packet.hdr.checksum = checksum(reinterpret_cast<uint16_t*>(&packet), sizeof(packet));

        auto send_time = std::chrono::steady_clock::now();
        if (sendto(sock, &packet, sizeof(packet), 0,
                   reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest)) < 0) {
            close(sock);
            return false;
        }

        char recv_buf[1024];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        ssize_t n = recvfrom(sock, recv_buf, sizeof(recv_buf), 0,
                             reinterpret_cast<struct sockaddr*>(&from), &from_len);
        auto recv_time = std::chrono::steady_clock::now();
        close(sock);

        if (n < 0) return false;

        auto* ip_hdr = reinterpret_cast<struct iphdr*>(recv_buf);
        auto* icmp_hdr = reinterpret_cast<struct icmphdr*>(recv_buf + (ip_hdr->ihl * 4));
        if (icmp_hdr->type == ICMP_ECHOREPLY && icmp_hdr->un.echo.id == (getpid() & 0xFFFF)) {
            latency_ms = std::chrono::duration<double, std::milli>(recv_time - send_time).count();
            return true;
        }
        return false;
    }

    static uint16_t checksum(uint16_t* data, int length) {
        uint32_t sum = 0;
        for (int i = 0; i < length / 2; i++) sum += data[i];
        if (length % 2) sum += data[length / 2] & 0xFF;
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        return ~sum;
    }

    void update_duration() {
        auto s = stats();
        s.duration_sec += std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start_time_
        ).count();
        update_stats(s);
    }

    void start_health_check() {
        health_check_running_ = true;
        health_thread_ = std::thread([this]() {
            while (health_check_running_ && state() == VpnState::Connected) {
                auto h = health();
                h.last_check = std::chrono::system_clock::now();

                if (!config_.health_check.target_ip.empty()) {
                    double latency = 0;
                    if (ping_host(config_.health_check.target_ip, latency)) {
                        h.healthy = true;
                        h.consecutive_failures = 0;
                        h.latency_ms = latency;
                    } else {
                        h.healthy = false;
                        h.consecutive_failures++;
                        if (h.consecutive_failures >= static_cast<int>(config_.max_retries)) {
                            record_error("Health check failed " + std::to_string(h.consecutive_failures) + " times");
                            if (config_.max_retries > 0) {
                                restart();
                                break;
                            }
                        }
                    }
                } else {
                    h.healthy = true;
                    h.consecutive_failures = 0;
                    h.latency_ms = 0;
                }

                update_health(h);
                int wait_sec = static_cast<int>(config_.health_check.interval_sec);
                for (int i = 0; i < wait_sec * 10 && health_check_running_ && state() == VpnState::Connected; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        });
    }
};

}
