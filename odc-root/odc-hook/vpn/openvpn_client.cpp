#include "openvpn_client.hpp"
#include "tun_device.hpp"

#include <client/ovpncli.hpp>
#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <cstring>

namespace odc::hook {

class OVPN3Impl : public openvpn::ClientAPI::OpenVPNClient {
public:
    OVPN3Impl(odc::hook::OpenVPNClient* parent) : parent_(parent) {}

    void event(const openvpn::ClientAPI::Event& ev) override {
        std::string ev_name = ev.name;
        std::string ev_info = ev.info;

        if (ev_name == "CONNECTED") {
            parent_->set_state(VpnState::Connected);
        } else if (ev_name == "DISCONNECTED") {
            parent_->set_state(VpnState::Disconnected);
        } else if (ev_name == "RECONNECTING") {
            parent_->set_state(VpnState::Reconnecting, ev_info);
        } else if (ev.error) {
            parent_->set_state(VpnState::Error, ev_name + ": " + ev_info);
        } else {
            SPDLOG_DEBUG("OpenVPN event: {} info={}", ev_name, ev_info);
        }
    }

    void log(const openvpn::ClientAPI::LogInfo& li) override {
        SPDLOG_DEBUG("OpenVPN: {}", li.text);
    }

    // TunBuilderBase overrides
    bool tun_builder_new() override {
        TunConfig tcfg;
        tcfg.name = parent_->cfg_.interface_name;

        if (!tun_.open(tcfg)) {
            SPDLOG_ERROR("OpenVPN: failed to create TUN device");
            return false;
        }
        parent_->interface_name_ = tun_.name();
        parent_->tun_ = &tun_;
        SPDLOG_INFO("OpenVPN: TUN {} created", parent_->interface_name_);
        return true;
    }

    bool tun_builder_set_remote_address(const std::string& addr, bool ipv6) override {
        (void)addr; (void)ipv6;
        return true;
    }

    bool tun_builder_add_address(const std::string& addr, int prefix_len,
                                 const std::string& gw, bool ipv6, bool net30) override {
        (void)gw; (void)ipv6; (void)net30;
        tun_cfg_.ip_address = addr;
        tun_cfg_.prefix_len = prefix_len;
        return true;
    }

    bool tun_builder_add_route(const std::string& addr, int prefix_len,
                               int metric, bool ipv6) override {
        (void)addr; (void)prefix_len; (void)metric; (void)ipv6;
        return true;
    }

    bool tun_builder_reroute_gw(bool ipv4, bool ipv6, unsigned int flags) override {
        (void)ipv4; (void)ipv6; (void)flags;
        return true;
    }

    bool tun_builder_exclude_route(const std::string& addr, int prefix_len,
                                   int metric, bool ipv6) override {
        (void)addr; (void)prefix_len; (void)metric; (void)ipv6;
        return true;
    }

    bool tun_builder_set_mtu(int mtu) override {
        tun_cfg_.mtu = mtu;
        return true;
    }

    bool tun_builder_set_session_name(const std::string& name) override {
        (void)name;
        return true;
    }

    int tun_builder_establish() override {
        tun_.set_ip();
        tun_.set_mtu(tun_cfg_.mtu);
        tun_.bring_up();
        SPDLOG_INFO("OpenVPN: TUN {} established with IP {} prefix {} MTU {}",
                    tun_.name(), tun_cfg_.ip_address, tun_cfg_.prefix_len, tun_cfg_.mtu);
        return tun_.native_handle();
    }

    void tun_builder_teardown(bool disconnect) override {
        (void)disconnect;
        tun_.close();
        parent_->interface_name_.clear();
    }

    bool pause_on_connection_timeout() override { return false; }

    void acc_event(const openvpn::ClientAPI::AppCustomControlMessageEvent&) override {}

    void external_pki_cert_request(openvpn::ClientAPI::ExternalPKICertRequest&) override {}
    void external_pki_sign_request(openvpn::ClientAPI::ExternalPKISignRequest&) override {}

