#include <openvpn/log/logsimple.hpp>

#include "openvpn_management.hpp"
#include "openvpn_tun.hpp"

#include "odc/logging.hpp"

#include <openvpn/buffer/buffer.hpp>

#include <arpa/inet.h>
#include <chrono>
#include <sstream>

namespace odc {
namespace {

std::string ip_from_host(uint32_t host) {
    struct in_addr a{};
    a.s_addr = htonl(host);
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf;
}

uint32_t host_from_ip(const std::string& ip) {
    struct in_addr a{};
    if (inet_pton(AF_INET, ip.c_str(), &a) != 1) return 0;
    return ntohl(a.s_addr);
}

openvpn::BufferPtr make_push_buf(const std::string& s) {
    auto buf = openvpn::BufferAllocatedRc::Create(s.size() + 32, openvpn::BufAllocFlags::GROW);
    buf->write(reinterpret_cast<const unsigned char*>(s.data()), s.size());
    return buf;
}

} // namespace

void OvpnClientRegistry::client_connected(const OvpnClientInfo& info,
                                          openvpn::ManClientInstance::Recv* recv) {
    ConnectCb cb;
    {
        std::lock_guard lock(mu_);
        by_cn_[info.common_name] = info;
        recv_by_cn_[info.common_name] = recv;
        cb = on_connect_;
    }
    if (cb) cb(info);
}

void OvpnClientRegistry::client_disconnected(const std::string& cn,
                                             openvpn::ManClientInstance::Recv* recv) {
    OvpnClientInfo info;
    DisconnectCb cb;
    {
        std::lock_guard lock(mu_);
        if (recv) {
            auto rit = recv_by_cn_.find(cn);
            if (rit == recv_by_cn_.end() || rit->second != recv) {
                return; // superseded by a newer session
            }
        }
        auto it = by_cn_.find(cn);
        if (it == by_cn_.end()) return;
        info = it->second;
        by_cn_.erase(it);
        recv_by_cn_.erase(cn);
        if (!info.assigned_ip.empty()) {
            session_by_vip_.erase(info.assigned_ip);
        }
        cb = on_disconnect_;
    }
    if (cb) cb(info);
}

void OvpnClientRegistry::update_stats(const std::string& cn, uint64_t rx, uint64_t tx) {
    std::lock_guard lock(mu_);
    auto it = by_cn_.find(cn);
    if (it == by_cn_.end()) return;
    it->second.bytes_rx = rx;
    it->second.bytes_tx = tx;
}

std::vector<OvpnClientInfo> OvpnClientRegistry::list() const {
    std::lock_guard lock(mu_);
    std::vector<OvpnClientInfo> out;
    out.reserve(by_cn_.size());
    for (const auto& [_, info] : by_cn_) out.push_back(info);
    return out;
}

bool OvpnClientRegistry::disconnect_cn(const std::string& cn) {
    openvpn::ManClientInstance::Recv* recv = nullptr;
    {
        std::lock_guard lock(mu_);
        auto it = recv_by_cn_.find(cn);
        if (it == recv_by_cn_.end()) return false;
        recv = it->second;
    }
    if (!recv) return false;
    recv->push_halt_restart_msg(openvpn::HaltRestart::HALT,
                                "disconnected by tunnel-manager",
                                "admin disconnect");
    return true;
}

void OvpnClientRegistry::clear() {
    std::lock_guard lock(mu_);
    by_cn_.clear();
    recv_by_cn_.clear();
    session_by_vip_.clear();
}

void OvpnClientRegistry::register_vip(const std::string& vip,
                                      openvpn::TunClientInstance::Recv* session) {
    std::lock_guard lock(mu_);
    session_by_vip_[vip] = session;
}

void OvpnClientRegistry::unregister_vip(const std::string& vip) {
    std::lock_guard lock(mu_);
    session_by_vip_.erase(vip);
}

openvpn::TunClientInstance::Recv* OvpnClientRegistry::session_for_vip(
    const std::string& vip) const {
    std::lock_guard lock(mu_);
    auto it = session_by_vip_.find(vip);
    return it == session_by_vip_.end() ? nullptr : it->second;
}

// ── Per-client Man object ──────────────────────────────────────────────────

class OvpnManSend : public openvpn::ManClientInstance::Send {
public:
    OvpnManSend(openvpn::ManClientInstance::Recv* parent,
                OvpnManFactory& factory,
                std::uint64_t id)
        : parent_(parent), factory_(factory), id_(id) {}

