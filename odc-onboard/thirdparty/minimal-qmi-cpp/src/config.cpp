#include "config.h"
#include "log.h"
#include <fstream>

namespace uqmi {

static std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    auto e = s.find_last_not_of(" \t\r\n");
    return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

Config Config::load(const std::string& path) {
    Config cfg;
    cfg.config_path = path;
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("cannot open " + path);

    std::string line, section;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            section = line.substr(1, line.find(']') - 1);
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto key = trim(line.substr(0, eq));
        auto val = trim(line.substr(eq + 1));

        if (section == "device") {
            if (key == "path")      cfg.device_path = val;
            if (key == "interface") cfg.iface_name = val;
        } else if (section == "connection") {
            if (key == "apn")       cfg.apn = val;
            if (key == "ip_type")   cfg.ip_type = std::stoi(val);
            if (key == "auth")      cfg.auth_type = val;
            if (key == "username")  cfg.username = val;
            if (key == "password")  cfg.password = val;
            if (key == "auto_connect") cfg.auto_connect = (val == "true" || val == "1");
        } else if (section == "sim") {
            if (key == "pin")       cfg.pin = val;
        } else if (section == "recovery") {
            if (key == "max_retries")        cfg.max_retries = std::stoi(val);
            if (key == "backoff_base_ms")    cfg.backoff_base_ms = std::stoul(val);
            if (key == "backoff_max_ms")     cfg.backoff_max_ms = std::stoul(val);
            if (key == "backoff_jitter_ms")  cfg.backoff_jitter_ms = std::stoul(val);
            if (key == "circuit_breaker_ms") cfg.circuit_breaker_ms = std::stoul(val);
        } else if (section == "watchdog") {
            if (key == "interval_s")  cfg.watchdog_interval_s = std::stoul(val);
            if (key == "ping_target") cfg.ping_target = val;
            if (key == "modem_info_interval_s") cfg.modem_info_interval_s = std::stoul(val);
        } else if (section == "timeouts") {
            if (key == "open_timeout_ms")  cfg.open_timeout_ms = std::stoul(val);
            if (key == "init_timeout_ms")  cfg.init_timeout_ms = std::stoul(val);
            if (key == "sim_timeout_ms")   cfg.sim_timeout_ms = std::stoul(val);
            if (key == "reg_timeout_ms")   cfg.reg_timeout_ms = std::stoul(val);
            if (key == "data_timeout_ms")  cfg.data_timeout_ms = std::stoul(val);
        } else if (section == "kafka") {
            if (key == "enabled")         cfg.kafka_enabled = (val == "true" || val == "1");
            if (key == "broker")          cfg.kafka_broker = val;
            if (key == "client_id")       cfg.kafka_client_id = val;
            if (key == "events_topic")    cfg.kafka_events_topic = val;
            if (key == "telemetry_topic") cfg.kafka_telemetry_topic = val;
            if (key == "commands_topic")  cfg.kafka_commands_topic = val;
            if (key == "responses_topic") cfg.kafka_responses_topic = val;
        }
    }
    LOG_I("config loaded from %s", path.c_str());
    return cfg;
}

bool Config::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) { LOG_E("cannot write config to %s", path.c_str()); return false; }

    f << "# ultima-qmi controller configuration\n"
      << "# Auto-generated — edit as needed\n\n"
      << "[device]\n"
      << "path = " << device_path << "\n"
      << "interface = " << iface_name << "\n\n"
      << "[connection]\n"
      << "apn = " << apn << "\n"
      << "ip_type = " << ip_type << "\n"
      << "auth = " << auth_type << "\n"
      << "username = " << username << "\n"
      << "password = " << password << "\n"
      << "auto_connect = " << (auto_connect ? "true" : "false") << "\n\n"
      << "[sim]\n"
      << "pin = " << pin << "\n\n"
      << "[recovery]\n"
      << "max_retries = " << max_retries << "\n"
      << "backoff_base_ms = " << backoff_base_ms << "\n"
      << "backoff_max_ms = " << backoff_max_ms << "\n"
      << "backoff_jitter_ms = " << backoff_jitter_ms << "\n"
      << "circuit_breaker_ms = " << circuit_breaker_ms << "\n\n"
      << "[watchdog]\n"
      << "interval_s = " << watchdog_interval_s << "\n"
      << "ping_target = " << ping_target << "\n"
      << "modem_info_interval_s = " << modem_info_interval_s << "\n\n"
      << "[timeouts]\n"
      << "open_timeout_ms = " << open_timeout_ms << "\n"
      << "init_timeout_ms = " << init_timeout_ms << "\n"
      << "sim_timeout_ms = " << sim_timeout_ms << "\n"
      << "reg_timeout_ms = " << reg_timeout_ms << "\n"
      << "data_timeout_ms = " << data_timeout_ms << "\n\n"
      << "[kafka]\n"
      << "enabled = " << (kafka_enabled ? "true" : "false") << "\n"
      << "broker = " << kafka_broker << "\n"
      << "client_id = " << kafka_client_id << "\n"
      << "events_topic = " << kafka_events_topic << "\n"
      << "telemetry_topic = " << kafka_telemetry_topic << "\n"
      << "commands_topic = " << kafka_commands_topic << "\n"
      << "responses_topic = " << kafka_responses_topic << "\n";

    LOG_I("config saved to %s", path.c_str());
    return f.good();
}

bool Config::save() const {
    if (config_path.empty()) return false;
    return save(config_path);
}

bool Config::generate_default(const std::string& path, const std::string& device, const std::string& iface) {
    Config cfg;
    cfg.config_path = path;
    cfg.device_path = device;
    cfg.iface_name = iface.empty() ? "wwan0" : iface;
    return cfg.save(path);
}

} // namespace uqmi
