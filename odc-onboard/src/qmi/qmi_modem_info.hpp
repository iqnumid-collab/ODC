#pragma once
#include "qmi_services.hpp"
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace odc {
namespace qmi {

struct ModemData {
    int operating_mode = -1;
    int link_layer_protocol = -1;
    bool qos_enabled = false;

    int8_t  rssi_dbm = 0;
    int     radio_interface = -1;
    int16_t lte_rsrp = 0;
    float   lte_snr = 0.0f;

    int      registration_state = -1;
    int      ps_attach_state = -1;
    uint16_t plmn_mcc = 0;
    uint16_t plmn_mnc = 0;
    std::string operator_name;
    int      roaming = -1;

    int connection_status = -1;
    IpSettings ip_settings;

    struct Profile { int index; std::string name; };
    std::vector<Profile> profiles_3gpp;

    bool internet_reachable = false;
    int  ping_latency_ms = -1;
};

using ModemDataCb = std::function<void(const ModemData&)>;

class QmiModemInfo {
public:
    void collect_all(QmiServices& qmi, ModemDataCb done_cb,
                     const std::string& ping_target = "8.8.8.8",
                     const std::string& iface = "wwan0");

private:
    QmiServices* qmi_ = nullptr;
    ModemDataCb  done_cb_;
    ModemData    data_;
    std::string  ping_target_;
    std::string  iface_;

    void step_dms_operating_mode();
    void step_wda_data_format();
    void step_nas_signal_strength();
    void step_nas_serving_system();
    void step_wds_packet_status();
    void step_wds_current_settings();
    void step_wds_profile_list();
    void step_ping_check();
    void do_ping();
    void finish();
};

} // namespace qmi
} // namespace odc
