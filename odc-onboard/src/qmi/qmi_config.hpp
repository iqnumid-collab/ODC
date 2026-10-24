#pragma once
#include <string>
#include <cstdint>

namespace odc {
namespace qmi {

struct QmiConfig {
    std::string device_path;
    std::string iface_name = "wwan0";

    std::string apn;
    int         ip_type   = 4;      // 4=IPv4, 6=IPv6, 46=dual
    std::string auth_type = "none"; // none, pap, chap
    std::string username;
    std::string password;
    bool        auto_connect = true;

    std::string pin;

    int      max_retries        = 5;
    uint32_t backoff_base_ms    = 1000;
    uint32_t backoff_max_ms     = 60000;
    uint32_t backoff_jitter_ms  = 500;

    uint32_t watchdog_interval_s   = 30;
    std::string ping_target        = "8.8.8.8";
    uint32_t modem_info_interval_s = 10;

    uint32_t open_timeout_ms = 30000;
    uint32_t init_timeout_ms = 30000;
    uint32_t sim_timeout_ms  = 30000;
    uint32_t reg_timeout_ms  = 120000;
    uint32_t data_timeout_ms = 30000;
};

} // namespace qmi
} // namespace odc
