#pragma once

#include "cli/cli_backend.hpp"
#include "vpn/vpn_manager.hpp"
#include "vpn/vpn_config.hpp"
#include "core/privilege_check.hpp"
#include "core/log_control.hpp"
#include "thread_orchestrator/orchestrator.hpp"
#include "mavlink/mav_router.hpp"
#include "mavlink/mav_extender_manager.hpp"
#include "rtsp/rtsp_compressor_manager.hpp"

#include <nlohmann/json.hpp>
#include <iomanip>

namespace odc {

class CliLocalBackend : public CliBackend {
public:
    CliLocalBackend(VpnManager& vpn_mgr, LogControl& log_ctrl, orch::ThreadOrchestrator& orch, MavRouter* mav_router = nullptr, MavExtenderManager* mav_extender_mgr = nullptr, RtspCompressorManager* rtsp_compressor = nullptr)
        : vpn_mgr_(vpn_mgr), log_ctrl_(log_ctrl), orch_(orch), mav_router_(mav_router), mav_extender_mgr_(mav_extender_mgr), rtsp_compressor_(rtsp_compressor) {}

    CliCommandResult vpn_list() override {
        auto connections = vpn_mgr_.list_connections();
        nlohmann::json data = nlohmann::json::array();
        for (const auto& conn : connections) {
            nlohmann::json c;
            c["id"] = conn.id;
            c["type"] = to_string_view(conn.type);
            c["state"] = to_string_view(conn.state);
            c["enabled"] = conn.enabled;
            c["name"] = conn.name;
            c["config_path"] = conn.config_path;
            c["stats"] = {
                {"bytes_in", conn.stats.bytes_in},
                {"bytes_out", conn.stats.bytes_out},
                {"packets_in", conn.stats.packets_in},
                {"packets_out", conn.stats.packets_out},
                {"duration_sec", conn.stats.duration_sec},
                {"reconnect_count", conn.stats.reconnect_count}
            };
            c["health"] = {
                {"healthy", conn.health.healthy},
                {"latency_ms", conn.health.latency_ms},
                {"consecutive_failures", conn.health.consecutive_failures}
            };
            data.push_back(c);
        }
        return {true, "", data};
    }

    CliCommandResult vpn_status(const std::string& id) override {
        auto info = vpn_mgr_.get_connection(id);
        if (!info) {
            return {false, "Connection not found: " + id, {}};
        }
        nlohmann::json data;
        data["id"] = info->id;
        data["type"] = to_string_view(info->type);
        data["state"] = to_string_view(info->state);
        data["enabled"] = info->enabled;
        data["name"] = info->name;
        data["config_path"] = info->config_path;
        data["stats"] = {
            {"bytes_in", info->stats.bytes_in},
            {"bytes_out", info->stats.bytes_out},
            {"packets_in", info->stats.packets_in},
            {"packets_out", info->stats.packets_out},
            {"duration_sec", info->stats.duration_sec},
            {"reconnect_count", info->stats.reconnect_count},
            {"last_error", info->stats.last_error}
        };
        data["health"] = {
            {"healthy", info->health.healthy},
            {"latency_ms", info->health.latency_ms},
            {"consecutive_failures", info->health.consecutive_failures}
        };
        return {true, "", data};
    }

    CliCommandResult vpn_start(const std::string& id) override {
        bool ok = vpn_mgr_.start_connection(id);
        return {ok, ok ? "Starting connection: " + id : "Failed to start connection: " + id, {{"success", ok}}};
    }

    CliCommandResult vpn_stop(const std::string& id) override {
        bool ok = vpn_mgr_.stop_connection(id);
        return {ok, ok ? "Stopping connection: " + id : "Failed to stop connection: " + id, {{"success", ok}}};
    }

    CliCommandResult vpn_restart(const std::string& id) override {
        bool ok = vpn_mgr_.restart_connection(id);
        return {ok, ok ? "Restarting connection: " + id : "Failed to restart connection: " + id, {{"success", ok}}};
    }

