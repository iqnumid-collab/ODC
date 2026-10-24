#pragma once

#include "vpn/vpn_connection.hpp"
#include "core/netlink_manager.hpp"
#include "core/tun_device.hpp"
#include "core/credential_crypto.hpp"
#include "core/route_safety.hpp"

#include "client/ovpncli.hpp"

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <atomic>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>

namespace odc {

class OpenVpnClientImpl : public openvpn::ClientAPI::OpenVPNClient {
public:
    void event(const openvpn::ClientAPI::Event& e) override {
        if (event_callback_) {
            event_callback_(e);
        }
    }

    void log(const openvpn::ClientAPI::LogInfo& log) override {
        if (log_callback_) {
            log_callback_(log);
        }
    }

    bool pause_on_connection_timeout() override {
        return true;
    }

    bool tun_builder_new() override {
        tun_builder_reset();
        return true;
    }

    bool tun_builder_set_layer(int layer) override {
        layer_ = layer;
        return true;
    }

    bool tun_builder_add_address(const std::string& address, int prefix_length,
                                  const std::string& gateway, bool ipv6, bool net30) override {
        addresses_.push_back({address, prefix_length, gateway, ipv6, net30});
        return true;
    }

    bool tun_builder_add_route(const std::string& address, int prefix_length,
                                int metric, bool ipv6) override {
        if (route_safety_config_ &&
            RouteSafetyValidator::is_default_route(address, prefix_length, ipv6)) {
            if ((ipv6 && route_safety_config_->block_ipv6_default) ||
                (!ipv6 && route_safety_config_->block_default_route)) {
                SPDLOG_WARN("[openvpn-{}] Blocked default route {}/{} (ipv6={})",
                            connection_id_, address, prefix_length, ipv6);
                return true;
            }
        }
        routes_.push_back({address, prefix_length, metric, ipv6, false});
        return true;
    }

    bool tun_builder_exclude_route(const std::string& address, int prefix_length,
                                    int metric, bool ipv6) override {
        if (route_safety_config_ &&
            RouteSafetyValidator::is_default_route(address, prefix_length, ipv6)) {
            if ((ipv6 && route_safety_config_->block_ipv6_default) ||
                (!ipv6 && route_safety_config_->block_default_route)) {
                SPDLOG_WARN("[openvpn-{}] Blocked exclude default route {}/{} (ipv6={})",
                            connection_id_, address, prefix_length, ipv6);
                return true;
            }
        }
        routes_.push_back({address, prefix_length, metric, ipv6, true});
        return true;
    }

    bool tun_builder_reroute_gw(bool ipv4, bool ipv6, unsigned int) override {
        if (route_safety_config_ &&
            ((ipv4 && route_safety_config_->block_default_route) ||
             (ipv6 && route_safety_config_->block_ipv6_default))) {
            SPDLOG_WARN("[openvpn-{}] Blocked default gateway reroute (ipv4={}, ipv6={})",
                        connection_id_, ipv4, ipv6);
            return false;
        }
        reroute_gw_ipv4_ = ipv4;
        reroute_gw_ipv6_ = ipv6;
        return true;
    }

    bool tun_builder_set_remote_address(const std::string& address, bool) override {
        remote_addr_ = address;
        return true;
    }

    bool tun_builder_set_mtu(int mtu) override {
        mtu_ = mtu;
        return true;
    }

    bool tun_builder_set_session_name(const std::string& name) override {
        session_name_ = name;
        return true;
    }

