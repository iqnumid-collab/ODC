#pragma once

#include <string>
#include <cstdint>
#include <spdlog/spdlog.h>

namespace odc {

class RouteSafetyValidator {
public:
    explicit RouteSafetyValidator(const RouteSafetyConfig& config) : config_(config) {}

    bool is_route_allowed(const std::string& dst, int prefix_len, bool ipv6) const {
        if (!is_default_route(dst, prefix_len, ipv6)) {
            return true;
        }
        if (ipv6 && config_.block_ipv6_default) {
            if (config_.log_route_changes) {
                SPDLOG_WARN("[RouteSafety] Blocked IPv6 default route ::/0");
            }
            return false;
        }
        if (!ipv6 && config_.block_default_route) {
            if (config_.log_route_changes) {
                SPDLOG_WARN("[RouteSafety] Blocked IPv4 default route 0.0.0.0/0");
            }
            return false;
        }
        return true;
    }

    static bool is_default_route(const std::string& dst, int prefix_len, bool ipv6) {
        if (ipv6) {
            return (dst == "::" && prefix_len == 0) ||
                   (dst == "::" && prefix_len == 1) ||
                   (dst == "8000::" && prefix_len == 1);
        }
        if (prefix_len == 0) {
            return (dst == "0.0.0.0" || dst.empty());
        }
        if (prefix_len == 1) {
            return dst == "0.0.0.0" || dst == "128.0.0.0";
        }
        return false;
    }

    uint32_t get_safe_metric() const {
        return config_.route_metric;
    }

private:
    RouteSafetyConfig config_;
};

}
