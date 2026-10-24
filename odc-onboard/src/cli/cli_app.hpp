#pragma once

#include "cli/cli_backend.hpp"
#include "core/privilege_check.hpp"
#include "core/credential_crypto.hpp"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <optional>
#include <cstdio>
#include <termios.h>
#include <unistd.h>
#include <filesystem>
#include <iomanip>
#include <sys/select.h>

namespace odc {

class CliApp {
public:
    explicit CliApp(CliBackend& backend) : backend_(backend) {}

    void run() {
        if (!json_mode_) {
            print_banner();
        }

        std::string line;
        while (true) {
            if (backend_.is_remote()) {
                drain_events();
            }

            if (!json_mode_) {
                std::cout << "odc-onboard> " << std::flush;
            }
            if (!std::getline(std::cin, line)) {
                break;
            }

            if (line.empty()) continue;

            if (line == "exit" || line == "quit") {
                break;
            }

            if (line == "help" || line == "?") {
                print_help();
                continue;
            }

            if (line == "clear") {
                std::cout << "\033[2J\033[H";
                continue;
            }

            if (line.rfind("grant-requirements", 0) == 0) {
                cmd_grant_requirements(line);
                continue;
            }

            if (line.rfind("mavlink", 0) == 0) {
                cmd_mavlink(line);
                continue;
            }

            if (line.rfind("mav extender", 0) == 0) {
                cmd_mav_extender(line);
                continue;
            }

            if (line.rfind("rtsp", 0) == 0) {
                cmd_rtsp(line);
                continue;
            }

            try {
                execute_command(line);
            } catch (const std::exception& e) {
                if (json_mode_) {
                    nlohmann::json err;
                    err["error"] = e.what();
                    std::cout << err.dump() << "\n";
                } else {
                    std::cerr << "Error: " << e.what() << "\n";
                }
            }
        }

        if (!json_mode_) {
            std::cout << "Exiting...\n";
        }
    }

    void set_json_mode(bool enabled) { json_mode_ = enabled; }

private:
    CliBackend& backend_;
    bool json_mode_{false};

    void drain_events() {
        while (auto evt = backend_.poll_event()) {
            display_event(*evt);
        }
    }

    void display_event(const std::string& json_str) {
        try {
            auto j = nlohmann::json::parse(json_str);
            std::string method = j.value("method", "");

            if (method == "vpn.state_change") {
                auto p = j["params"];
                std::cout << "\n[event] " << p.value("connection_id", "")
                          << ": " << p.value("old_state", "")
                          << " -> " << p.value("new_state", "") << "\n";
            } else if (method == "vpn.stats") {
                auto p = j["params"];
                std::cout << "\n[event] " << p.value("connection_id", "")
                          << " stats: " << p.value("bytes_in", 0) << "B in, "
                          << p.value("bytes_out", 0) << "B out\n";
            } else if (method == "vpn.health_check") {
                auto p = j["params"];
                std::cout << "\n[event] " << p.value("connection_id", "")
                          << " health: " << (p.value("healthy", false) ? "OK" : "FAIL")
                          << " latency=" << p.value("latency_ms", 0) << "ms\n";
            } else if (method == "log") {
                auto p = j["params"];
                std::cout << "\n[log:" << p.value("level", "") << "] "
                          << p.value("message", "") << "\n";
            } else if (method == "mavlink.router_status") {
                auto p = j["params"];
                std::cout << "\n[mavlink] router " << (p.value("running", false) ? "running" : "stopped")
                          << " endpoints=" << p.value("connected_count", 0)
                          << "/" << p.value("endpoint_count", 0) << "\n";
            } else if (method == "mavlink.endpoint_status") {
                auto p = j["params"];
                std::cout << "\n[mavlink] " << p.value("name", "")
                          << " " << (p.value("connected", false) ? "connected" : "disconnected")
                          << " in=" << p.value("messages_in", 0)
                          << " out=" << p.value("messages_out", 0) << "\n";
            } else if (method == "mavlink.routing_stats") {
                auto p = j["params"];
                std::cout << "\n[mavlink stats] routed=" << p.value("total_routed", 0)
                          << " filtered=" << p.value("total_filtered", 0)
                          << " errors=" << p.value("total_errors", 0) << "\n";
            } else if (method == "mavlink.log_status") {
                auto p = j["params"];
                std::cout << "\n[mavlink log] " << (p.value("logging", false) ? "recording" : "idle") << "\n";
            } else if (method == "mav.extender_status") {
                auto p = j["params"];
                std::cout << "\n[extender] " << p.value("extender_id", "")
                          << " " << (p.value("connected", false) ? "connected" : "disconnected")
                          << " target=" << p.value("target_ip", "")
                          << ":" << p.value("target_port", 0) << "\n";
            } else if (method == "vpn.interface_event") {
                auto p = j["params"];
                std::cout << "\n[iface] " << p.value("connection_id", "")
                          << " " << p.value("interface_name", "")
                          << " " << p.value("action", "") << "\n";
            }
            std::cout << "odc-onboard> " << std::flush;
        } catch (...) {
        }
    }

