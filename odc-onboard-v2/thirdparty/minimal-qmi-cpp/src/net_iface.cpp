#include "net_iface.h"
#include "log.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace uqmi {

static constexpr int WWAN_METRIC = 9999;

static int run(const char* cmd) {
    LOG_D("exec: %s", cmd);
    return system(cmd);
}

bool NetIface::set_raw_ip(const std::string& iface) {
    std::string path = "/sys/class/net/" + iface + "/qmi/raw_ip";
    std::ofstream f(path);
    if (!f) { LOG_W("cannot write %s", path.c_str()); return false; }
    f << "Y";
    return f.good();
}

bool NetIface::bring_up(const std::string& iface) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ip link set %s up", iface.c_str());
    return run(cmd) == 0;
}

bool NetIface::bring_down(const std::string& iface) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ip link set %s down", iface.c_str());
    return run(cmd) == 0;
}

bool NetIface::has_existing_dns() {
    std::ifstream f("/etc/resolv.conf");
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("nameserver") != std::string::npos)
            return true;
    }
    return false;
}

bool NetIface::configure(const std::string& iface, const IpSettings& s) {
    char cmd[256];

    // Flush only this interface's addresses
    snprintf(cmd, sizeof(cmd), "ip addr flush dev %s", iface.c_str());
    run(cmd);

    // MTU first (before adding address)
    if (s.mtu > 0) {
        snprintf(cmd, sizeof(cmd), "ip link set %s mtu %u", iface.c_str(), s.mtu);
        run(cmd);
    }

    // Add IP with proper prefix from modem
    if (!s.ipv4_addr.empty()) {
        snprintf(cmd, sizeof(cmd), "ip addr add %s/%u dev %s",
                 s.ipv4_addr.c_str(), s.prefix, iface.c_str());
        if (run(cmd) != 0) return false;
    }

    // If /32, add link-scoped route to make gateway reachable
    if (s.prefix == 32 && !s.gateway.empty()) {
        snprintf(cmd, sizeof(cmd), "ip route add %s dev %s scope link",
                 s.gateway.c_str(), iface.c_str());
        run(cmd);
    }

    // Default route with high metric — never overrides existing gateways
    if (!s.gateway.empty()) {
        snprintf(cmd, sizeof(cmd), "ip route add default via %s dev %s metric %d",
                 s.gateway.c_str(), iface.c_str(), WWAN_METRIC);
    } else {
        snprintf(cmd, sizeof(cmd), "ip route add default dev %s metric %d",
                 iface.c_str(), WWAN_METRIC);
    }
    run(cmd);

    // Non-destructive DNS: only write if no nameservers exist
    dns_written_ = false;
    if (!s.dns1.empty() && !has_existing_dns()) {
        snprintf(cmd, sizeof(cmd), "echo 'nameserver %s' > /etc/resolv.conf",
                 s.dns1.c_str());
        run(cmd);
        if (!s.dns2.empty()) {
            snprintf(cmd, sizeof(cmd), "echo 'nameserver %s' >> /etc/resolv.conf",
                     s.dns2.c_str());
            run(cmd);
        }
        dns_written_ = true;
        LOG_I("wrote WWAN DNS (no existing nameservers found)");
    }

    // Save state for clean teardown
    cfg_iface_  = iface;
    cfg_gw_     = s.gateway;
    cfg_prefix_ = s.prefix;

    LOG_I("interface %s configured: %s/%u via %s metric %d",
          iface.c_str(), s.ipv4_addr.c_str(), s.prefix,
          s.gateway.empty() ? "(direct)" : s.gateway.c_str(), WWAN_METRIC);
    return true;
}

bool NetIface::deconfigure(const std::string& iface) {
    char cmd[256];

    // Remove only our specific default route (by gw + dev + metric)
    if (!cfg_gw_.empty()) {
        snprintf(cmd, sizeof(cmd),
                 "ip route del default via %s dev %s metric %d 2>/dev/null",
                 cfg_gw_.c_str(), iface.c_str(), WWAN_METRIC);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "ip route del default dev %s metric %d 2>/dev/null",
                 iface.c_str(), WWAN_METRIC);
    }
    run(cmd);

    // Remove link-scoped gateway route if we added one
    if (cfg_prefix_ == 32 && !cfg_gw_.empty()) {
        snprintf(cmd, sizeof(cmd), "ip route del %s dev %s scope link 2>/dev/null",
                 cfg_gw_.c_str(), iface.c_str());
        run(cmd);
    }

    // Flush only this interface's addresses
    snprintf(cmd, sizeof(cmd), "ip addr flush dev %s", iface.c_str());
    run(cmd);

    // Clean up DNS only if we wrote it
    if (dns_written_) {
        std::ofstream f("/etc/resolv.conf", std::ios::trunc);
        dns_written_ = false;
        LOG_I("cleared WWAN DNS entries");
    }

    // Clear saved state
    cfg_iface_.clear();
    cfg_gw_.clear();
    cfg_prefix_ = 32;

    return true;
}

} // namespace uqmi
