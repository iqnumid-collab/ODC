#pragma once

#include "openvpn_server.hpp"

#include <openvpn/server/manage.hpp>
#include <openvpn/addr/ip.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace odc {

class OvpnTunFactory;

// Shared client registry used by Man + facade list/disconnect.
class OvpnClientRegistry {
public:
    using ConnectCb = std::function<void(const OvpnClientInfo&)>;
    using DisconnectCb = std::function<void(const OvpnClientInfo&)>;

    void set_callbacks(ConnectCb on_c, DisconnectCb on_d) {
        on_connect_ = std::move(on_c);
        on_disconnect_ = std::move(on_d);
    }

    void client_connected(const OvpnClientInfo& info,
                          openvpn::ManClientInstance::Recv* recv);
    // If recv is non-null, only clear when it still owns the CN (avoids races
    // when a newer session replaced an older duplicate-CN session).
    void client_disconnected(const std::string& cn,
                             openvpn::ManClientInstance::Recv* recv = nullptr);
    void update_stats(const std::string& cn, uint64_t rx, uint64_t tx);

    std::vector<OvpnClientInfo> list() const;
    bool disconnect_cn(const std::string& cn);
    void clear();

    void register_vip(const std::string& vip, openvpn::TunClientInstance::Recv* session);
    void unregister_vip(const std::string& vip);
    openvpn::TunClientInstance::Recv* session_for_vip(const std::string& vip) const;

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, OvpnClientInfo> by_cn_;
    std::unordered_map<std::string, openvpn::ManClientInstance::Recv*> recv_by_cn_;
    std::unordered_map<std::string, openvpn::TunClientInstance::Recv*> session_by_vip_;
    ConnectCb on_connect_;
    DisconnectCb on_disconnect_;
};

class OvpnManFactory : public openvpn::ManClientInstance::Factory {
public:
    using Ptr = openvpn::RCPtr<OvpnManFactory>;

    OvpnManFactory(OvpnServerConfig cfg,
                   OvpnClientRegistry& registry,
                   OvpnTunFactory* tun_factory);

    void start() override;
    void stop() override;
    openvpn::ManClientInstance::Send::Ptr new_man_obj(
        openvpn::ManClientInstance::Recv* instance) override;

    bool allocate_ip(std::string& out_ip);
    void release_ip(const std::string& ip);
    const OvpnServerConfig& config() const { return cfg_; }
    OvpnClientRegistry& registry() { return registry_; }
    OvpnTunFactory* tun_factory() { return tun_factory_; }

private:
    OvpnServerConfig cfg_;
    OvpnClientRegistry& registry_;
    OvpnTunFactory* tun_factory_;
    std::mutex pool_mu_;
    uint32_t next_host_{2};
    uint32_t net_base_{0};
    uint32_t prefix_{24};
    std::unordered_map<std::string, bool> allocated_;
    std::atomic<std::uint64_t> next_id_{1};
    bool running_{false};
};

} // namespace odc
