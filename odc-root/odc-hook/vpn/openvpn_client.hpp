#pragma once

#include "vpn_types.hpp"
#include <asio.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <mutex>

namespace odc::hook {

class TunDevice;

class OpenVPNClient {
public:
    using StateCallback = std::function<void(VpnState, std::string error)>;
    using StatsCallback = std::function<void(VpnStats)>;

    OpenVPNClient();
    ~OpenVPNClient();
    friend class OVPN3Impl;

    bool start(const OpenVPNConfig& cfg, asio::io_context& io);
    void stop();
    VpnState state() const;
    VpnStats stats() const;

    void on_state_change(StateCallback cb);
    void on_stats(StatsCallback cb);

    const std::string& interface_name() const;

private:
    asio::io_context* io_{nullptr};
    StateCallback state_cb_;
    StatsCallback stats_cb_;
    OpenVPNConfig cfg_;

    std::atomic<VpnState> state_{VpnState::Idle};
    VpnStats stats_;
    mutable std::mutex stats_mutex_;
    std::string interface_name_;
    TunDevice* tun_{nullptr};  // for cross-platform stats

    std::thread connect_thread_;
    std::thread stats_thread_;
    std::atomic<bool> running_{false};

    void set_state(VpnState s, const std::string& error = "");
    void stats_poll_loop();
};

} // namespace odc::hook