    void print_banner() {
        if (!json_mode_) {
            std::cout << "┌─────────────────────────────────────────────┐\n";
            if (backend_.is_remote()) {
                std::cout << "│         odc-onboard CLI v0.1.0              │\n";
                std::cout << "│  Attached to running daemon                 │\n";
            } else {
                std::cout << "│         odc-onboard CLI v0.1.0              │\n";
                std::cout << "│  Standalone mode (no daemon detected)       │\n";
            }
            std::cout << "│  Type 'help' for available commands         │\n";
            std::cout << "└─────────────────────────────────────────────┘\n\n";
        }
    }

    void print_help() {
        std::cout << "\nAvailable commands:\n";
        std::cout << "  System:\n";
        std::cout << "    grant-requirements [password]   Set up privileges, dirs, and permissions\n";
        std::cout << "    check-privileges              Check CAP_NET_ADMIN privileges\n";
        std::cout << "    log level <level>             Set log level (trace/debug/info/warn/error)\n";
        std::cout << "    metrics                       Show thread orchestrator metrics\n";
        std::cout << "\n";
        std::cout << "  VPN Management:\n";
        std::cout << "    vpn list                      List all VPN connections\n";
        std::cout << "    vpn status <id>               Show connection status\n";
        std::cout << "    vpn start <id>                Start a connection\n";
        std::cout << "    vpn stop <id>                 Stop a connection\n";
        std::cout << "    vpn restart <id>              Restart a connection\n";
        std::cout << "    vpn remove <id>               Remove a connection\n";
        std::cout << "    vpn edit <id>                 Edit a connection's config\n";
        std::cout << "\n";
        std::cout << "  VPN Add:\n";
        std::cout << "    vpn add wireguard --id <id> --config <path> [--name <name>] [--auto-start]\n";
        std::cout << "    vpn add openvpn --id <id> --config <path> [--creds <path>] [--name <name>] [--auto-start]\n";
        std::cout << "\n";
        std::cout << "  VPN Config:\n";
        std::cout << "    vpn config validate <path>    Validate a VPN config file\n";
        std::cout << "\n";
        std::cout << "  MAVLink Management:\n";
        std::cout << "    mavlink status                Show MAVLink router status\n";
        std::cout << "    mavlink endpoints             List endpoints and traffic statistics\n";
        std::cout << "    mavlink stats                 Show aggregate routing statistics\n";
        std::cout << "    mavlink log start             Start flight data logging\n";
        std::cout << "    mavlink log stop              Stop flight data logging\n";
        std::cout << "    mavlink monitor               Real-time MAVLink event monitor\n";
        std::cout << "\n";
        std::cout << "  MAVLink Extender (VPN-linked):\n";
        std::cout << "    mav extender list             List all active extenders\n";
        std::cout << "    mav extender status <id>      Show extender status\n";
        std::cout << "    mav extender start <id> <if> <ip>  Start extender manually\n";
        std::cout << "    mav extender stop <id>        Stop extender\n";
        std::cout << "    mav extender remove <id>      Remove extender\n";
        std::cout << "    mav extender auto <on|off>    Toggle auto-spawning\n";
        std::cout << "    mav extender port <port>      Set UDP peer port\n";
        std::cout << "\n";
        std::cout << "  RTSP Compressor:\n";
        std::cout << "    rtsp status                   List compressor instances\n";
        std::cout << "    rtsp start <id> <iface> <ip>  Start compressor for VPN\n";
        std::cout << "    rtsp stop <id>                Stop compressor\n";
        std::cout << "\n";
        std::cout << "  Output:\n";
        std::cout << "    --json                        Enable JSON output mode\n";
        std::cout << "    --no-json                     Disable JSON output mode\n";
        std::cout << "\n";
        std::cout << "  Other:\n";
        std::cout << "    clear                         Clear screen\n";
        std::cout << "    help / ?                      Show this help\n";
        std::cout << "    exit / quit                   Exit console\n";
        std::cout << "\n";
    }

    void execute_command(const std::string& line) {
        std::istringstream iss(line);
        std::vector<std::string> tokens{
            std::istream_iterator<std::string>{iss},
            std::istream_iterator<std::string>{}
        };

        if (tokens.empty()) return;

        if (tokens[0] == "--json") {
            json_mode_ = true;
            std::cout << "JSON output mode enabled\n";
            return;
        }
        if (tokens[0] == "--no-json") {
            json_mode_ = false;
            std::cout << "JSON output mode disabled\n";
            return;
        }

        if (tokens[0] == "check-privileges") {
            cmd_check_privileges();
        } else if (tokens[0] == "log") {
            cmd_log(tokens);
        } else if (tokens[0] == "metrics") {
            cmd_metrics();
        } else if (tokens[0] == "vpn") {
            cmd_vpn(tokens);
        } else if (tokens[0] == "mavlink") {
            cmd_mavlink(line);
        } else if (tokens[0] == "mav") {
            cmd_mav_extender(line);
        } else if (tokens[0] == "rtsp") {
            cmd_rtsp(line);
        } else {
            std::cerr << "Unknown command: " << tokens[0] << "\n";
        }
    }

