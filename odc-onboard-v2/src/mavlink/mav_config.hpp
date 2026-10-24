#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace odc {

struct MavEndpointConfig {
    std::string type;
    std::string name;
    std::string device;
    std::vector<int> baud;
    bool flow_control{false};
    std::string address;
    int port{0};
    std::string mode;
    int retry_timeout_sec{0};
    std::string group;
};

struct MavExtenderConfig {
    std::string id;
    MavEndpointConfig upstream;
    std::vector<MavEndpointConfig> endpoints;
};

struct MavLogConfig {
    bool enabled{false};
    std::string log_dir;
    std::string mode{"while_armed"};
    std::string dialect{"auto"};
    int min_free_space_kb{1000};
    int max_log_files{10};
    bool telemetry{false};
};

struct MavExtenderAutoConfig {
    bool enabled{true};
    uint16_t udp_port{14580};
    std::vector<std::string> vpn_types;
};

struct MavlinkConfig {
    bool enabled{false};
    unsigned long tcp_server_port{5760};
    unsigned long dedup_period_sec{0};
    unsigned long sniffer_sysid{0};
    bool report_stats{true};
    MavLogConfig logging;
    MavExtenderAutoConfig extender_auto;
    std::vector<MavEndpointConfig> endpoints;
    std::vector<MavExtenderConfig> extenders;
};

inline std::optional<MavlinkConfig> parse_mavlink_config(const nlohmann::json& j) {
    if (!j.contains("mavlink") || !j["mavlink"].is_object()) {
        return std::nullopt;
    }

    const auto& ml = j["mavlink"];
    MavlinkConfig cfg;

    cfg.enabled = ml.value("enabled", false);
    cfg.tcp_server_port = ml.value("tcp_server_port", 5760);
    cfg.dedup_period_sec = ml.value("dedup_period_sec", 0);
    cfg.sniffer_sysid = ml.value("sniffer_sysid", 0);
    cfg.report_stats = ml.value("report_stats", true);

    if (ml.contains("extender_auto") && ml["extender_auto"].is_object()) {
        const auto& ea = ml["extender_auto"];
        cfg.extender_auto.enabled = ea.value("enabled", true);
        cfg.extender_auto.udp_port = ea.value("udp_port", 14580);
        if (ea.contains("vpn_types") && ea["vpn_types"].is_array()) {
            for (const auto& vt : ea["vpn_types"]) {
                cfg.extender_auto.vpn_types.push_back(vt.get<std::string>());
            }
        }
    }

    if (ml.contains("logging") && ml["logging"].is_object()) {
        const auto& log = ml["logging"];
        cfg.logging.enabled = log.value("enabled", false);
        cfg.logging.log_dir = log.value("log_dir", "/var/log/odc-onboard/mavlink");
        cfg.logging.mode = log.value("mode", "while_armed");
        cfg.logging.dialect = log.value("dialect", "auto");
        cfg.logging.min_free_space_kb = log.value("min_free_space_kb", 1000);
        cfg.logging.max_log_files = log.value("max_log_files", 10);
        cfg.logging.telemetry = log.value("telemetry", false);
    }

    if (ml.contains("endpoints") && ml["endpoints"].is_array()) {
        for (const auto& ep : ml["endpoints"]) {
            MavEndpointConfig epc;
            epc.type = ep.value("type", "");
            epc.name = ep.value("name", epc.type);
            epc.device = ep.value("device", "");
            epc.flow_control = ep.value("flow_control", false);
            epc.address = ep.value("address", "");
            epc.port = ep.value("port", 0);
            epc.mode = ep.value("mode", "normal");
            epc.retry_timeout_sec = ep.value("retry_timeout_sec", 0);
            epc.group = ep.value("group", "");

            if (ep.contains("baud")) {
                if (ep["baud"].is_number_integer()) {
                    epc.baud.push_back(ep["baud"].get<int>());
                } else if (ep["baud"].is_array()) {
                    for (const auto& b : ep["baud"]) {
                        epc.baud.push_back(b.get<int>());
                    }
                }
            }

            cfg.endpoints.push_back(std::move(epc));
        }
    }

    if (ml.contains("extenders") && ml["extenders"].is_array()) {
        for (const auto& ext : ml["extenders"]) {
            MavExtenderConfig ec;
            ec.id = ext.value("id", "");

            if (ext.contains("upstream") && ext["upstream"].is_object()) {
                const auto& up = ext["upstream"];
                ec.upstream.type = up.value("type", "udp");
                ec.upstream.name = up.value("name", "upstream");
                ec.upstream.address = up.value("address", "127.0.0.1");
                ec.upstream.port = up.value("port", 0);
                ec.upstream.mode = up.value("mode", "client");
                ec.upstream.retry_timeout_sec = up.value("retry_timeout_sec", 5);
            }

            if (ext.contains("endpoints") && ext["endpoints"].is_array()) {
                for (const auto& ep : ext["endpoints"]) {
                    MavEndpointConfig epc;
                    epc.type = ep.value("type", "");
                    epc.name = ep.value("name", epc.type);
                    epc.device = ep.value("device", "");
                    epc.flow_control = ep.value("flow_control", false);
                    epc.address = ep.value("address", "0.0.0.0");
                    epc.port = ep.value("port", 0);
                    epc.mode = ep.value("mode", "server");
                    epc.retry_timeout_sec = ep.value("retry_timeout_sec", 0);
                    epc.group = ep.value("group", "");

                    if (ep.contains("baud")) {
                        if (ep["baud"].is_number_integer()) {
                            epc.baud.push_back(ep["baud"].get<int>());
                        } else if (ep["baud"].is_array()) {
                            for (const auto& b : ep["baud"]) {
                                epc.baud.push_back(b.get<int>());
                            }
                        }
                    }

                    ec.endpoints.push_back(std::move(epc));
                }
            }

            cfg.extenders.push_back(std::move(ec));
        }
    }

    return cfg;
}

}