    CliCommandResult vpn_remove(const std::string& id) override {
        bool ok = vpn_mgr_.remove_connection(id);
        return {ok, ok ? "Removed connection: " + id : "Failed to remove connection: " + id, {{"success", ok}}};
    }

    CliCommandResult vpn_add_wireguard(const std::string& id, const std::string& config_path,
                                        const std::string& name, bool auto_start) override {
        WireGuardConnectionConfig cfg;
        cfg.id = id;
        cfg.config_path = config_path;
        cfg.name = name;
        cfg.auto_start = auto_start;
        bool ok = vpn_mgr_.add_wireguard(cfg);
        return {ok, ok ? "Added WireGuard connection: " + id : "Failed to add WireGuard connection: " + id, {{"success", ok}, {"id", id}}};
    }

    CliCommandResult vpn_add_openvpn(const std::string& id, const std::string& config_path,
                                      const std::string& creds_path, const std::string& name,
                                      bool auto_start) override {
        OpenVPNConnectionConfig cfg;
        cfg.id = id;
        cfg.config_path = config_path;
        cfg.credentials_path = creds_path;
        cfg.name = name;
        cfg.auto_start = auto_start;
        bool ok = vpn_mgr_.add_openvpn(cfg);
        return {ok, ok ? "Added OpenVPN connection: " + id : "Failed to add OpenVPN connection: " + id, {{"success", ok}, {"id", id}}};
    }

    CliCommandResult vpn_config_validate(const std::string& path) override {
        std::filesystem::path config_path(path);
        bool valid = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;

        if (!std::filesystem::exists(config_path)) {
            errors.push_back("File does not exist: " + path);
            valid = false;
        } else {
            try {
                std::ifstream ifs(config_path);
                std::string content((std::istreambuf_iterator<char>(ifs)),
                                    std::istreambuf_iterator<char>());

                if (content.empty()) {
                    errors.push_back("Config file is empty");
                    valid = false;
                }

                bool is_wg = content.find("[Interface]") != std::string::npos;
                bool is_ovpn = content.find("dev ") != std::string::npos ||
                               content.find("remote ") != std::string::npos;

                if (!is_wg && !is_ovpn) {
                    warnings.push_back("Config file does not appear to be WireGuard or OpenVPN format");
                }

                if (is_wg) {
                    if (content.find("[Interface]") == std::string::npos) {
                        errors.push_back("Missing [Interface] section");
                        valid = false;
                    }
                    if (content.find("[Peer]") == std::string::npos) {
                        errors.push_back("Missing [Peer] section");
                        valid = false;
                    }
                    if (content.find("PrivateKey") == std::string::npos) {
                        errors.push_back("Missing PrivateKey in [Interface]");
                        valid = false;
                    }
                }

                auto perm_result = check_config_file_permissions(config_path);
                if (!perm_result.valid) {
                    warnings.push_back("Permission issue: " + perm_result.error_message);
                }
            } catch (const std::exception& e) {
                errors.push_back(std::string("Error reading file: ") + e.what());
                valid = false;
            }
        }

        nlohmann::json data;
        data["valid"] = valid;
        data["path"] = path;
        data["errors"] = errors;
        data["warnings"] = warnings;
        return {valid, valid ? "Config validation passed" : "Config validation failed", data};
    }

    CliCommandResult log_level(const std::string& level) override {
        LogLevel lvl;
        std::string l = level;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);

        if (l == "trace") lvl = LogLevel::Trace;
        else if (l == "debug") lvl = LogLevel::Debug;
        else if (l == "info") lvl = LogLevel::Info;
        else if (l == "warn") lvl = LogLevel::Warn;
        else if (l == "error") lvl = LogLevel::Error;
        else return {false, "Invalid log level: " + level, {}};

        log_ctrl_.set_default_level(lvl);
        return {true, "Log level set to: " + std::string(to_string_view(lvl)), {{"level", to_string_view(lvl)}}};
    }

    CliCommandResult metrics() override {
        auto m = orch_.get_metrics();
        nlohmann::json data;
        data["active_tasks"] = m.active_tasks;
        data["pending_tasks"] = m.pending_tasks;
        data["completed_tasks"] = m.completed_tasks;
        data["worker_count"] = m.worker_count;
        return {true, "", data};
    }

