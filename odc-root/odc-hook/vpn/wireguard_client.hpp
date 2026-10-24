#pragma once

#include "vpn_types.hpp"
#include <asio.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace odc::hook {

class WireGuardClient {
public:
    using StateCallback = std::function<void(VpnState, std::string error)>;
    using StatsCallback = std::function<void(VpnStats)>;

    WireGuardClient();
    ~WireGuardClient();

    bool start(const wg::WgDeviceConfig& cfg, asio::io_context& io);
    void stop();
    VpnState state() const { return state_.load(); }
    VpnStats stats() const;

    void on_state_change(StateCallback cb) { state_cb_ = std::move(cb); }
    void on_stats(StatsCallback cb) { stats_cb_ = std::move(cb); }

private:
    asio::io_context* io_{nullptr};
    std::atomic<VpnState> state_{VpnState::Idle};
    StateCallback state_cb_;
    StatsCallback stats_cb_;
    VpnStats stats_;

    wg::WgDeviceConfig cfg_;

#if defined(__linux__)
    // Linux: use kernel WireGuard via netlink
    class WgNetlinkAdapter;
    std::unique_ptr<WgNetlinkAdapter> netlink_;
#else
    // Windows/macOS: use userspace WgDevice + TunDevice
    class TunDevice;
    namespace wg { class WgDevice; }
    std::unique_ptr<TunDevice> tun_;
    std::unique_ptr<wg::WgDevice> device_;
#endif

    std::thread io_thread_;
    std::thread stats_thread_;
    std::atomic<bool> running_{false};

    void set_state(VpnState s, const std::string& error = "");
    void stats_poll_loop();
};

} // namespace odc::hook
