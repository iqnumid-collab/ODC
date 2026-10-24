#pragma once
#include "qmi_services.h"
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace orch { class ThreadOrchestrator; }

namespace uqmi {

struct ModemData {
    // DMS operating mode
    int operating_mode = -1; // QmiDmsOperatingMode

    // WDA data format
    int  link_layer_protocol = -1; // QmiWdaLinkLayerProtocol
    bool qos_enabled = false;

    // NAS signal strength
    int8_t  rssi_dbm = 0;
    int     radio_interface = -1; // QmiNasRadioInterface
    int16_t lte_rsrp = 0;
    float   lte_snr  = 0.0f;

    // NAS serving system
    int      registration_state = -1; // QmiNasRegistrationState
    int      ps_attach_state = -1;    // QmiNasAttachState
    uint16_t plmn_mcc = 0;
    uint16_t plmn_mnc = 0;
    std::string operator_name;
    int      roaming = -1; // QmiNasRoamingIndicatorStatus

    // WDS packet service status
    int connection_status = -1; // QmiWdsConnectionStatus

    // WDS current settings
    IpSettings ip_settings;

    // WDS 3GPP profile list
    struct Profile { int index; std::string name; };
    std::vector<Profile> profiles_3gpp;

    // Internet connectivity check
    bool internet_reachable = false;
    int  ping_latency_ms    = -1;

    void log_summary() const;
};

using ModemDataCb = std::function<void(const ModemData&)>;

class ModemInfo {
public:
    void collect_all(QmiServices& qmi, ModemDataCb done_cb,
                     const std::string& ping_target = "8.8.8.8",
                     const std::string& iface = "wwan0",
                     orch::ThreadOrchestrator* pool = nullptr);

private:
    QmiServices* qmi_ = nullptr;
    ModemDataCb  done_cb_;
    ModemData    data_;
    std::string  ping_target_;
    std::string  iface_;
    orch::ThreadOrchestrator* pool_ = nullptr;

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

} // namespace uqmi
