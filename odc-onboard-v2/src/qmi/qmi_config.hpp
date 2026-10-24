#pragma once
#include <string>
#include <cstdint>
#include <vector>

namespace odc {
namespace qmi {

struct PdnConfig {
    std::string id;
    std::string apn;
    int         ip_type = 4;      // 4=IPv4, 6=IPv6, 46=dual
    std::string iface;
    int         profile_index = -1; // >=0 → StartNetwork by profile
    std::string auth_type = "none";
    std::string username;
    std::string password;
};

struct FccUnlockConfig {
    bool        enabled = false;
    std::string method = "standard"; // standard | foxconn | foxconn_v2
};

struct QmiConfig {
    std::string device_path;
    std::string iface_name = "wwan0";

    std::string apn;
    int         ip_type   = 4;      // 4=IPv4, 6=IPv6, 46=dual
    std::string auth_type = "none"; // none, pap, chap
    std::string username;
    std::string password;
    bool        auto_connect = true;
    bool        allow_roaming = true;

    std::string pin;

    /** If >= 0, StartNetwork uses this 3GPP profile index. */
    int  profile_index = -1;
    /** Create/modify modem profile from apn/auth before start. */
    bool ensure_profile = false;
    /** Modem-side WDS autoconnect (distinct from app auto_connect). */
    bool modem_autoconnect = false;

    /** Extra / multi PDN sessions. Empty → single session from top-level fields. */
    std::vector<PdnConfig> pdns;

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

    /** Soft-fail count before escalating to hard reset (0 = never auto hard-reset). */
    int         hard_reset_after_soft_fails = 2;
    /** dms | usb | dms_then_usb */
    std::string hard_reset_mode = "dms_then_usb";
    uint32_t    hard_reset_settle_ms = 5000;

    FccUnlockConfig fcc_unlock;
};

} // namespace qmi
} // namespace odc