    std::string instance_name() const override {
        return cn_.empty() ? ("ovpn-" + std::to_string(id_)) : ("ovpn-" + cn_);
    }
    std::uint64_t instance_id() const override { return id_; }
    bool is_stopped() const override { return stopped_; }
    std::string to_string_debug() const override { return instance_name(); }
    void ipma_notify(const struct ovpn_tun_head_ipma&) override {}

    std::string describe_user(const bool) override {
        return std::string("{\"cn\":\"") + cn_ + "\",\"ip\":\"" + assigned_ip_ + "\"}";
    }

    void disconnect_user(const openvpn::HaltRestart::Type type,
                         const openvpn::AuthStatus::Type,
                         const std::string& reason,
                         const std::string& client_reason) override {
        if (parent_) {
            parent_->push_halt_restart_msg(type, reason, client_reason);
        }
    }

    void set_acl_index(const int, const std::string*, const bool) override {}
    void userprop_local_update() override {}
    Json::Value doma_acl(const Json::Value&) override {
        return Json::Value(Json::objectValue);
    }
    void post_info_user(openvpn::BufferPtr&&) override {}

    void pre_stop() override {
        if (!cn_.empty() && connected_) {
            factory_.registry().client_disconnected(cn_, parent_);
            connected_ = false;
        }
        if (!assigned_ip_.empty()) {
            factory_.release_ip(assigned_ip_);
            if (factory_.tun_factory()) {
                factory_.tun_factory()->unbind_client_vip(assigned_ip_);
            }
            assigned_ip_.clear();
        }
    }

    void stop() override {
        if (stopped_) return;
        stopped_ = true;
        pre_stop();
    }

    void auth_request(const openvpn::AuthCreds::Ptr&,
                      const openvpn::AuthCert::Ptr& auth_cert,
                      const openvpn::PeerAddr::Ptr& peer_addr) override {
        auto logger = get_logger("ovpn_server");
        if (!auth_cert) {
            parent_->auth_failed("missing client certificate", "missing client certificate");
            return;
        }
        cn_ = auth_cert->get_cn();
        if (cn_.empty()) {
            parent_->auth_failed("empty CN", "empty CN");
            return;
        }
        if (peer_addr) {
            remote_ = peer_addr->remote.to_string();
        }
        SPDLOG_LOGGER_INFO(logger, "OpenVPN auth OK cn={} from {}", cn_, remote_);
        // Latest session wins: drop any prior session for this CN.
        factory_.registry().disconnect_cn(cn_);
        // Certificate already validated by OpenSSL; allow push.
        parent_->schedule_auth_pending_timeout(0);
    }

    void push_request(openvpn::ProtoContext::ProtoConfig::Ptr) override {
        auto logger = get_logger("ovpn_server");
        if (stopped_ || !parent_) return;

        if (assigned_ip_.empty()) {
            if (!factory_.allocate_ip(assigned_ip_)) {
                parent_->auth_failed("IP pool exhausted", "IP pool exhausted");
                return;
            }
        }

        const auto& cfg = factory_.config();
        uint32_t mask_host = prefix_ == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix_));
        struct in_addr ma{};
        ma.s_addr = htonl(mask_host);
        std::string netmask = inet_ntoa(ma);
        std::string gw = ip_from_host(net_base_ + 1);

        std::ostringstream push;
        push << "PUSH_REPLY,topology subnet"
             << ",ifconfig " << assigned_ip_ << ' ' << netmask
             << ",route-gateway " << gw
             << ",ping 10,ping-restart 60"
             << ",cipher " << cfg.cipher
             << ",explicit-exit-notify";