    void cmd_check_privileges() {
        auto result = backend_.check_privileges();
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            auto is_root = result.data.value("is_root", false);
            auto has_net_admin = result.data.value("has_net_admin", false);
            auto missing = result.data.value("missing_capabilities", std::vector<std::string>());
            auto error = result.data.value("error", "");

            std::cout << "Running as root: " << (is_root ? "YES" : "NO") << "\n";
            std::cout << "CAP_NET_ADMIN: " << (has_net_admin ? "YES" : "NO") << "\n";
            if (!missing.empty()) {
                std::cout << "Missing capabilities: ";
                for (size_t i = 0; i < missing.size(); i++) {
                    if (i > 0) std::cout << ", ";
                    std::cout << missing[i];
                }
                std::cout << "\n";
            }
            if (!error.empty()) {
                std::cout << "Error: " << error << "\n";
            }

            if (!has_net_admin && !is_root) {
                std::cout << "\nWARNING: VPN operations require CAP_NET_ADMIN or root privileges.\n";
                std::cout << "Run with: sudo ./odc-onboard\n";
                std::cout << "Or set capability: sudo setcap cap_net_admin+ep ./odc-onboard\n";
            }
        }
    }

    void cmd_grant_requirements(const std::string& line) {
        std::string password;
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        std::string remaining;
        std::getline(iss, remaining);

        if (!remaining.empty() && remaining[0] == ' ') {
            remaining = remaining.substr(1);
        }

        if (!remaining.empty()) {
            password = remaining;
        } else {
            std::cout << "Enter sudo password: " << std::flush;
            termios oldt, newt;
            tcgetattr(STDIN_FILENO, &oldt);
            newt = oldt;
            newt.c_lflag &= ~ECHO;
            tcsetattr(STDIN_FILENO, TCSANOW, &newt);
            std::getline(std::cin, password);
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            std::cout << "\n";
        }

        if (password.empty()) {
            std::cerr << "Password required\n";
            return;
        }

        std::string binary_path;
        try {
            binary_path = std::filesystem::canonical("/proc/self/exe").string();
        } catch (...) {
            binary_path = "/usr/local/bin/odc-onboard";
        }

        struct Step {
            std::string description;
            std::string command;
        };

        std::vector<Step> steps = {
            {"Setting CAP_NET_ADMIN capability on binary",
             "setcap cap_net_admin+ep " + binary_path},
            {"Creating config directory",
             "mkdir -p /etc/odc-onboard"},
            {"Setting config directory permissions (700)",
             "chmod 700 /etc/odc-onboard"},
            {"Setting config directory ownership (root:root)",
             "chown root:root /etc/odc-onboard"},
            {"Ensuring /dev/net/tun exists",
             "mkdir -p /dev/net && mknod /dev/net/tun c 10 200 2>/dev/null; chmod 600 /dev/net/tun"},
            {"Reloading systemd daemon",
             "systemctl daemon-reload 2>/dev/null"},
        };

        std::cout << "\nGranting requirements...\n\n";

        for (const auto& step : steps) {
            std::cout << "  " << step.description << "... ";
            std::string full_cmd = "echo '" + password + "' | sudo -S sh -c '" + step.command + "' 2>&1";

            char buffer[256];
            FILE* pipe = popen(full_cmd.c_str(), "r");
            if (!pipe) {
                std::cout << "\033[0;31mFAIL\033[0m (pipe error)\n";
                continue;
            }

            std::string output;
            while (fgets(buffer, sizeof(buffer), pipe)) {
                output += buffer;
            }
            int status = pclose(pipe);

            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                std::cout << "\033[0;32mOK\033[0m\n";
            } else {
                std::cout << "\033[0;31mFAIL\033[0m\n";
                if (!output.empty()) {
                    std::cout << "    " << output;
                }
            }
        }

