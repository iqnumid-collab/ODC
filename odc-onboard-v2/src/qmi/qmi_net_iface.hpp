#pragma once
#include "qmi_services.hpp"
#include <string>

namespace odc {
namespace qmi {

class QmiNetIface {
public:
    bool set_raw_ip(const std::string& iface);
    bool bring_up(const std::string& iface);
    bool bring_down(const std::string& iface);
    bool configure(const std::string& iface, const IpSettings& s);
    bool deconfigure(const std::string& iface);

private:
    static constexpr int WWAN_METRIC = 9999;

    std::string cfg_iface_;
    std::string cfg_gw_;
    std::string cfg_v6_gw_;
    uint32_t    cfg_prefix_ = 32;
    bool        dns_written_ = false;

    bool has_existing_dns();
};

} // namespace qmi
} // namespace odc
