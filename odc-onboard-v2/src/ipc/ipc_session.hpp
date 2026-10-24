#pragma once

#include "ipc/jsonrpc.hpp"
#include "ipc/method_registry.hpp"
#include "core/event_bus.hpp"
#include "core/event.hpp"
#include "vpn/vpn_manager.hpp"
#include "core/log_control.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace odc::ipc {

class IpcSession {
public:
    IpcSession(int fd, MethodRegistry& registry, EventBus& bus,
               VpnManager& vpn_mgr, LogControl& log_ctrl,
               orch::ThreadOrchestrator& orch)
        : fd_(fd), registry_(registry), event_bus_(bus),
          vpn_mgr_(vpn_mgr), log_ctrl_(log_ctrl), orch_(orch),
          alive_(std::make_shared<std::atomic<bool>>(true)) {}

    ~IpcSession() {
        alive_->store(false, std::memory_order_release);
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        for (auto id : sub_ids_) {
            event_bus_.unsubscribe(id);
        }
        sub_ids_.clear();
    }

    void run() {
        char buf[4096];
        std::string buffer;

        while (true) {
            ssize_t n = ::recv(fd_, buf, sizeof(buf) - 1, 0);
            if (n <= 0) break;

            buf[n] = '\0';
            buffer += buf;

            size_t pos;
            while ((pos = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);

                if (line.empty()) continue;

                dispatch_line(line);
            }
        }
    }

private:
    int fd_;
    MethodRegistry& registry_;
    EventBus& event_bus_;
    VpnManager& vpn_mgr_;
    LogControl& log_ctrl_;
    orch::ThreadOrchestrator& orch_;
    std::vector<EventBus::SubscriptionId> sub_ids_;
    std::mutex write_mutex_;
    std::shared_ptr<std::atomic<bool>> alive_;

    void dispatch_line(const std::string& line) {
        auto req = parse_request(line);
        if (!req) {
            send_error(-1, -32700, "Parse error");
            return;
        }

        if (req->method == "subscribe") {
            handle_subscribe(*req);
            return;
        }

        if (req->method == "unsubscribe") {
            handle_unsubscribe(*req);
            return;
        }

        auto handler = registry_.get(req->method);
        if (!handler) {
            send_error(req->id.value_or(-1), -32601, "Method not found: " + req->method);
            return;
        }

        try {
            auto result = (*handler)(req->params);
            if (req->id.has_value()) {
                send_response(make_success_response(req->id.value(), result));
            }
        } catch (const std::exception& e) {
            send_error(req->id.value_or(-1), -32000, e.what());
        }
    }