    int tun_builder_establish() override {
        SPDLOG_INFO("[openvpn-{}] tun_builder_establish fd={} iface={} addrs={}",
                    connection_id_, tun_fd_, interface_name_, addresses_.size());
        if (tun_fd_ < 0) {
            SPDLOG_ERROR("[openvpn-{}] tun_builder_establish: no pre-created TUN fd",
                         connection_id_);
            return -1;
        }

        const std::string& ifname = interface_name_;
        if (ifname.empty()) {
            SPDLOG_ERROR("[openvpn-{}] tun_builder_establish: empty interface name",
                         connection_id_);
            return tun_fd_;
        }

        try {
            NetlinkManager nl;
            for (const auto& addr : addresses_) {
                if (!nl.add_address(ifname, addr.address, addr.prefix_length, addr.ipv6)) {
                    SPDLOG_WARN("[openvpn-{}] add_address {}/{} failed",
                                connection_id_, addr.address, addr.prefix_length);
                }
            }
            if (mtu_ > 0) {
                nl.set_mtu(ifname, mtu_);
            }
            // ioctl UP (reliable)
            int s = socket(AF_INET, SOCK_DGRAM, 0);
            if (s >= 0) {
                struct ifreq ifr{};
                std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
                if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0) {
                    ifr.ifr_flags |= IFF_UP;
                    ioctl(s, SIOCSIFFLAGS, &ifr);
                }
                close(s);
            }
            for (const auto& route : routes_) {
                if (route.exclude) continue;
                nl.add_route(ifname, route.address, route.prefix_length, "", route.ipv6);
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[openvpn-{}] TUN config exception: {}", connection_id_, e.what());
        }

        SPDLOG_INFO("[openvpn-{}] TUN {} established (fd={}) addrs={}",
                    connection_id_, ifname, tun_fd_, addresses_.size());
        return tun_fd_;
    }

    bool tun_builder_add_proxy_bypass(const std::string& bypass_host) override {
        proxy_bypass_.push_back(bypass_host);
        return true;
    }

    bool tun_builder_dco_available() { return false; }

    void tun_builder_teardown(bool) override {
        tun_builder_reset();
    }

    void external_pki_cert_request(openvpn::ClientAPI::ExternalPKICertRequest&) override {}
    void external_pki_sign_request(openvpn::ClientAPI::ExternalPKISignRequest&) override {}

    void acc_event(const openvpn::ClientAPI::AppCustomControlMessageEvent&) override {}

    void set_event_callback(std::function<void(const openvpn::ClientAPI::Event&)> cb) {
        event_callback_ = std::move(cb);
    }

    void set_log_callback(std::function<void(const openvpn::ClientAPI::LogInfo&)> cb) {
        log_callback_ = std::move(cb);
    }

    void set_tun_fd(int fd) { tun_fd_ = fd; }
    void set_interface_name(const std::string& name) { interface_name_ = name; }
    void set_route_safety_config(const RouteSafetyConfig& cfg) { route_safety_config_ = cfg; }
    void set_connection_id(const std::string& id) { connection_id_ = id; }

    struct Address {
        std::string address;
        int prefix_length;
        std::string gateway;
        bool ipv6;
        bool net30;
    };

    struct Route {
        std::string address;
        int prefix_length;
        int metric;
        bool ipv6;
        bool exclude;
    };

    int layer_{3};
    std::vector<Address> addresses_;
    std::vector<Route> routes_;
    bool reroute_gw_ipv4_{false};
    bool reroute_gw_ipv6_{false};
    std::string remote_addr_;
    int mtu_{1500};
    std::string session_name_;
    std::vector<std::string> dns_servers_;
    std::vector<std::string> dns_domains_;
    std::vector<std::string> search_domains_;
    std::vector<std::string> proxy_bypass_;
    std::vector<std::string> dhcp_options_;
    std::string adapter_domain_suffix_;
    bool disable_ipv6_{false};
    int tun_fd_{-1};
    std::string interface_name_;

private:
    std::function<void(const openvpn::ClientAPI::Event&)> event_callback_;
    std::function<void(const openvpn::ClientAPI::LogInfo&)> log_callback_;
    std::optional<RouteSafetyConfig> route_safety_config_;
    std::string connection_id_;

    void tun_builder_reset() {
        addresses_.clear();
        routes_.clear();
        dns_servers_.clear();
        dns_domains_.clear();
        search_domains_.clear();
        proxy_bypass_.clear();
        dhcp_options_.clear();
        // Keep tun_fd_ — set_tun_fd() runs before connect(); tun_builder_new()
        // must not drop the pre-created ovpn-<id> descriptor or OpenVPN3
        // falls back to creating a second "tun" iface (broken data plane).
    }
};

class OpenVpnConnection : public VpnConnection {
public:
    explicit OpenVpnConnection(const OpenVPNConnectionConfig& config)
        : config_(config), running_(false) {}