        std::cout << "\nVerifying privileges...\n";
        auto result = check_privileges();
        if (result.has_net_admin || result.is_root) {
            std::cout << "  \033[0;32mCAP_NET_ADMIN granted successfully\033[0m\n";
        } else {
            std::cout << "  \033[0;31mPrivilege check still failing. Restart terminal or re-login.\033[0m\n";
        }
        std::cout << "\n";
    }

    void cmd_log(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3 || tokens[1] != "level") {
            std::cerr << "Usage: log level <trace|debug|info|warn|error>\n";
            return;
        }

        auto result = backend_.log_level(tokens[2]);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) {
                std::cout << result.message << "\n";
            } else {
                std::cerr << result.message << "\n";
            }
        }
    }

    void cmd_metrics() {
        auto result = backend_.metrics();
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            std::cout << "\nThread Orchestrator Metrics:\n";
            std::cout << "  Active tasks:    " << result.data.value("active_tasks", 0) << "\n";
            std::cout << "  Pending tasks:   " << result.data.value("pending_tasks", 0) << "\n";
            std::cout << "  Completed tasks: " << result.data.value("completed_tasks", 0) << "\n";
            std::cout << "  Worker count:    " << result.data.value("worker_count", 0) << "\n";
            std::cout << "\n";
        }
    }

    void cmd_vpn(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) {
            std::cerr << "Usage: vpn <list|status|start|stop|restart|remove|add|edit|config> [args...]\n";
            return;
        }

        if (tokens[1] == "list") {
            cmd_vpn_list();
        } else if (tokens[1] == "status") {
            if (tokens.size() < 3) {
                std::cerr << "Usage: vpn status <id>\n";
                return;
            }
            cmd_vpn_status(tokens[2]);
        } else if (tokens[1] == "start") {
            if (tokens.size() < 3) {
                std::cerr << "Usage: vpn start <id>\n";
                return;
            }
            cmd_vpn_start(tokens[2]);
        } else if (tokens[1] == "stop") {
            if (tokens.size() < 3) {
                std::cerr << "Usage: vpn stop <id>\n";
                return;
            }
            cmd_vpn_stop(tokens[2]);
        } else if (tokens[1] == "restart") {
            if (tokens.size() < 3) {
                std::cerr << "Usage: vpn restart <id>\n";
                return;
            }
            cmd_vpn_restart(tokens[2]);
        } else if (tokens[1] == "remove") {
            if (tokens.size() < 3) {
                std::cerr << "Usage: vpn remove <id>\n";
                return;
            }
            cmd_vpn_remove(tokens[2]);
        } else if (tokens[1] == "edit") {
            if (tokens.size() < 3) {
                std::cerr << "Usage: vpn edit <id>\n";
                return;
            }
            cmd_vpn_edit(tokens[2]);
        } else if (tokens[1] == "add") {
            cmd_vpn_add(tokens);
        } else if (tokens[1] == "config") {
            cmd_vpn_config(tokens);
        } else {
            std::cerr << "Unknown vpn command: " << tokens[1] << "\n";
        }
    }

    void cmd_vpn_list() {
        auto result = backend_.vpn_list();
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
            return;
        }

        if (!result.success || result.data.empty()) {
            std::cout << "No VPN connections configured.\n";
            return;
        }

        std::cout << "\nVPN Connections:\n";
        std::cout << std::left << std::setw(20) << "ID"
                  << std::setw(12) << "Type"
                  << std::setw(14) << "State"
                  << std::setw(10) << "Enabled"
                  << std::setw(20) << "Name"
                  << "\n";
        std::cout << std::string(76, '-') << "\n";

        for (const auto& conn : result.data) {
            std::cout << std::left << std::setw(20) << conn.value("id", "")
                      << std::setw(12) << conn.value("type", "")
                      << std::setw(14) << conn.value("state", "")
                      << std::setw(10) << (conn.value("enabled", false) ? "yes" : "no")
                      << std::setw(20) << (conn.value("name", "").empty() ? "-" : conn.value("name", ""))
                      << "\n";
        }
        std::cout << "\n";
    }

    void cmd_vpn_status(const std::string& id) {
        auto result = backend_.vpn_status(id);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
            return;
        }

        if (!result.success) {
            std::cerr << result.message << "\n";
            return;
        }

        std::cout << "\nConnection: " << result.data.value("id", "") << "\n";
        std::cout << "  Type:      " << result.data.value("type", "") << "\n";
        std::cout << "  State:     " << result.data.value("state", "") << "\n";
        std::cout << "  Enabled:   " << (result.data.value("enabled", false) ? "yes" : "no") << "\n";

        auto name = result.data.value("name", "");
        if (!name.empty()) std::cout << "  Name:      " << name << "\n";
        auto config_path = result.data.value("config_path", "");
        if (!config_path.empty()) std::cout << "  Config:    " << config_path << "\n";

        if (result.data.contains("stats")) {
            auto stats = result.data["stats"];
            std::cout << "\n  Statistics:\n";
            std::cout << "    Bytes in:      " << stats.value("bytes_in", 0) << "\n";
            std::cout << "    Bytes out:     " << stats.value("bytes_out", 0) << "\n";
            std::cout << "    Packets in:    " << stats.value("packets_in", 0) << "\n";
            std::cout << "    Packets out:   " << stats.value("packets_out", 0) << "\n";
            std::cout << "    Duration:      " << stats.value("duration_sec", 0) << "s\n";
            std::cout << "    Reconnects:    " << stats.value("reconnect_count", 0) << "\n";
            auto last_err = stats.value("last_error", "");
            if (!last_err.empty()) std::cout << "    Last error:    " << last_err << "\n";
        }

        if (result.data.contains("health")) {
            auto health = result.data["health"];
            std::cout << "\n  Health:\n";
            std::cout << "    Healthy:       " << (health.value("healthy", false) ? "yes" : "no") << "\n";
            std::cout << "    Latency:       " << health.value("latency_ms", 0) << "ms\n";
            std::cout << "    Failures:      " << health.value("consecutive_failures", 0) << "\n";
        }
        std::cout << "\n";
    }

    void cmd_vpn_start(const std::string& id) {
        auto result = backend_.vpn_start(id);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_vpn_stop(const std::string& id) {
        auto result = backend_.vpn_stop(id);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_vpn_restart(const std::string& id) {
        auto result = backend_.vpn_restart(id);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_vpn_remove(const std::string& id) {
        auto result = backend_.vpn_remove(id);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_vpn_edit(const std::string& id) {
        auto result = backend_.vpn_status(id);
        if (!result.success) {
            if (json_mode_) {
                nlohmann::json j;
                j["error"] = result.message;
                std::cout << j.dump(2) << "\n";
            } else {
                std::cerr << result.message << "\n";
            }
            return;
        }

        if (json_mode_) {
            result.data["editable_fields"] = {"name", "enabled", "auto_start", "max_retries", "health_check"};
            result.data["instructions"] = "Edit the config file at config_path and use 'vpn restart " + id + "' to apply changes";
            std::cout << result.data.dump(2) << "\n";
        } else {
            std::cout << "\nEdit connection: " << id << "\n";
            std::cout << "  Config file: " << result.data.value("config_path", "") << "\n";
            std::cout << "\nTo edit this connection:\n";
            std::cout << "  1. Edit the config file: " << result.data.value("config_path", "") << "\n";
            std::cout << "  2. Or edit credentials file if OpenVPN: " << result.data.value("config_path", "") << ".creds\n";
            std::cout << "  3. Restart the connection: vpn restart " << id << "\n";
            std::cout << "\n";
        }
    }

    void cmd_vpn_config(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) {
            std::cerr << "Usage: vpn config validate <path>\n";
            return;
        }

        if (tokens[2] == "validate") {
            if (tokens.size() < 4) {
                std::cerr << "Usage: vpn config validate <path>\n";
                return;
            }
            cmd_vpn_config_validate(tokens[3]);
        } else {
            std::cerr << "Unknown vpn config command: " << tokens[2] << "\n";
        }
    }

    void cmd_vpn_config_validate(const std::string& path) {
        auto result = backend_.vpn_config_validate(path);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
            return;
        }

        std::cout << "\nConfig validation: " << path << "\n";
        std::cout << "Valid: " << (result.success ? "YES" : "NO") << "\n";

        if (result.data.contains("errors") && !result.data["errors"].empty()) {
            std::cout << "\nErrors:\n";
            for (const auto& err : result.data["errors"]) {
                std::cout << "  - " << err.get<std::string>() << "\n";
            }
        }

        if (result.data.contains("warnings") && !result.data["warnings"].empty()) {
            std::cout << "\nWarnings:\n";
            for (const auto& warn : result.data["warnings"]) {
                std::cout << "  - " << warn.get<std::string>() << "\n";
            }
        }
        std::cout << "\n";
    }

    void cmd_vpn_add(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) {
            std::cerr << "Usage: vpn add <wireguard|openvpn> --id <id> --config <path> [options]\n";
            return;
        }

        std::string type = tokens[2];
        std::string id, config_path, creds_path, name;
        bool auto_start = false;

        CLI::App app{"vpn add"};
        app.add_option("--id", id, "Connection ID")->required();
        app.add_option("--config", config_path, "Config file path")->required();
        app.add_option("--name", name, "Friendly name");
        app.add_option("--auto-start", auto_start, "Auto-start connection");

        if (type == "openvpn") {
            app.add_option("--creds", creds_path, "Credentials file path");
        }

        try {
            std::vector<std::string> args(tokens.begin() + 3, tokens.end());
            app.parse(args);
        } catch (const CLI::ParseError& e) {
            std::cerr << e.what() << "\n";
            return;
        }

        CliCommandResult result;
        if (type == "wireguard") {
            result = backend_.vpn_add_wireguard(id, config_path, name, auto_start);
        } else if (type == "openvpn") {
            result = backend_.vpn_add_openvpn(id, config_path, creds_path, name, auto_start);
        } else {
            std::cerr << "Unknown VPN type: " << type << " (use wireguard or openvpn)\n";
            return;
        }

        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_mavlink(const std::string& line) {
        std::istringstream iss(line);
        std::vector<std::string> tokens{
            std::istream_iterator<std::string>{iss},
            std::istream_iterator<std::string>{}
        };

        if (tokens.size() < 2) {
            cmd_mavlink_help();
            return;
        }

        if (tokens[1] == "status") {
            cmd_mavlink_status();
        } else if (tokens[1] == "endpoints") {
            cmd_mavlink_endpoints();
        } else if (tokens[1] == "stats") {
            cmd_mavlink_stats();
        } else if (tokens[1] == "log") {
            cmd_mavlink_log(tokens);
        } else if (tokens[1] == "monitor") {
            cmd_mavlink_monitor();
        } else if (tokens[1] == "help") {
            cmd_mavlink_help();
        } else {
            std::cerr << "Unknown mavlink command: " << tokens[1] << "\n";
        }
    }

    void cmd_mavlink_status() {
        auto result = backend_.mavlink_status();
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
            return;
        }

        if (!result.success) {
            std::cerr << result.message << "\n";
            return;
        }

        std::cout << "\nMAVLink Router Status:\n";
        std::cout << "  Running:       " << (result.data.value("running", false) ? "yes" : "no") << "\n";
        std::cout << "  TCP Port:      " << result.data.value("tcp_server_port", 0) << "\n";
        std::cout << "  Dedup:         " << (result.data.value("dedup_enabled", false) ? "yes" : "no") << "\n";
        std::cout << "  Sniffer SysID: " << result.data.value("sniffer_sysid", 0) << "\n";
        std::cout << "  Endpoints:     " << result.data.value("connected_count", 0)
                  << "/" << result.data.value("endpoint_count", 0) << " connected\n";
        std::cout << "\n";
    }

    void cmd_mavlink_endpoints() {
        auto result = backend_.mavlink_endpoints();
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
            return;
        }

        if (!result.success || result.data.empty()) {
            std::cout << "No MAVLink endpoints configured.\n";
            return;
        }

        std::cout << "\nMAVLink Endpoints:\n";
        std::cout << std::left << std::setw(22) << "Name"
                  << std::setw(10) << "Type"
                  << std::setw(12) << "Connected"
                  << std::setw(14) << "Msg In"
                  << std::setw(14) << "Msg Out"
                  << std::setw(14) << "Msg Lost"
                  << std::setw(14) << "Bytes In"
                  << std::setw(14) << "Bytes Out"
                  << "\n";
        std::cout << std::string(114, '-') << "\n";

        for (const auto& ep : result.data) {
            std::string addr = ep.value("device_or_address", "");
            if (ep.value("port", 0) > 0) {
                addr += ":" + std::to_string(ep.value("port", 0));
            }
            std::string name = ep.value("name", "");
            if (name.length() > 20) name = name.substr(0, 19) + "~";

            std::cout << std::left << std::setw(22) << name
                      << std::setw(10) << ep.value("type", "")
                      << std::setw(12) << (ep.value("connected", false) ? "yes" : "no")
                      << std::setw(14) << ep.value("messages_in", 0)
                      << std::setw(14) << ep.value("messages_out", 0)
                      << std::setw(14) << ep.value("messages_lost", 0)
                      << std::setw(14) << ep.value("bytes_in", 0)
                      << std::setw(14) << ep.value("bytes_out", 0)
                      << "\n";
        }
        std::cout << "\n";
    }

    void cmd_mavlink_stats() {
        auto result = backend_.mavlink_stats();
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
            return;
        }

        if (!result.success) {
            std::cerr << result.message << "\n";
            return;
        }

        std::cout << "\nMAVLink Routing Statistics:\n";
        std::cout << "  Total Routed:      " << result.data.value("total_routed", 0) << "\n";
        std::cout << "  Total Filtered:    " << result.data.value("total_filtered", 0) << "\n";
        std::cout << "  Dedup Dropped:     " << result.data.value("total_dedup_dropped", 0) << "\n";
        std::cout << "  Total Errors:      " << result.data.value("total_errors", 0) << "\n";
        std::cout << "\n";
    }

    void cmd_mavlink_log(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) {
            std::cerr << "Usage: mavlink log <start|stop>\n";
            return;
        }

        if (tokens[2] == "start") {
            cmd_mavlink_start_log();
        } else if (tokens[2] == "stop") {
            cmd_mavlink_stop_log();
        } else {
            std::cerr << "Unknown mavlink log command: " << tokens[2] << "\n";
        }
    }

    void cmd_mavlink_start_log() {
        auto result = backend_.mavlink_start_log();
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_mavlink_stop_log() {
        auto result = backend_.mavlink_stop_log();
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_mavlink_monitor() {
        if (!backend_.is_remote()) {
            std::cerr << "Monitor mode requires connection to daemon.\n";
            std::cerr << "Start daemon with: odc-onboard --daemon\n";
            std::cerr << "Then attach with: odc-onboard --cli\n";
            return;
        }

        std::cout << "\nMAVLink Event Monitor (press Ctrl+C or 'q' to exit)\n";
        std::cout << std::string(60, '-') << "\n";

        fd_set readfds;
        struct timeval tv;

        while (true) {
            drain_events();

            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);
            tv.tv_sec = 0;
            tv.tv_usec = 100000;

            int ret = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
            if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
                char c;
                if (read(STDIN_FILENO, &c, 1) > 0 && c == 'q') {
                    break;
                }
            }
        }

        std::cout << "\nMonitor exited.\n";
    }

    void cmd_mav_extender(const std::string& line) {
        std::istringstream iss(line);
        std::vector<std::string> tokens{
            std::istream_iterator<std::string>{iss},
            std::istream_iterator<std::string>{}
        };

        if (tokens.size() < 2) {
            cmd_mav_extender_help();
            return;
        }

        std::string sub = tokens[1];
        if (sub == "extender") {
            if (tokens.size() < 3) {
                cmd_mav_extender_help();
                return;
            }
            std::string action = tokens[2];
            if (action == "list") {
                cmd_mav_extender_list();
            } else if (action == "status") {
                if (tokens.size() < 4) {
                    std::cerr << "Usage: mav extender status <conn_id>\n";
                    return;
                }
                cmd_mav_extender_status(tokens[3]);
            } else if (action == "start") {
                if (tokens.size() < 6) {
                    std::cerr << "Usage: mav extender start <conn_id> <iface> <ip>\n";
                    return;
                }
                cmd_mav_extender_start(tokens[3], tokens[4], tokens[5]);
            } else if (action == "stop") {
                if (tokens.size() < 4) {
                    std::cerr << "Usage: mav extender stop <conn_id>\n";
                    return;
                }
                cmd_mav_extender_stop(tokens[3]);
            } else if (action == "remove") {
                if (tokens.size() < 4) {
                    std::cerr << "Usage: mav extender remove <conn_id>\n";
                    return;
                }
                cmd_mav_extender_remove(tokens[3]);
            } else if (action == "auto") {
                if (tokens.size() < 4) {
                    std::cerr << "Usage: mav extender auto <on|off>\n";
                    return;
                }
                cmd_mav_extender_auto(tokens[3]);
            } else if (action == "port") {
                if (tokens.size() < 4) {
                    std::cerr << "Usage: mav extender port <port>\n";
                    return;
                }
                try {
                    uint16_t port = static_cast<uint16_t>(std::stoul(tokens[3]));
                    cmd_mav_extender_port(port);
                } catch (...) {
                    std::cerr << "Invalid port number: " << tokens[3] << "\n";
                }
            } else if (action == "help") {
                cmd_mav_extender_help();
            } else {
                std::cerr << "Unknown mav extender command: " << action << "\n";
            }
        } else if (sub == "help") {
            cmd_mav_extender_help();
        } else {
            std::cerr << "Unknown mav command: " << sub << "\n";
        }
    }

    void cmd_mav_extender_list() {
        auto result = backend_.mav_extender_list();
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
            return;
        }
        if (!result.success || result.data.empty()) {
            std::cout << "No MAVLink extenders active.\n";
            return;
        }
        std::cout << "\nMAVLink Extenders:\n";
        std::cout << std::left << std::setw(18) << "Extender ID"
                  << std::setw(16) << "Connection"
                  << std::setw(12) << "Interface"
                  << std::setw(18) << "Target"
                  << std::setw(8) << "Active"
                  << std::setw(12) << "Msg In"
                  << std::setw(12) << "Msg Out"
                  << "\n";
        std::cout << std::string(96, '-') << "\n";
        for (const auto& ext : result.data) {
            std::string target = ext.value("target_ip", "") + ":" + std::to_string(ext.value("target_port", 0));
            std::cout << std::left << std::setw(18) << ext.value("extender_id", "")
                      << std::setw(16) << ext.value("vpn_connection_id", "")
                      << std::setw(12) << ext.value("vpn_interface", "")
                      << std::setw(18) << target
                      << std::setw(8) << (ext.value("active", false) ? "yes" : "no")
                      << std::setw(12) << ext.value("messages_in", 0)
                      << std::setw(12) << ext.value("messages_out", 0)
                      << "\n";
        }
        std::cout << "\n";
    }

    void cmd_mav_extender_status(const std::string& conn_id) {
        auto result = backend_.mav_extender_status(conn_id);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
            return;
        }
        if (!result.success) {
            std::cerr << result.message << "\n";
            return;
        }
        std::cout << "\nExtender Status:\n";
        std::cout << "  Extender ID:   " << result.data.value("extender_id", "") << "\n";
        std::cout << "  Connection:    " << result.data.value("vpn_connection_id", "") << "\n";
        std::cout << "  Interface:     " << result.data.value("vpn_interface", "") << "\n";
        std::cout << "  Target:        " << result.data.value("target_ip", "")
                  << ":" << result.data.value("target_port", 0) << "\n";
        std::cout << "  Active:        " << (result.data.value("active", false) ? "yes" : "no") << "\n";
        std::cout << "  Messages In:   " << result.data.value("messages_in", 0) << "\n";
        std::cout << "  Messages Out:  " << result.data.value("messages_out", 0) << "\n";
        std::cout << "\n";
    }

    void cmd_mav_extender_start(const std::string& conn_id, const std::string& iface, const std::string& ip) {
        auto result = backend_.mav_extender_start(conn_id, iface, ip);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_mav_extender_stop(const std::string& conn_id) {
        auto result = backend_.mav_extender_stop(conn_id);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_mav_extender_remove(const std::string& conn_id) {
        auto result = backend_.mav_extender_remove(conn_id);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_mav_extender_auto(const std::string& mode) {
        auto result = backend_.mav_extender_auto(mode);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_mav_extender_port(uint16_t port) {
        auto result = backend_.mav_extender_port(port);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_mav_extender_help() {
        std::cout << "\nMAVLink Extender Commands:\n";
        std::cout << "  mav extender list                          List all active extenders\n";
        std::cout << "  mav extender status <conn_id>              Show extender status\n";
        std::cout << "  mav extender start <conn_id> <iface> <ip>  Manually start extender\n";
        std::cout << "  mav extender stop <conn_id>                Stop extender\n";
        std::cout << "  mav extender remove <conn_id>              Remove extender\n";
        std::cout << "  mav extender auto <on|off>                 Enable/disable auto-spawning\n";
        std::cout << "  mav extender port <port>                   Set UDP peer port\n";
        std::cout << "\n";
    }

    void cmd_mavlink_help() {
        std::cout << "\nMAVLink Commands:\n";
        std::cout << "  mavlink status          Show MAVLink router status\n";
        std::cout << "  mavlink endpoints       List endpoints and traffic statistics\n";
        std::cout << "  mavlink stats           Show aggregate routing statistics\n";
        std::cout << "  mavlink log start       Start flight data logging\n";
        std::cout << "  mavlink log stop        Stop flight data logging\n";
        std::cout << "  mavlink monitor         Real-time MAVLink event monitor\n";
        std::cout << "\n";
    }

    void cmd_rtsp(const std::string& line) {
        std::istringstream iss(line);
        std::vector<std::string> tokens{
            std::istream_iterator<std::string>{iss},
            std::istream_iterator<std::string>{}
        };

        if (tokens.size() < 2) {
            cmd_rtsp_help();
            return;
        }

        std::string action = tokens[1];
        if (action == "status") {
            cmd_rtsp_status();
        } else if (action == "start") {
            if (tokens.size() < 5) {
                std::cerr << "Usage: rtsp start <id> <iface> <ip>\n";
                return;
            }
            cmd_rtsp_start(tokens[2], tokens[3], tokens[4]);
        } else if (action == "stop") {
            if (tokens.size() < 3) {
                std::cerr << "Usage: rtsp stop <id>\n";
                return;
            }
            cmd_rtsp_stop(tokens[2]);
        } else if (action == "help") {
            cmd_rtsp_help();
        } else {
            std::cerr << "Unknown rtsp command: " << action << "\n";
        }
    }

    void cmd_rtsp_status() {
        auto result = backend_.rtsp_status();
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
            return;
        }
        if (!result.success) {
            std::cerr << result.message << "\n";
            return;
        }
        if (result.data.is_array() && result.data.empty()) {
            std::cout << "No compressor instances active.\n";
            return;
        }
        std::cout << "\nRTSP Compressor Instances:\n";
        std::cout << "──────────────────────────────────────────────────────────────\n";
        for (const auto& j : result.data) {
            std::cout << "  connection: " << j.value("connection_id", "")
                      << "  iface: " << j.value("interface_name", "")
                      << "  ip: " << j.value("target_ip", "")
                      << "  port: " << j.value("output_port", 0)
                      << "  running: " << (j.value("running", false) ? "yes" : "no")
                      << "\n";
        }
        std::cout << "\n";
    }

    void cmd_rtsp_start(const std::string& conn_id, const std::string& iface, const std::string& ip) {
        auto result = backend_.rtsp_start(conn_id, iface, ip);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_rtsp_stop(const std::string& conn_id) {
        auto result = backend_.rtsp_stop(conn_id);
        if (json_mode_) {
            std::cout << result.data.dump(2) << "\n";
        } else {
            if (result.success) std::cout << result.message << "\n";
            else std::cerr << result.message << "\n";
        }
    }

    void cmd_rtsp_help() {
        std::cout << "\nRTSP Compressor Commands:\n";
        std::cout << "  rtsp status                     List compressor instances\n";
        std::cout << "  rtsp start <id> <iface> <ip>    Start compressor for VPN\n";
        std::cout << "  rtsp stop <id>                  Stop compressor\n";
        std::cout << "\n";
    }
};

}
