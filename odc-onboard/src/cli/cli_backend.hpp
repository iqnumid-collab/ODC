#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <vector>

namespace odc {

struct CliCommandResult {
    bool success;
    std::string message;
    nlohmann::json data;
};

class CliBackend {
public:
    virtual ~CliBackend() = default;

    virtual CliCommandResult vpn_list() = 0;
    virtual CliCommandResult vpn_status(const std::string& id) = 0;
    virtual CliCommandResult vpn_start(const std::string& id) = 0;
    virtual CliCommandResult vpn_stop(const std::string& id) = 0;
    virtual CliCommandResult vpn_restart(const std::string& id) = 0;
    virtual CliCommandResult vpn_remove(const std::string& id) = 0;
    virtual CliCommandResult vpn_add_wireguard(const std::string& id, const std::string& config_path,
                                                const std::string& name, bool auto_start) = 0;
    virtual CliCommandResult vpn_add_openvpn(const std::string& id, const std::string& config_path,
                                              const std::string& creds_path, const std::string& name,
                                              bool auto_start) = 0;
    virtual CliCommandResult vpn_config_validate(const std::string& path) = 0;
    virtual CliCommandResult log_level(const std::string& level) = 0;
    virtual CliCommandResult metrics() = 0;
    virtual CliCommandResult check_privileges() = 0;

    virtual CliCommandResult mavlink_status() = 0;
    virtual CliCommandResult mavlink_endpoints() = 0;
    virtual CliCommandResult mavlink_stats() = 0;
    virtual CliCommandResult mavlink_start_log() = 0;
    virtual CliCommandResult mavlink_stop_log() = 0;

    virtual CliCommandResult mav_extender_list() = 0;
    virtual CliCommandResult mav_extender_status(const std::string& conn_id) = 0;
    virtual CliCommandResult mav_extender_start(const std::string& conn_id, const std::string& iface, const std::string& ip) = 0;
    virtual CliCommandResult mav_extender_stop(const std::string& conn_id) = 0;
    virtual CliCommandResult mav_extender_remove(const std::string& conn_id) = 0;
    virtual CliCommandResult mav_extender_auto(const std::string& mode) = 0;
    virtual CliCommandResult mav_extender_port(uint16_t port) = 0;

    virtual CliCommandResult rtsp_status() = 0;
    virtual CliCommandResult rtsp_start(const std::string& conn_id, const std::string& iface, const std::string& ip) = 0;
    virtual CliCommandResult rtsp_stop(const std::string& conn_id) = 0;

    virtual std::optional<std::string> poll_event() { return std::nullopt; }
    virtual bool is_remote() const { return false; }
};

}
