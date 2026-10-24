#pragma once

#include "cli/cli_backend.hpp"
#include "ipc/ipc_client.hpp"

#include <nlohmann/json.hpp>
#include <iomanip>

namespace odc {

class CliRemoteBackend : public CliBackend {
public:
    explicit CliRemoteBackend(ipc::IpcClient& client) : client_(client) {}

    bool is_remote() const override { return true; }

    CliCommandResult vpn_list() override {
        try {
            auto data = client_.send_request("vpn.list");
            return {true, "", data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult vpn_status(const std::string& id) override {
        try {
            auto data = client_.send_request("vpn.status", {{"id", id}});
            return {true, "", data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult vpn_start(const std::string& id) override {
        try {
            auto data = client_.send_request("vpn.start", {{"id", id}});
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult vpn_stop(const std::string& id) override {
        try {
            auto data = client_.send_request("vpn.stop", {{"id", id}});
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult vpn_restart(const std::string& id) override {
        try {
            auto data = client_.send_request("vpn.restart", {{"id", id}});
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult vpn_remove(const std::string& id) override {
        try {
            auto data = client_.send_request("vpn.remove", {{"id", id}});
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult vpn_add_wireguard(const std::string& id, const std::string& config_path,
                                        const std::string& name, bool auto_start) override {
        try {
            nlohmann::json params = {
                {"type", "wireguard"},
                {"id", id},
                {"config_path", config_path},
                {"name", name},
                {"auto_start", auto_start}
            };
            auto data = client_.send_request("vpn.add", params);
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult vpn_add_openvpn(const std::string& id, const std::string& config_path,
                                      const std::string& creds_path, const std::string& name,
                                      bool auto_start) override {
        try {
            nlohmann::json params = {
                {"type", "openvpn"},
                {"id", id},
                {"config_path", config_path},
                {"name", name},
                {"auto_start", auto_start}
            };
            if (!creds_path.empty()) {
                params["creds_path"] = creds_path;
            }
            auto data = client_.send_request("vpn.add", params);
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult vpn_config_validate(const std::string& path) override {
        try {
            auto data = client_.send_request("vpn.config.validate", {{"path", path}});
            bool valid = data.value("valid", false);
            return {valid, valid ? "Config validation passed" : "Config validation failed", data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult log_level(const std::string& level) override {
        try {
            auto data = client_.send_request("log.level", {{"level", level}});
            return {true, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult metrics() override {
        try {
            auto data = client_.send_request("metrics");
            return {true, "", data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult check_privileges() override {
        try {
            auto data = client_.send_request("privileges.check");
            bool ok = data.value("has_net_admin", false) || data.value("is_root", false);
            return {ok, "", data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    std::optional<std::string> poll_event() override {
        return client_.poll_events();
    }

    CliCommandResult mavlink_status() override {
        try {
            auto data = client_.send_request("mavlink.status");
            return {true, "", data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult mavlink_endpoints() override {
        try {
            auto data = client_.send_request("mavlink.endpoints");
            return {true, "", data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult mavlink_stats() override {
        try {
            auto data = client_.send_request("mavlink.stats");
            return {true, "", data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult mavlink_start_log() override {
        try {
            auto data = client_.send_request("mavlink.start_log");
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult mavlink_stop_log() override {
        try {
            auto data = client_.send_request("mavlink.stop_log");
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult mav_extender_list() override {
        try {
            auto data = client_.send_request("mav.extender.list");
            return {true, "", data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult mav_extender_status(const std::string& conn_id) override {
        try {
            auto data = client_.send_request("mav.extender.status", {{"connection_id", conn_id}});
            return {true, "", data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult mav_extender_start(const std::string& conn_id, const std::string& iface, const std::string& ip) override {
        try {
            auto data = client_.send_request("mav.extender.start", {{"connection_id", conn_id}, {"interface", iface}, {"ip", ip}});
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult mav_extender_stop(const std::string& conn_id) override {
        try {
            auto data = client_.send_request("mav.extender.stop", {{"connection_id", conn_id}});
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult mav_extender_remove(const std::string& conn_id) override {
        try {
            auto data = client_.send_request("mav.extender.remove", {{"connection_id", conn_id}});
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult mav_extender_auto(const std::string& mode) override {
        try {
            auto data = client_.send_request("mav.extender.auto", {{"mode", mode}});
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult mav_extender_port(uint16_t port) override {
        try {
            auto data = client_.send_request("mav.extender.port", {{"port", port}});
            return {true, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult rtsp_status() override {
        try {
            auto data = client_.send_request("rtsp.status");
            return {true, "", data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult rtsp_start(const std::string& conn_id, const std::string& iface, const std::string& ip) override {
        try {
            auto data = client_.send_request("rtsp.start", {{"connection_id", conn_id}, {"interface", iface}, {"ip", ip}});
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult rtsp_stop(const std::string& conn_id) override {
        try {
            auto data = client_.send_request("rtsp.stop", {{"connection_id", conn_id}});
            bool ok = data.value("success", false);
            return {ok, data.value("message", ""), data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

    CliCommandResult graph_request(const std::string& method, const nlohmann::json& params = nlohmann::json::object()) override {
        try {
            auto data = client_.send_request(method, params);
            bool ok = true;
            std::string msg;
            if (data.contains("success")) {
                ok = data.value("success", false);
                if (!ok) {
                    msg = data.value("error", "");
                }
            } else if (data.contains("valid")) {
                ok = data.value("valid", false);
                if (!ok) {
                    msg = "Validation failed";
                }
            }
            return {ok, msg, data};
        } catch (const std::exception& e) {
            return {false, e.what(), {}};
        }
    }

private:
    ipc::IpcClient& client_;
};

}