    TunDevice tun_;
    TunConfig tun_cfg_;
    odc::hook::OpenVPNClient* parent_;
};

// ── OpenVPNClient ──────────────────────────────────────────────────────────

OpenVPNClient::OpenVPNClient() = default;
OpenVPNClient::~OpenVPNClient() { stop(); }

bool OpenVPNClient::start(const OpenVPNConfig& cfg, asio::io_context& io) {
    io_ = &io;
    cfg_ = cfg;
    running_ = true;
    set_state(VpnState::Connecting);

    connect_thread_ = std::thread([this]() {
        try {
            OVPN3Impl client(this);

            if (!std::filesystem::exists(cfg_.config_path)) {
                SPDLOG_ERROR("OpenVPN: config file not found: {}", cfg_.config_path);
                set_state(VpnState::Error, "config file not found");
                return;
            }

            std::ifstream ifs(cfg_.config_path);
            std::stringstream ss;
            ss << ifs.rdbuf();
            std::string profile = ss.str();

            if (cfg_.route_safety_block_default) {
                auto pos = profile.find("\ndev ");
                if (pos == std::string::npos) pos = 0;
                std::string filters =
                    "\npull-filter ignore \"redirect-gateway\"\n"
                    "pull-filter ignore \"redirect-private\"\n"
                    "route-nopull\n";
                profile.insert(pos, filters);
            }

            openvpn::ClientAPI::Config ovpn_cfg;
            ovpn_cfg.content = profile;
            ovpn_cfg.guiVersion = "odc-hook/0.1.0";

            auto eval = client.eval_config(ovpn_cfg);
            if (eval.error) {
                SPDLOG_ERROR("OpenVPN: eval_config failed: {}", eval.message);
                set_state(VpnState::Error, eval.message);
                return;
            }

            if (!cfg_.credentials_path.empty() &&
                std::filesystem::exists(cfg_.credentials_path)) {
                std::ifstream cfs(cfg_.credentials_path);
                std::string user, pass;
                if (std::getline(cfs, user) && std::getline(cfs, pass)) {
                    openvpn::ClientAPI::ProvideCreds creds;
                    creds.username = user;
                    creds.password = pass;
                    client.provide_creds(creds);
                }
            }

            SPDLOG_INFO("OpenVPN: connecting via {}", cfg_.config_path);
            auto status = client.connect();
            if (status.error) {
                SPDLOG_ERROR("OpenVPN: connect error: {} ({})", status.message, status.status);
                set_state(VpnState::Error, status.message);
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("OpenVPN: exception: {}", e.what());
            set_state(VpnState::Error, e.what());
        }
    });

    stats_thread_ = std::thread(&OpenVPNClient::stats_poll_loop, this);
    return true;
}

void OpenVPNClient::stop() {
    if (!running_.exchange(false)) return;
    set_state(VpnState::Disconnecting);

    if (connect_thread_.joinable()) connect_thread_.join();
    if (stats_thread_.joinable()) stats_thread_.join();

    set_state(VpnState::Disconnected);
}

VpnState OpenVPNClient::state() const { return state_.load(); }

VpnStats OpenVPNClient::stats() const {
    std::lock_guard lock(stats_mutex_);
    return stats_;
}

void OpenVPNClient::on_state_change(StateCallback cb) { state_cb_ = std::move(cb); }
void OpenVPNClient::on_stats(StatsCallback cb) { stats_cb_ = std::move(cb); }
const std::string& OpenVPNClient::interface_name() const { return interface_name_; }

void OpenVPNClient::set_state(VpnState s, const std::string& error) {
    state_ = s;
    if (state_cb_) state_cb_(s, error);
}

void OpenVPNClient::stats_poll_loop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        if (state_ == VpnState::Connected) {
            if (tun_ && tun_->is_open()) {
                std::lock_guard lock(stats_mutex_);
                stats_.bytes_in = tun_->rx_bytes();
                stats_.bytes_out = tun_->tx_bytes();
            }
#if defined(__linux__)
            else if (!interface_name_.empty()) {
                std::string base = "/sys/class/net/" + interface_name_ + "/statistics/";

                auto read_stat = [](const std::string& path) -> uint64_t {
                    std::ifstream ifs(path);
                    if (!ifs) return 0;
                    uint64_t val = 0;
                    ifs >> val;
                    return val;
                };

                std::lock_guard lock(stats_mutex_);
                stats_.bytes_in = read_stat(base + "rx_bytes");
                stats_.bytes_out = read_stat(base + "tx_bytes");
                stats_.packets_in = read_stat(base + "rx_packets");
                stats_.packets_out = read_stat(base + "tx_packets");
            }
#endif
        }

        if (stats_cb_) {
            std::lock_guard lock(stats_mutex_);
            stats_cb_(stats_);
        }
    }
}

} // namespace odc::hook