    CliCommandResult check_privileges() override {
        auto priv = odc::check_privileges();
        nlohmann::json data;
        data["is_root"] = priv.is_root;
        data["has_net_admin"] = priv.has_net_admin;
        data["missing_capabilities"] = priv.missing_capabilities;
        if (!priv.error_message.empty()) data["error"] = priv.error_message;
        bool ok = priv.has_net_admin || priv.is_root;
        return {ok, "", data};
    }

    CliCommandResult mavlink_status() override {
        if (!mav_router_) return {false, "MAVLink router not configured", {}};
        auto s = mav_router_->get_status();
        nlohmann::json data;
        data["running"] = s.running;
        data["tcp_server_port"] = s.tcp_server_port;
        data["dedup_enabled"] = s.dedup_enabled;
        data["sniffer_sysid"] = s.sniffer_sysid;
        data["endpoint_count"] = s.endpoint_count;
        data["connected_count"] = s.connected_count;
        return {true, "", data};
    }

    CliCommandResult mavlink_endpoints() override {
        if (!mav_router_) return {false, "MAVLink router not configured", {}};
        auto eps = mav_router_->get_endpoints();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& ep : eps) {
            nlohmann::json j;
            j["name"] = ep.name;
            j["type"] = ep.type;
            j["device_or_address"] = ep.device_or_address;
            j["port"] = ep.port;
            j["connected"] = ep.connected;
            j["messages_in"] = ep.messages_in;
            j["messages_out"] = ep.messages_out;
            j["messages_lost"] = ep.messages_lost;
            j["bytes_in"] = ep.bytes_in;
            j["bytes_out"] = ep.bytes_out;
            arr.push_back(j);
        }
        return {true, "", arr};
    }

    CliCommandResult mavlink_stats() override {
        if (!mav_router_) return {false, "MAVLink router not configured", {}};
        auto s = mav_router_->get_stats();
        nlohmann::json data;
        data["total_routed"] = s.total_routed;
        data["total_filtered"] = s.total_filtered;
        data["total_dedup_dropped"] = s.total_dedup_dropped;
        data["total_errors"] = s.total_errors;
        return {true, "", data};
    }

    CliCommandResult mavlink_start_log() override {
        if (!mav_router_) return {false, "MAVLink router not configured", {}};
        bool ok = mav_router_->start_log();
        return {ok, ok ? "MAVLink logging started" : "Failed to start MAVLink logging", {{"success", ok}}};
    }

    CliCommandResult mavlink_stop_log() override {
        if (!mav_router_) return {false, "MAVLink router not configured", {}};
        bool ok = mav_router_->stop_log();
        return {ok, ok ? "MAVLink logging stopped" : "Failed to stop MAVLink logging", {{"success", ok}}};
    }

    CliCommandResult mav_extender_list() override {
        if (!mav_extender_mgr_) return {false, "Extender manager not configured", {}};
        auto extenders = mav_extender_mgr_->list_extenders();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& ext : extenders) {
            nlohmann::json j;
            j["extender_id"] = ext.extender_id;
            j["vpn_connection_id"] = ext.vpn_connection_id;
            j["vpn_interface"] = ext.vpn_interface;
            j["target_ip"] = ext.target_ip;
            j["target_port"] = ext.target_port;
            j["active"] = ext.active;
            j["messages_in"] = ext.messages_in;
            j["messages_out"] = ext.messages_out;
            arr.push_back(j);
        }
        return {true, "", arr};
    }

    CliCommandResult mav_extender_status(const std::string& conn_id) override {
        if (!mav_extender_mgr_) return {false, "Extender manager not configured", {}};
        auto extenders = mav_extender_mgr_->list_extenders();
        for (const auto& ext : extenders) {
            if (ext.vpn_connection_id == conn_id) {
                nlohmann::json j;
                j["extender_id"] = ext.extender_id;
                j["vpn_connection_id"] = ext.vpn_connection_id;
                j["vpn_interface"] = ext.vpn_interface;
                j["target_ip"] = ext.target_ip;
                j["target_port"] = ext.target_port;
                j["active"] = ext.active;
                j["messages_in"] = ext.messages_in;
                j["messages_out"] = ext.messages_out;
                return {true, "", j};
            }
        }
        return {false, "No extender found for connection: " + conn_id, {}};
    }

    CliCommandResult mav_extender_start(const std::string& conn_id, const std::string& iface, const std::string& ip) override {
        if (!mav_extender_mgr_) return {false, "Extender manager not configured", {}};
        bool ok = mav_extender_mgr_->start_extender(conn_id, iface, ip);
        return {ok, ok ? "Started extender for: " + conn_id : "Failed to start extender for: " + conn_id, {{"success", ok}}};
    }

    CliCommandResult mav_extender_stop(const std::string& conn_id) override {
        if (!mav_extender_mgr_) return {false, "Extender manager not configured", {}};
        bool ok = mav_extender_mgr_->stop_extender(conn_id);
        return {ok, ok ? "Stopped extender for: " + conn_id : "Failed to stop extender for: " + conn_id, {{"success", ok}}};
    }

    CliCommandResult mav_extender_remove(const std::string& conn_id) override {
        if (!mav_extender_mgr_) return {false, "Extender manager not configured", {}};
        bool ok = mav_extender_mgr_->remove_extender(conn_id);
        return {ok, ok ? "Removed extender for: " + conn_id : "Failed to remove extender for: " + conn_id, {{"success", ok}}};
    }

    CliCommandResult mav_extender_auto(const std::string& mode) override {
        if (!mav_extender_mgr_) return {false, "Extender manager not configured", {}};
        if (mode == "on") {
            mav_extender_mgr_->set_auto_enabled(true);
            return {true, "Auto extender spawning enabled", {{"enabled", true}}};
        } else if (mode == "off") {
            mav_extender_mgr_->set_auto_enabled(false);
            return {true, "Auto extender spawning disabled", {{"enabled", false}}};
        }
        return {false, "Invalid mode: " + mode + " (use 'on' or 'off')", {}};
    }

    CliCommandResult mav_extender_port(uint16_t port) override {
        if (!mav_extender_mgr_) return {false, "Extender manager not configured", {}};
        mav_extender_mgr_->set_udp_port(port);
        return {true, "Extender UDP port set to: " + std::to_string(port), {{"port", port}}};
    }

    CliCommandResult rtsp_status() override {
        std::string msg = "RTSP compressor not configured";
        if (rtsp_compressor_) {
            auto compressors = rtsp_compressor_->list_compressors();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& c : compressors) {
                nlohmann::json j;
                j["connection_id"] = c.connection_id;
                j["interface_name"] = c.interface_name;
                j["target_ip"] = c.target_ip;
                j["output_port"] = c.output_port;
                j["running"] = c.running;
                arr.push_back(j);
            }
            return {true, "", arr};
        }
        return {false, msg, {}};
    }

    CliCommandResult rtsp_start(const std::string& conn_id, const std::string& iface, const std::string& ip) override {
        if (!rtsp_compressor_) return {false, "RTSP compressor not configured", {}};
        bool ok = rtsp_compressor_->start_compressor(conn_id, iface, ip);
        return {ok, ok ? "Started compressor: " + conn_id : "Failed", {{"success", ok}}};
    }

    CliCommandResult rtsp_stop(const std::string& conn_id) override {
        if (!rtsp_compressor_) return {false, "RTSP compressor not configured", {}};
        bool ok = rtsp_compressor_->stop_compressor(conn_id);
        return {ok, ok ? "Stopped compressor: " + conn_id : "Failed", {{"success", ok}}};
    }

private:
    VpnManager& vpn_mgr_;
    LogControl& log_ctrl_;
    orch::ThreadOrchestrator& orch_;
    MavRouter* mav_router_{nullptr};
    MavExtenderManager* mav_extender_mgr_{nullptr};
    RtspCompressorManager* rtsp_compressor_{nullptr};
};

}
