#pragma once

#include "core/event.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace odc {

struct RouteSafetyConfig {
    bool block_default_route{true};
    bool block_ipv6_default{true};
    uint32_t route_metric{1024};
    bool log_route_changes{true};
};

struct HealthCheckConfig {
    bool enabled{false};
    uint32_t interval_sec{30};
    std::string target_ip;
};

struct WireGuardConnectionConfig {
    std::string id;
    bool enabled{true};
    std::filesystem::path config_path;
    std::string name;
    std::string description;
    bool auto_start{false};
    uint32_t max_retries{3};
    HealthCheckConfig health_check;
    RouteSafetyConfig route_safety;
};

struct OpenVPNConnectionConfig {
    std::string id;
    bool enabled{true};
    std::filesystem::path config_path;
    std::filesystem::path credentials_path;
    std::string name;
    std::string description;
    bool auto_start{false};
    uint32_t max_retries{5};
    HealthCheckConfig health_check;
    RouteSafetyConfig route_safety;
};

struct VpnConfig {
    std::vector<WireGuardConnectionConfig> wireguard_connections;
    std::vector<OpenVPNConnectionConfig> openvpn_connections;
};

struct GeneralConfig {
    std::string log_level{"info"};
    std::string device_config_dir{"/etc/odc-onboard/devices"};
};

struct AppConfig {
    uint32_t version{1};
    GeneralConfig general;
    VpnConfig vpn;
};

inline void from_json(const nlohmann::json& j, HealthCheckConfig& cfg) {
    j.at("enabled").get_to(cfg.enabled);
    if (j.contains("interval_sec")) j.at("interval_sec").get_to(cfg.interval_sec);
    if (j.contains("target_ip")) j.at("target_ip").get_to(cfg.target_ip);
}

inline void to_json(nlohmann::json& j, const HealthCheckConfig& cfg) {
    j = nlohmann::json{
        {"enabled", cfg.enabled},
        {"interval_sec", cfg.interval_sec},
        {"target_ip", cfg.target_ip}
    };
}

inline void from_json(const nlohmann::json& j, RouteSafetyConfig& cfg) {
    if (j.contains("block_default_route")) j.at("block_default_route").get_to(cfg.block_default_route);
    if (j.contains("block_ipv6_default")) j.at("block_ipv6_default").get_to(cfg.block_ipv6_default);
    if (j.contains("route_metric")) j.at("route_metric").get_to(cfg.route_metric);
    if (j.contains("log_route_changes")) j.at("log_route_changes").get_to(cfg.log_route_changes);
}

inline void to_json(nlohmann::json& j, const RouteSafetyConfig& cfg) {
    j = nlohmann::json{
        {"block_default_route", cfg.block_default_route},
        {"block_ipv6_default", cfg.block_ipv6_default},
        {"route_metric", cfg.route_metric},
        {"log_route_changes", cfg.log_route_changes}
    };
}

inline void from_json(const nlohmann::json& j, WireGuardConnectionConfig& cfg) {
    j.at("id").get_to(cfg.id);
    if (j.contains("enabled")) j.at("enabled").get_to(cfg.enabled);
    j.at("config_path").get_to(cfg.config_path);
    if (j.contains("metadata")) {
        auto& m = j["metadata"];
        if (m.contains("name")) m.at("name").get_to(cfg.name);
        if (m.contains("description")) m.at("description").get_to(cfg.description);
        if (m.contains("auto_start")) m.at("auto_start").get_to(cfg.auto_start);
        if (m.contains("max_retries")) m.at("max_retries").get_to(cfg.max_retries);
        if (m.contains("health_check")) m.at("health_check").get_to(cfg.health_check);
        if (m.contains("route_safety")) m.at("route_safety").get_to(cfg.route_safety);
    }
}

