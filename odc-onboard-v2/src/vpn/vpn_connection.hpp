#pragma once

#include "core/event.hpp"
#include "core/event_bus.hpp"
#include "vpn/vpn_config.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <functional>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace odc {

struct VpnConnectionStats {
    uint64_t bytes_in{0};
    uint64_t bytes_out{0};
    uint64_t packets_in{0};
    uint64_t packets_out{0};
    double duration_sec{0};
    int reconnect_count{0};
    std::string last_error;
};

struct VpnHealthStatus {
    bool healthy{false};
    double latency_ms{0};
    int consecutive_failures{0};
    std::chrono::system_clock::time_point last_check;
};

class VpnConnection {
public:
    virtual ~VpnConnection() = default;

    virtual VpnType type() const = 0;
    virtual std::string id() const = 0;

    VpnState state() const { return state_.load(std::memory_order_acquire); }
    VpnConnectionStats stats() const {
        std::lock_guard lock(stats_mutex_);
        return stats_;
    }
    VpnHealthStatus health() const {
        std::lock_guard lock(health_mutex_);
        return health_;
    }

    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool restart() {
        stop();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return start();
    }

    void set_event_bus(EventBus* bus) { event_bus_ = bus; }

    void set_interface_name(const std::string& iface) { interface_name_ = iface; }
    const std::string& interface_name() const { return interface_name_; }

    void set_connection_timeout_sec(uint32_t timeout) { connection_timeout_sec_ = timeout; }
    uint32_t connection_timeout_sec() const { return connection_timeout_sec_; }

protected:
    void set_state(VpnState new_state) {
        auto old = state_.exchange(new_state, std::memory_order_acq_rel);
        if (event_bus_ && old != new_state) {
            publish_state_change(old, new_state);
        }
    }

    void update_stats(const VpnConnectionStats& stats) {
        std::lock_guard lock(stats_mutex_);
        stats_ = stats;
        if (event_bus_) {
            publish_stats();
        }
    }

    void update_health(const VpnHealthStatus& health) {
        std::lock_guard lock(health_mutex_);
        health_ = health;
        if (event_bus_) {
            publish_health();
        }
    }

    void record_error(const std::string& error) {
        std::lock_guard lock(stats_mutex_);
        stats_.last_error = error;
    }

    bool read_interface_stats() {
        if (interface_name_.empty()) {
            return false;
        }

        std::string base = "/sys/class/net/" + interface_name_ + "/statistics/";
        if (!std::filesystem::exists(base)) {
            return false;
        }

        auto read_stat_file = [&](const std::string& filename) -> uint64_t {
            std::string path = base + filename;
            std::ifstream ifs(path);
            if (!ifs) return 0;
            std::string line;
            if (std::getline(ifs, line)) {
                try {
                    return std::stoull(line);
                } catch (...) {
                    return 0;
                }
            }
            return 0;
        };

        std::lock_guard lock(stats_mutex_);
        stats_.bytes_in = read_stat_file("rx_bytes");
        stats_.bytes_out = read_stat_file("tx_bytes");
        stats_.packets_in = read_stat_file("rx_packets");
        stats_.packets_out = read_stat_file("tx_packets");

        return true;
    }

    void publish_interface_address(VpnInterfaceAction action,
                                     const std::string& ip_address,
                                     int prefix_len, bool ipv6) {
        if (!event_bus_) return;
        VpnInterfaceEvent evt;
        evt.connection_id = id();
        evt.interface_name = interface_name_;
        evt.action = action;
        evt.ip_address = ip_address;
        evt.prefix_len = prefix_len;
        evt.ipv6 = ipv6;
        event_bus_->publish_sync(Event{std::move(evt)});
    }

    EventBus* event_bus_{nullptr};
    std::string interface_name_;
    uint32_t connection_timeout_sec_{30};

private:
    std::atomic<VpnState> state_{VpnState::Idle};
    mutable std::mutex stats_mutex_;
    VpnConnectionStats stats_;
    mutable std::mutex health_mutex_;
    VpnHealthStatus health_;

    void publish_state_change(VpnState old_state, VpnState new_state) {
        if (!event_bus_) return;
        VpnStateChangeEvent evt{
            .connection_id = id(),
            .type = type(),
            .old_state = old_state,
            .new_state = new_state
        };
        if (new_state == VpnState::Error) {
            std::lock_guard lock(stats_mutex_);
            evt.error_message = stats_.last_error;
        }
        event_bus_->publish(Event{std::move(evt)});
    }

    void publish_stats() {
        if (!event_bus_) return;
        VpnStatsEvent evt{
            .connection_id = id(),
            .type = type(),
            .bytes_in = stats_.bytes_in,
            .bytes_out = stats_.bytes_out,
            .packets_in = stats_.packets_in,
            .packets_out = stats_.packets_out,
            .duration_sec = stats_.duration_sec,
            .reconnect_count = stats_.reconnect_count
        };
        event_bus_->publish(std::move(evt));
    }

    void publish_health() {
        if (!event_bus_) return;
        VpnHealthCheckEvent evt{
            .connection_id = id(),
            .type = type(),
            .healthy = health_.healthy,
            .latency_ms = health_.latency_ms,
            .consecutive_failures = health_.consecutive_failures
        };
        event_bus_->publish(std::move(evt));
    }
};

}
