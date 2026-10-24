#include "qmi_net_iface.hpp"
#include "qmi_services.hpp"
#include <spdlog/spdlog.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace odc {
namespace qmi {

static int run_cmd(const char* cmd) {
    SPDLOG_DEBUG("qmi: exec: {}", cmd);
    return system(cmd);
}

bool QmiNetIface::set_raw_ip(const std::string& iface) {
    std::string path = "/sys/class/net/" + iface + "/qmi/raw_ip";
    std::ofstream f(path);
    if (!f) { SPDLOG_WARN("qmi: cannot write {}", path); return false; }
    f << "Y";
    return f.good();
}

bool QmiNetIface::bring_up(const std::string& iface) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ip link set %s up", iface.c_str());
    return run_cmd(cmd) == 0;
}

bool QmiNetIface::bring_down(const std::string& iface) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ip link set %s down", iface.c_str());
    return run_cmd(cmd) == 0;
}

bool QmiNetIface::has_existing_dns() {
    std::ifstream f("/etc/resolv.conf");
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("nameserver") != std::string::npos)
            return true;
    }
    return false;
}

bool QmiNetIface::configure(const std::string& iface, const IpSettings& s) {
    char cmd[256];

    snprintf(cmd, sizeof(cmd), "ip addr flush dev %s", iface.c_str());
    run_cmd(cmd);

    if (s.mtu > 0) {
        snprintf(cmd, sizeof(cmd), "ip link set %s mtu %u", iface.c_str(), s.mtu);
        run_cmd(cmd);
    }

    if (!s.ipv4_addr.empty()) {
        snprintf(cmd, sizeof(cmd), "ip addr add %s/%u dev %s",
                 s.ipv4_addr.c_str(), s.prefix, iface.c_str());
        if (run_cmd(cmd) != 0) return false;
    }

    if (!s.ipv6_addr.empty()) {
        snprintf(cmd, sizeof(cmd), "ip -6 addr add %s/%u dev %s",
                 s.ipv6_addr.c_str(), static_cast<unsigned>(s.ipv6_prefix), iface.c_str());
        run_cmd(cmd);
    }

    if (s.prefix == 32 && !s.gateway.empty()) {
        snprintf(cmd, sizeof(cmd), "ip route add %s dev %s scope link",
                 s.gateway.c_str(), iface.c_str());
        run_cmd(cmd);
    }

    if (!s.gateway.empty()) {
        snprintf(cmd, sizeof(cmd), "ip route add default via %s dev %s metric %d",
                 s.gateway.c_str(), iface.c_str(), WWAN_METRIC);
        run_cmd(cmd);
    } else if (!s.ipv4_addr.empty()) {
        snprintf(cmd, sizeof(cmd), "ip route add default dev %s metric %d",
                 iface.c_str(), WWAN_METRIC);
        run_cmd(cmd);
    }

    if (!s.ipv6_gateway.empty()) {
        snprintf(cmd, sizeof(cmd), "ip -6 route add default via %s dev %s metric %d",
                 s.ipv6_gateway.c_str(), iface.c_str(), WWAN_METRIC);
        run_cmd(cmd);
    } else if (!s.ipv6_addr.empty()) {
        snprintf(cmd, sizeof(cmd), "ip -6 route add default dev %s metric %d",
                 iface.c_str(), WWAN_METRIC);
        run_cmd(cmd);
    }

    dns_written_ = false;
    if ((!s.dns1.empty() || !s.ipv6_dns1.empty()) && !has_existing_dns()) {
        std::ofstream resolv("/etc/resolv.conf", std::ios::trunc);
        if (!s.dns1.empty()) {
            resolv << "nameserver " << s.dns1 << "\n";
        }
        if (!s.dns2.empty()) {
            resolv << "nameserver " << s.dns2 << "\n";
        }
        if (!s.ipv6_dns1.empty()) {
            resolv << "nameserver " << s.ipv6_dns1 << "\n";
        }
        if (!s.ipv6_dns2.empty()) {
            resolv << "nameserver " << s.ipv6_dns2 << "\n";
        }
        dns_written_ = true;
        SPDLOG_INFO("qmi: wrote WWAN DNS");
    }

    cfg_iface_  = iface;
    cfg_gw_     = s.gateway;
    cfg_prefix_ = s.prefix;
    cfg_v6_gw_  = s.ipv6_gateway;

    SPDLOG_INFO("qmi: iface {} configured: {}/{} v6 {}/{} via {} metric {}",
                 iface, s.ipv4_addr, s.prefix, s.ipv6_addr, s.ipv6_prefix,
                 s.gateway.empty() ? "(direct)" : s.gateway, WWAN_METRIC);
    return !s.ipv4_addr.empty() || !s.ipv6_addr.empty();
}

bool QmiNetIface::deconfigure(const std::string& iface) {
    char cmd[256];

    if (!cfg_gw_.empty()) {
        snprintf(cmd, sizeof(cmd),
                 "ip route del default via %s dev %s metric %d 2>/dev/null",
                 cfg_gw_.c_str(), iface.c_str(), WWAN_METRIC);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "ip route del default dev %s metric %d 2>/dev/null",
                 iface.c_str(), WWAN_METRIC);
    }
    run_cmd(cmd);

    if (!cfg_v6_gw_.empty()) {
        snprintf(cmd, sizeof(cmd),
                 "ip -6 route del default via %s dev %s metric %d 2>/dev/null",
                 cfg_v6_gw_.c_str(), iface.c_str(), WWAN_METRIC);
        run_cmd(cmd);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "ip -6 route del default dev %s metric %d 2>/dev/null",
                 iface.c_str(), WWAN_METRIC);
        run_cmd(cmd);
    }

    if (cfg_prefix_ == 32 && !cfg_gw_.empty()) {
        snprintf(cmd, sizeof(cmd), "ip route del %s dev %s scope link 2>/dev/null",
                 cfg_gw_.c_str(), iface.c_str());
        run_cmd(cmd);
    }

    snprintf(cmd, sizeof(cmd), "ip addr flush dev %s", iface.c_str());
    run_cmd(cmd);

    if (dns_written_) {
        std::ofstream f("/etc/resolv.conf", std::ios::trunc);
        dns_written_ = false;
        SPDLOG_INFO("qmi: cleared WWAN DNS entries");
    }

    cfg_iface_.clear();
    cfg_gw_.clear();
    cfg_v6_gw_.clear();
    cfg_prefix_ = 32;
    return true;
}

} // namespace qmi
} // namespace odc
