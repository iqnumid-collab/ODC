#pragma once
#include "qmi_services.h"
#include <string>

namespace uqmi {

class NetIface {
public:
    bool set_raw_ip(const std::string& iface);
    bool bring_up(const std::string& iface);
    bool bring_down(const std::string& iface);
    bool configure(const std::string& iface, const IpSettings& settings);
    bool deconfigure(const std::string& iface);

private:
    // State for clean teardown
    std::string cfg_iface_;
    std::string cfg_gw_;
    uint32_t    cfg_prefix_  = 32;
    bool        dns_written_ = false;

    static bool has_existing_dns();
};

} // namespace uqmi