    ~OpenVpnConnection() override {
        stop();
    }

    VpnType type() const override { return VpnType::OpenVPN; }
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
            tun_device_ = std::make_unique<TunDevice>();
            if (!tun_device_->create("ovpn-" + config_.id)) {
                record_error("Failed to create TUN device");
                set_state(VpnState::Error);
                return false;
            }

            set_interface_name(tun_device_->name());
            client_.set_tun_fd(tun_device_->fd());
            client_.set_interface_name(tun_device_->name());
            client_.set_route_safety_config(config_.route_safety);
            client_.set_connection_id(config_.id);

            netlink_ = std::make_unique<NetlinkManager>();

            std::string profile_content;
            {
                std::ifstream ifs(config_.config_path);
                std::stringstream ss;
                ss << ifs.rdbuf();
                profile_content = ss.str();
            }

            if (config_.route_safety.block_default_route) {
                profile_content += "\npull-filter ignore redirect-gateway\n";
                profile_content += "\npull-filter ignore redirect-gateway6\n";
                SPDLOG_INFO("[openvpn-{}] Added pull-filter to block redirect-gateway", config_.id);
            }

            // Force TunBuilder path: strip kernel "dev tun" so OpenVPN3 does not
            // create a second anonymous "tun" alongside ovpn-<id>.
            {
                std::istringstream in(profile_content);
                std::ostringstream out;
                std::string line;
                while (std::getline(in, line)) {
                    auto trimmed = line;
                    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
                        trimmed.erase(trimmed.begin());
                    }
                    if (trimmed.rfind("dev ", 0) == 0 || trimmed == "dev") {
                        continue;
                    }
                    out << line << '\n';
                }
                profile_content = out.str();
            }

            openvpn::ClientAPI::Config config;
            config.content = profile_content;

            auto eval = client_.eval_config(config);
            if (eval.error) {
                record_error("Config evaluation failed: " + eval.message);
                set_state(VpnState::Error);
                return false;
            }

            if (!eval.autologin) {
                openvpn::ClientAPI::ProvideCreds creds;
                if (!config_.credentials_path.empty() && std::filesystem::exists(config_.credentials_path)) {
                    load_credentials(creds);
                }
                client_.provide_creds(creds);
            }

            running_ = true;
            connected_ = false;

            client_.set_event_callback([this](const openvpn::ClientAPI::Event& e) {
                handle_event(e);
            });

            client_.set_log_callback([this](const openvpn::ClientAPI::LogInfo& log) {
                if (event_bus_) {
                    LogEvent evt{
                        .level = LogLevel::Debug,
                        .logger_name = "openvpn:" + config_.id,
                        .message = log.text
                    };
                    event_bus_->publish(std::move(evt));
                }
            });

            auto timeout_start = std::chrono::steady_clock::now();
            auto timeout_dur = std::chrono::seconds(connection_timeout_sec());

            io_thread_ = std::thread([this]() {
                try {
                    auto status = client_.connect();
                    if (status.error) {
                        record_error(status.message);
                    }
                } catch (const std::exception& e) {
                    record_error(e.what());
                }
                running_ = false;
                connected_ = false;
                if (state() == VpnState::Connected) {
                    set_state(VpnState::Disconnected);
                }
            });

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

        client_.stop();

        if (io_thread_.joinable()) {
            io_thread_.join();
        }

        if (health_thread_.joinable()) {
            health_thread_.join();
        }

        if (stats_thread_.joinable()) {
            stats_thread_.join();
        }

        teardown_interface();

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
    OpenVPNConnectionConfig config_;
    OpenVpnClientImpl client_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread io_thread_;
    std::thread health_thread_;
    std::thread stats_thread_;
    std::atomic<bool> health_check_running_{false};
    std::atomic<bool> stats_running_{false};
    std::chrono::steady_clock::time_point start_time_;
    std::unique_ptr<TunDevice> tun_device_;
    std::unique_ptr<NetlinkManager> netlink_;

