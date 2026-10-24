#include "mavlink/mav_router.hpp"

#include "mainloop.h"
#include "endpoint.h"
#include "logendpoint.h"
#include "common/log.h"
#include "core/event.hpp"

#include <cstring>
#include <arpa/inet.h>
#include <algorithm>

namespace odc {

class MavRouterImpl {
public:
    MavRouterImpl(const MavlinkConfig& config, EventBus& bus, const std::string& extender_id)
        : config_(config), bus_(bus), extender_id_(extender_id),
          mainloop_(std::make_unique<Mainloop>()) {}

    void run(const orch::CancellationToken& token) {
        mainloop_->open();
        setup_configuration();

        status_.running = true;
        status_.tcp_server_port = config_.tcp_server_port;
        status_.dedup_enabled = config_.dedup_period_sec > 0;
        status_.sniffer_sysid = config_.sniffer_sysid;
        status_.extender_id = extender_id_;
        status_.is_extender = !extender_id_.empty();

        publish_router_status();

        auto last_stats_publish = std::chrono::steady_clock::now();

        while (!token.is_cancelled() && mainloop_->iterate()) {
            snapshot_endpoints();

            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_publish > std::chrono::seconds(5)) {
                publish_routing_stats();
                last_stats_publish = now;
            }
        }

        mainloop_->terminate();
        status_.running = false;
        publish_router_status();
    }

    MavRouterStatus get_status() const {
        std::lock_guard lock(mutex_);
        return status_;
    }

    std::vector<MavEndpointInfo> get_endpoints() const {
        std::lock_guard lock(mutex_);
        return endpoints_;
    }

    MavRoutingStats get_stats() const {
        std::lock_guard lock(mutex_);
        return stats_;
    }

    bool start_log() {
        if (!mainloop_) return false;
        auto log_ep = mainloop_->get_log_endpoint();
        if (log_ep && log_ep->is_logging()) {
            return false;
        }
        if (log_ep) {
            log_ep->start();
            publish_log_status();
            return true;
        }
        return false;
    }

    bool stop_log() {
        if (!mainloop_) return false;
        auto log_ep = mainloop_->get_log_endpoint();
        if (log_ep && log_ep->is_logging()) {
            log_ep->stop();
            publish_log_status();
            return true;
        }
        return false;
    }

    std::string extender_id() const { return extender_id_; }
    bool is_extender() const { return !extender_id_.empty(); }

private:
    void setup_configuration() {
        Configuration cfg;
        cfg.tcp_port = config_.tcp_server_port;
        cfg.dedup_period_ms = config_.dedup_period_sec * 1000;
        cfg.sniffer_sysid = config_.sniffer_sysid;
        cfg.report_msg_statistics = config_.report_stats;
        cfg.debug_log_level = Log::Level::INFO;
        cfg.log_backend = Log::Backend::STDERR;
        cfg.device_config_dir = "/etc/odc-onboard/mavlink/devices";
        cfg.save_device_configs = true;

        if (is_extender()) {
            cfg.tcp_port = 0;
        }

        for (const auto& ep : config_.endpoints) {
            if (ep.type == "uart") {
                UartEndpointConfig uc;
                uc.name = ep.name;
                uc.device = ep.device;
                uc.baudrates = ep.baud.empty() ? std::vector<uint32_t>{57600} :
                               std::vector<uint32_t>(ep.baud.begin(), ep.baud.end());
                uc.flowcontrol = ep.flow_control;
                uc.group = ep.group;
                cfg.uart_configs.push_back(uc);
            } else if (ep.type == "udp") {
                UdpEndpointConfig uc;
                uc.name = ep.name;
                uc.address = ep.address;
                uc.port = ep.port;
                if (ep.mode == "server") uc.mode = UdpEndpointConfig::Mode::Server;
                else uc.mode = UdpEndpointConfig::Mode::Client;
                cfg.udp_configs.push_back(uc);
            } else if (ep.type == "tcp") {
                TcpEndpointConfig tc;
                tc.name = ep.name;
                tc.address = ep.address;
                tc.port = ep.port;
                tc.retry_timeout = ep.retry_timeout_sec;
                cfg.tcp_configs.push_back(tc);
            }
        }

        if (config_.logging.enabled && !is_extender()) {
            cfg.log_config.logs_dir = config_.logging.log_dir;
            if (config_.logging.mode == "while_armed")
                cfg.log_config.log_mode = LogMode::while_armed;
            else
                cfg.log_config.log_mode = LogMode::always;

            if (config_.logging.dialect == "ardupilotmega")
                cfg.log_config.mavlink_dialect = LogOptions::MavDialect::Ardupilotmega;
            else if (config_.logging.dialect == "common")
                cfg.log_config.mavlink_dialect = LogOptions::MavDialect::Common;
            else
                cfg.log_config.mavlink_dialect = LogOptions::MavDialect::Auto;

            cfg.log_config.min_free_space = config_.logging.min_free_space_kb * 1024;
            cfg.log_config.max_log_files = config_.logging.max_log_files;
            cfg.log_config.log_telemetry = config_.logging.telemetry;
        }

        mainloop_->add_endpoints(cfg);
    }