    void handle_subscribe(const JsonRpcRequest& req) {
        auto alive = alive_;
        sub_ids_.push_back(event_bus_.subscribe(
            [this, alive](const Event& e) {
                if (!alive || !alive->load(std::memory_order_acquire)) {
                    return;
                }
                std::optional<JsonRpcNotification> notif;

                if (auto* ev = std::get_if<VpnStateChangeEvent>(&e)) {
                    notif = JsonRpcNotification{};
                    notif->method = "vpn.state_change";
                    notif->params = {
                        {"connection_id", ev->connection_id},
                        {"type", to_string_view(ev->type)},
                        {"old_state", to_string_view(ev->old_state)},
                        {"new_state", to_string_view(ev->new_state)}
                    };
                } else if (auto* ev = std::get_if<VpnStatsEvent>(&e)) {
                    notif = JsonRpcNotification{};
                    notif->method = "vpn.stats";
                    notif->params = {
                        {"connection_id", ev->connection_id},
                        {"type", to_string_view(ev->type)},
                        {"bytes_in", ev->bytes_in},
                        {"bytes_out", ev->bytes_out},
                        {"packets_in", ev->packets_in},
                        {"packets_out", ev->packets_out},
                        {"duration_sec", ev->duration_sec},
                        {"reconnect_count", ev->reconnect_count}
                    };
                } else if (auto* ev = std::get_if<VpnHealthCheckEvent>(&e)) {
                    notif = JsonRpcNotification{};
                    notif->method = "vpn.health_check";
                    notif->params = {
                        {"connection_id", ev->connection_id},
                        {"type", to_string_view(ev->type)},
                        {"healthy", ev->healthy},
                        {"latency_ms", ev->latency_ms},
                        {"consecutive_failures", ev->consecutive_failures}
                    };
                } else if (auto* ev = std::get_if<LogEvent>(&e)) {
                    notif = JsonRpcNotification{};
                    notif->method = "log";
                    notif->params = {
                        {"level", to_string_view(ev->level)},
                        {"logger", ev->logger_name},
                        {"message", ev->message}
                    };
                } else if (auto* ev = std::get_if<MavRouterStatusEvent>(&e)) {
                    notif = JsonRpcNotification{};
                    notif->method = "mavlink.router_status";
                    notif->params = {
                        {"running", ev->running},
                        {"tcp_server_port", ev->tcp_server_port},
                        {"dedup_enabled", ev->dedup_enabled},
                        {"sniffer_sysid", ev->sniffer_sysid},
                        {"endpoint_count", ev->endpoint_count},
                        {"connected_count", ev->connected_count}
                    };
                } else if (auto* ev = std::get_if<MavEndpointStatusEvent>(&e)) {
                    notif = JsonRpcNotification{};
                    notif->method = "mavlink.endpoint_status";
                    notif->params = {
                        {"name", ev->name},
                        {"type", ev->type},
                        {"device_or_address", ev->device_or_address},
                        {"port", ev->port},
                        {"connected", ev->connected},
                        {"messages_in", ev->messages_in},
                        {"messages_out", ev->messages_out},
                        {"messages_lost", ev->messages_lost},
                        {"bytes_in", ev->bytes_in},
                        {"bytes_out", ev->bytes_out}
                    };
                } else if (auto* ev = std::get_if<MavRoutingStatsEvent>(&e)) {
                    notif = JsonRpcNotification{};
                    notif->method = "mavlink.routing_stats";
                    notif->params = {
                        {"total_routed", ev->total_routed},
                        {"total_filtered", ev->total_filtered},
                        {"total_dedup_dropped", ev->total_dedup_dropped},
                        {"total_errors", ev->total_errors}
                    };
                } else if (auto* ev = std::get_if<MavLogStatusEvent>(&e)) {
                    notif = JsonRpcNotification{};
                    notif->method = "mavlink.log_status";
                    notif->params = {
                        {"logging", ev->logging},
                        {"log_file", ev->log_file},
                        {"log_size_bytes", ev->log_size_bytes}
                    };
                } else if (auto* ev = std::get_if<RtspCompressorEvent>(&e)) {
                    notif = JsonRpcNotification{};
                    notif->method = "rtsp.event";
                    notif->params = {
                        {"connection_id", ev->connection_id},
                        {"type", to_string_view(ev->type)},
                        {"detail", ev->detail}
                    };
                } else if (auto* ev = std::get_if<RtspCompressorStatusEvent>(&e)) {
                    notif = JsonRpcNotification{};
                    notif->method = "rtsp.status";
                    notif->params = {
                        {"connection_id", ev->connection_id},
                        {"interface_name", ev->interface_name},
                        {"target_ip", ev->target_ip},
                        {"running", ev->running},
                        {"reconnecting", ev->reconnecting},
                        {"abr_enabled", ev->abr_enabled},
                        {"abr_bitrate_kbps", ev->abr_bitrate_kbps},
                        {"bitrate_kbps", ev->bitrate_kbps},
                        {"fps", ev->fps},
                        {"compression_ratio", ev->compression_ratio},
                        {"connected_clients", ev->connected_clients},
                        {"worst_client_loss_pct", ev->worst_client_loss_pct},
                        {"error_message", ev->error_message}
                    };
                }

                if (notif) {
                    send_notification(*notif);
                }
            }));

        if (req.id.has_value()) {
            send_response(make_success_response(req.id.value(), nlohmann::json{{"subscribed", true}}));
        }
    }

    void handle_unsubscribe(const JsonRpcRequest& req) {
        for (auto id : sub_ids_) {
            event_bus_.unsubscribe(id);
        }
        sub_ids_.clear();
        if (req.id.has_value()) {
            send_response(make_success_response(req.id.value(), nlohmann::json{{"unsubscribed", true}}));
        }
    }

    void send_response(const JsonRpcResponse& resp) {
        std::lock_guard lock(write_mutex_);
        if (fd_ < 0) return;
        std::string data = serialize_response(resp);
        size_t total_sent = 0;
        while (total_sent < data.size()) {
            ssize_t sent = ::send(fd_, data.data() + total_sent, data.size() - total_sent, MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EPIPE || errno == ECONNRESET) {
                    fd_ = -1;
                }
                return;
            }
            if (sent == 0) break;
            total_sent += sent;
        }
    }

    void send_notification(const JsonRpcNotification& notif) {
        std::lock_guard lock(write_mutex_);
        if (fd_ < 0) return;
        std::string data = serialize_notification(notif);
        ssize_t sent = ::send(fd_, data.data(), data.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EPIPE || errno == ECONNRESET) {
                fd_ = -1;
            }
        }
    }

    void send_error(int64_t id, int code, const std::string& msg) {
        std::lock_guard lock(write_mutex_);
        if (fd_ < 0) return;
        auto resp = make_error_response(id, code, msg);
        std::string data = serialize_response(resp);
        size_t total_sent = 0;
        while (total_sent < data.size()) {
            ssize_t sent = ::send(fd_, data.data() + total_sent, data.size() - total_sent, MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EPIPE || errno == ECONNRESET) {
                    fd_ = -1;
                }
                return;
            }
            if (sent == 0) break;
            total_sent += sent;
        }
    }
};

}