inline void to_json(nlohmann::json& j, const WireGuardConnectionConfig& cfg) {
    j = nlohmann::json{
        {"id", cfg.id},
        {"enabled", cfg.enabled},
        {"config_path", cfg.config_path.string()},
        {"metadata", {
            {"name", cfg.name},
            {"description", cfg.description},
            {"auto_start", cfg.auto_start},
            {"max_retries", cfg.max_retries},
            {"health_check", cfg.health_check},
            {"route_safety", cfg.route_safety}
        }}
    };
}

inline void from_json(const nlohmann::json& j, OpenVPNConnectionConfig& cfg) {
    j.at("id").get_to(cfg.id);
    if (j.contains("enabled")) j.at("enabled").get_to(cfg.enabled);
    j.at("config_path").get_to(cfg.config_path);
    if (j.contains("credentials_path")) j.at("credentials_path").get_to(cfg.credentials_path);
    if (j.contains("metadata")) {
        auto& m = j["metadata"];
        if (m.contains("name")) m.at("name").get_to(cfg.name);
        if (m.contains("description")) m.at("description").get_to(cfg.description);
        if (m.contains("auto_start")) m.at("auto_start").get_to(cfg.auto_start);
        if (m.contains("max_retries")) m.at("max_retries").get_to(cfg.max_retries);
        if (m.contains("health_check")) m.at("health_check").get_to(cfg.health_check);
        if (m.contains("route_safety")) m.at("route_safety").get_to(cfg.route_safety);
    }
}

inline void to_json(nlohmann::json& j, const OpenVPNConnectionConfig& cfg) {
    j = nlohmann::json{
        {"id", cfg.id},
        {"enabled", cfg.enabled},
        {"config_path", cfg.config_path.string()},
        {"credentials_path", cfg.credentials_path.string()},
        {"metadata", {
            {"name", cfg.name},
            {"description", cfg.description},
            {"auto_start", cfg.auto_start},
            {"max_retries", cfg.max_retries},
            {"health_check", cfg.health_check},
            {"route_safety", cfg.route_safety}
        }}
    };
}

inline void from_json(const nlohmann::json& j, VpnConfig& cfg) {
    if (j.contains("wireguard")) {
        if (j["wireguard"].contains("connections")) {
            j["wireguard"]["connections"].get_to(cfg.wireguard_connections);
        }
    }
    if (j.contains("openvpn")) {
        if (j["openvpn"].contains("connections")) {
            j["openvpn"]["connections"].get_to(cfg.openvpn_connections);
        }
    }
}

inline void to_json(nlohmann::json& j, const VpnConfig& cfg) {
    j["wireguard"] = {{"connections", cfg.wireguard_connections}};
    j["openvpn"] = {{"connections", cfg.openvpn_connections}};
}

inline void from_json(const nlohmann::json& j, GeneralConfig& cfg) {
    if (j.contains("log_level")) j.at("log_level").get_to(cfg.log_level);
    if (j.contains("device_config_dir")) j.at("device_config_dir").get_to(cfg.device_config_dir);
}

inline void to_json(nlohmann::json& j, const GeneralConfig& cfg) {
    j = nlohmann::json{
        {"log_level", cfg.log_level},
        {"device_config_dir", cfg.device_config_dir}
    };
}

inline void from_json(const nlohmann::json& j, AppConfig& cfg) {
    if (j.contains("version")) j.at("version").get_to(cfg.version);
    if (j.contains("general")) j.at("general").get_to(cfg.general);
    if (j.contains("vpn")) j.at("vpn").get_to(cfg.vpn);
}

inline void to_json(nlohmann::json& j, const AppConfig& cfg) {
    j = nlohmann::json{
        {"version", cfg.version},
        {"general", cfg.general},
        {"vpn", cfg.vpn}
    };
}

inline std::optional<AppConfig> load_app_config(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    try {
        std::ifstream ifs(path);
        nlohmann::json j;
        ifs >> j;
        return j.get<AppConfig>();
    } catch (const std::exception& e) {
        return std::nullopt;
    }
}

inline bool save_app_config(const AppConfig& cfg, const std::filesystem::path& path) {
    try {
        nlohmann::json j = cfg;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream ofs(path);
        ofs << j.dump(2) << "\n";
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}