        for (const auto& r : cfg.push_routes) {
            push << ",route " << r;
        }
        for (const auto& d : cfg.dns_servers) {
            push << ",dhcp-option DNS " << d;
        }

        std::vector<openvpn::BufferPtr> msgs;
        msgs.push_back(make_push_buf(push.str()));
        parent_->push_reply(std::move(msgs));

        // Session implements both Man Recv and Tun Recv
        auto* tun_recv = dynamic_cast<openvpn::TunClientInstance::Recv*>(parent_);
        if (factory_.tun_factory() && tun_recv) {
            factory_.tun_factory()->bind_client_vip(assigned_ip_, tun_recv);
        }

        OvpnClientInfo info;
        info.common_name = cn_;
        info.assigned_ip = assigned_ip_;
        info.remote_addr = remote_;
        info.connected_at = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        factory_.registry().client_connected(info, parent_);
        connected_ = true;
        SPDLOG_LOGGER_INFO(logger, "OpenVPN push cn={} vip={}", cn_, assigned_ip_);
    }

    void app_control(const std::string&) override {}

    void stats_notify(const openvpn::PeerStats& ps, const bool final) override {
        if (!cn_.empty()) {
            factory_.registry().update_stats(cn_, ps.rx_bytes, ps.tx_bytes);
        }
        if (final && connected_) {
            factory_.registry().client_disconnected(cn_, parent_);
            connected_ = false;
        }
    }

    void float_notify(const openvpn::PeerAddr::Ptr& addr) override {
        if (addr) remote_ = addr->remote.to_string();
    }

    void keepalive_override(unsigned int&, unsigned int&) override {}

private:
    openvpn::ManClientInstance::Recv* parent_;
    OvpnManFactory& factory_;
    std::uint64_t id_;
    std::string cn_;
    std::string assigned_ip_;
    std::string remote_;
    bool stopped_{false};
    bool connected_{false};
    uint32_t net_base_{0};
    int prefix_{24};

public:
    void set_net(uint32_t base, int prefix) {
        net_base_ = base;
        prefix_ = prefix;
    }
};

OvpnManFactory::OvpnManFactory(OvpnServerConfig cfg,
                               OvpnClientRegistry& registry,
                               OvpnTunFactory* tun_factory)
    : cfg_(std::move(cfg)), registry_(registry), tun_factory_(tun_factory) {
    size_t slash = cfg_.subnet.find('/');
    std::string net = slash == std::string::npos ? cfg_.subnet : cfg_.subnet.substr(0, slash);
    prefix_ = slash == std::string::npos ? 24 : std::stoi(cfg_.subnet.substr(slash + 1));
    net_base_ = host_from_ip(net) & (prefix_ == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix_)));
}

void OvpnManFactory::start() {
    running_ = true;
    next_host_ = 2;
    allocated_.clear();
}

void OvpnManFactory::stop() {
    running_ = false;
    registry_.clear();
}

openvpn::ManClientInstance::Send::Ptr OvpnManFactory::new_man_obj(
    openvpn::ManClientInstance::Recv* instance) {
    auto id = next_id_++;
    auto* obj = new OvpnManSend(instance, *this, id);
    obj->set_net(net_base_, static_cast<int>(prefix_));
    return openvpn::ManClientInstance::Send::Ptr(obj);
}

bool OvpnManFactory::allocate_ip(std::string& out_ip) {
    std::lock_guard lock(pool_mu_);
    uint32_t max_host = (1u << (32 - prefix_)) - 2; // exclude net+bcast conceptually
    for (uint32_t i = 0; i < max_host; ++i) {
        uint32_t host = next_host_++;
        if (host < 2) host = 2;
        if (host >= (1u << (32 - prefix_)) - 1) {
            next_host_ = 2;
            host = 2;
        }
        std::string ip = ip_from_host(net_base_ + host);
        if (allocated_.count(ip)) continue;
        allocated_[ip] = true;
        out_ip = ip;
        return true;
    }
    return false;
}

void OvpnManFactory::release_ip(const std::string& ip) {
    std::lock_guard lock(pool_mu_);
    allocated_.erase(ip);
}

} // namespace odc