    void snapshot_endpoints() {
        if (!mainloop_) return;

        std::lock_guard lock(mutex_);

        std::vector<MavEndpointInfo> new_endpoints;
        int connected = 0;

        for (const auto& ep : mainloop_->get_endpoints()) {
            MavEndpointInfo info;
            info.name = ep->get_display_name();

            const auto& type_str = ep->get_type();
            info.type = type_str;

            if (type_str == ENDPOINT_TYPE_UART) {
                auto* uart = static_cast<UartEndpoint*>(ep.get());
                info.device_or_address = uart->get_device();
            } else if (type_str == ENDPOINT_TYPE_UDP) {
                auto* udp = static_cast<UdpEndpoint*>(ep.get());
                info.device_or_address = udp->get_address();
                info.port = static_cast<int>(udp->get_port());
            } else if (type_str == ENDPOINT_TYPE_TCP) {
                auto* tcp = static_cast<TcpEndpoint*>(ep.get());
                info.device_or_address = tcp->get_address();
                info.port = static_cast<int>(tcp->get_port());
            } else if (type_str == ENDPOINT_TYPE_LOG) {
                info.type = "log";
            }

            info.connected = ep->is_valid();
            if (info.connected) connected++;

            auto s = ep->get_statistics();
            info.messages_in = s.messages_in;
            info.messages_out = s.messages_out;
            info.messages_lost = s.messages_lost;
            info.bytes_in = s.bytes_in;
            info.bytes_out = s.bytes_out;

            new_endpoints.push_back(std::move(info));
        }

        for (const auto& new_ep : new_endpoints) {
            auto it = std::find_if(endpoints_.begin(), endpoints_.end(),
                [&new_ep](const MavEndpointInfo& old) { return old.name == new_ep.name; });

            if (it == endpoints_.end()) {
                if (new_ep.connected) {
                    publish_endpoint_status(new_ep);
                }
            } else if (it->connected != new_ep.connected) {
                publish_endpoint_status(new_ep);
            }
        }

        endpoints_ = std::move(new_endpoints);
        status_.endpoint_count = static_cast<int>(endpoints_.size());
        status_.connected_count = connected;
    }

    void publish_router_status() {
        std::lock_guard lock(mutex_);
        MavRouterStatusEvent evt;
        evt.running = status_.running;
        evt.tcp_server_port = status_.tcp_server_port;
        evt.dedup_enabled = status_.dedup_enabled;
        evt.sniffer_sysid = status_.sniffer_sysid;
        evt.endpoint_count = status_.endpoint_count;
        evt.connected_count = status_.connected_count;
        bus_.publish(Event{evt});
    }

    void publish_endpoint_status(const MavEndpointInfo& info) {
        MavEndpointStatusEvent evt;
        evt.name = info.name;
        evt.type = info.type;
        evt.device_or_address = info.device_or_address;
        evt.port = info.port;
        evt.connected = info.connected;
        evt.messages_in = info.messages_in;
        evt.messages_out = info.messages_out;
        evt.messages_lost = info.messages_lost;
        evt.bytes_in = info.bytes_in;
        evt.bytes_out = info.bytes_out;
        bus_.publish(Event{evt});
    }

    void publish_routing_stats() {
        std::lock_guard lock(mutex_);
        uint64_t total_in = 0, total_out = 0, total_lost = 0;
        for (const auto& ep : endpoints_) {
            total_in += ep.messages_in;
            total_out += ep.messages_out;
            total_lost += ep.messages_lost;
        }

        MavRoutingStatsEvent evt;
        evt.total_routed = total_out;
        evt.total_filtered = stats_.total_filtered;
        evt.total_dedup_dropped = stats_.total_dedup_dropped;
        evt.total_errors = stats_.total_errors;
        bus_.publish(Event{evt});
    }

    void publish_log_status() {
        if (!mainloop_) return;
        auto log_ep = mainloop_->get_log_endpoint();

        MavLogStatusEvent evt;
        if (log_ep) {
            evt.logging = log_ep->is_logging();
        }
        bus_.publish(Event{evt});
    }

    MavlinkConfig config_;
    EventBus& bus_;
    std::string extender_id_;
    std::unique_ptr<Mainloop> mainloop_;

    mutable std::mutex mutex_;
    MavRouterStatus status_;
    std::vector<MavEndpointInfo> endpoints_;
    MavRoutingStats stats_;
};

MavRouter::MavRouter(const MavlinkConfig& config, EventBus& bus, const std::string& extender_id)
    : impl_(std::make_unique<MavRouterImpl>(config, bus, extender_id)) {}

MavRouter::~MavRouter() = default;

void MavRouter::run(const orch::CancellationToken& token) {
    impl_->run(token);
}

MavRouterStatus MavRouter::get_status() const {
    return impl_->get_status();
}

std::vector<MavEndpointInfo> MavRouter::get_endpoints() const {
    return impl_->get_endpoints();
}

MavRoutingStats MavRouter::get_stats() const {
    return impl_->get_stats();
}

bool MavRouter::start_log() {
    return impl_->start_log();
}

bool MavRouter::stop_log() {
    return impl_->stop_log();
}

std::string MavRouter::extender_id() const {
    return impl_->extender_id();
}

bool MavRouter::is_extender() const {
    return impl_->is_extender();
}

}