    void load_credentials(openvpn::ClientAPI::ProvideCreds& creds) {
        std::string path = config_.credentials_path.string();
        std::string key_path = path + ".key";

        if (std::filesystem::exists(key_path)) {
            try {
                std::string decrypted = CredentialCrypto::decrypt_file(config_.credentials_path, key_path);
                std::istringstream iss(decrypted);
                std::getline(iss, creds.username);
                std::getline(iss, creds.password);
            } catch (const std::exception& e) {
                std::ifstream ifs(config_.credentials_path);
                std::getline(ifs, creds.username);
                std::getline(ifs, creds.password);
            }
        } else {
            std::ifstream ifs(config_.credentials_path);
            std::getline(ifs, creds.username);
            std::getline(ifs, creds.password);
        }
    }

    void handle_event(const openvpn::ClientAPI::Event& e) {
        if (e.name == "CONNECTED") {
            connected_ = true;
            set_state(VpnState::Connected);
            start_time_ = std::chrono::steady_clock::now();

            // Address/routes/MTU are applied in tun_builder_establish().
            // Never run synchronous netlink on this OpenVPN io-thread callback:
            // a blocked recvmsg freezes UDP keepalives and kills the data plane
            // while leaving the TUN iface up (Recv-Q grows, ping TX stalls).
            for (const auto& addr : client_.addresses_) {
                publish_interface_address(VpnInterfaceAction::AddressAdded,
                                          addr.address, addr.prefix_length, addr.ipv6);
            }

            auto info = client_.connection_info();
            if (info.defined) {
                auto s = stats();
                s.bytes_in = 0;
                s.bytes_out = 0;
                update_stats(s);
            }
        } else if (e.name == "DISCONNECTED" || e.name == "RECONNECTING") {
            if (e.fatal) {
                record_error(e.info);
                if (state() == VpnState::Connected) {
                    set_state(VpnState::Error);
                }
            }
        }
    }

    void teardown_interface() {
        if (netlink_ && !interface_name().empty()) {
            netlink_->bring_interface_down(interface_name());
            netlink_.reset();
        }
        if (tun_device_) {
            tun_device_->close();
            tun_device_.reset();
        }
    }

    void update_duration() {
        auto s = stats();
        s.duration_sec += std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start_time_
        ).count();
        update_stats(s);
    }

    bool ping_host(const std::string& host, double& latency_ms, int timeout_ms = 2000) {
        int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (sock < 0) {
            return false;
        }

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
        packet.hdr.checksum = 0;
        packet.hdr.un.echo.id = getpid() & 0xFFFF;
        packet.hdr.un.echo.sequence = 1;

        for (size_t i = 0; i < sizeof(packet.data); i++) {
            packet.data[i] = i;
        }

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

        if (n < 0) {
            return false;
        }

        auto* ip_hdr = reinterpret_cast<struct iphdr*>(recv_buf);
        auto* icmp_hdr = reinterpret_cast<struct icmphdr*>(recv_buf + (ip_hdr->ihl * 4));

        if (icmp_hdr->type == ICMP_ECHOREPLY &&
            icmp_hdr->un.echo.id == (getpid() & 0xFFFF)) {
            latency_ms = std::chrono::duration<double, std::milli>(recv_time - send_time).count();
            return true;
        }

        return false;
    }

    static uint16_t checksum(uint16_t* data, int length) {
        uint32_t sum = 0;
        for (int i = 0; i < length / 2; i++) {
            sum += data[i];
        }
        if (length % 2) {
            sum += data[length / 2] & 0xFF;
        }
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        return ~sum;
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
                } else if (connected_ && running_) {
                    h.healthy = true;
                    h.consecutive_failures = 0;
                    h.latency_ms = 0;
                } else {
                    h.healthy = false;
                    h.consecutive_failures++;
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
