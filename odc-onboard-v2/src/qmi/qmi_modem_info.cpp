#include "qmi_modem_info.hpp"
#include <spdlog/spdlog.h>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace odc {
namespace qmi {

void QmiModemInfo::collect_all(QmiServices& qmi, ModemDataCb done_cb,
                               const std::string& ping_target,
                               const std::string& iface) {
    qmi_ = &qmi;
    done_cb_ = std::move(done_cb);
    data_ = {};
    ping_target_ = ping_target;
    iface_ = iface;
    step_dms_operating_mode();
}

void QmiModemInfo::step_dms_operating_mode() {
    if (!qmi_->dms()) { step_wda_data_format(); return; }
    qmi_client_dms_get_operating_mode(qmi_->dms(), nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* self = static_cast<QmiModemInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_dms_get_operating_mode_finish(QMI_CLIENT_DMS(src), res, &err);
            if (out) {
                QmiDmsOperatingMode mode;
                qmi_message_dms_get_operating_mode_output_get_mode(out, &mode, nullptr);
                self->data_.operating_mode = static_cast<int>(mode);
                qmi_message_dms_get_operating_mode_output_unref(out);
            } else {
                SPDLOG_WARN("qmi: dms_get_operating_mode: {}", err->message);
                g_error_free(err);
            }
            self->step_wda_data_format();
        }, this);
}

void QmiModemInfo::step_wda_data_format() {
    if (!qmi_->wda()) { step_nas_signal_strength(); return; }
    qmi_client_wda_get_data_format(qmi_->wda(), nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* self = static_cast<QmiModemInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_wda_get_data_format_finish(QMI_CLIENT_WDA(src), res, &err);
            if (out) {
                QmiWdaLinkLayerProtocol llp;
                if (qmi_message_wda_get_data_format_output_get_link_layer_protocol(out, &llp, nullptr))
                    self->data_.link_layer_protocol = static_cast<int>(llp);
                gboolean qos;
                if (qmi_message_wda_get_data_format_output_get_qos_format(out, &qos, nullptr))
                    self->data_.qos_enabled = qos;
                qmi_message_wda_get_data_format_output_unref(out);
            } else {
                SPDLOG_WARN("qmi: wda_get_data_format: {}", err->message);
                g_error_free(err);
            }
            self->step_nas_signal_strength();
        }, this);
}

void QmiModemInfo::step_nas_signal_strength() {
    if (!qmi_->nas()) { step_nas_serving_system(); return; }
    auto* input = qmi_message_nas_get_signal_strength_input_new();
    qmi_message_nas_get_signal_strength_input_set_request_mask(input,
        static_cast<QmiNasSignalStrengthRequest>(
            QMI_NAS_SIGNAL_STRENGTH_REQUEST_RSSI |
            QMI_NAS_SIGNAL_STRENGTH_REQUEST_LTE_SNR |
            QMI_NAS_SIGNAL_STRENGTH_REQUEST_LTE_RSRP), nullptr);
    qmi_client_nas_get_signal_strength(qmi_->nas(), input, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* self = static_cast<QmiModemInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_nas_get_signal_strength_finish(QMI_CLIENT_NAS(src), res, &err);
            if (out) {
                gint8 strength; QmiNasRadioInterface ri;
                if (qmi_message_nas_get_signal_strength_output_get_signal_strength(out, &strength, &ri, nullptr)) {
                    self->data_.rssi_dbm = strength;
                    self->data_.radio_interface = static_cast<int>(ri);
                }
                gint16 rsrp;
                if (qmi_message_nas_get_signal_strength_output_get_lte_rsrp(out, &rsrp, nullptr))
                    self->data_.lte_rsrp = rsrp;
                gint16 snr;
                if (qmi_message_nas_get_signal_strength_output_get_lte_snr(out, &snr, nullptr))
                    self->data_.lte_snr = snr / 10.0f;
                qmi_message_nas_get_signal_strength_output_unref(out);
            } else {
                SPDLOG_WARN("qmi: nas_get_signal_strength: {}", err->message);
                g_error_free(err);
            }
            self->step_nas_serving_system();
        }, this);
    qmi_message_nas_get_signal_strength_input_unref(input);
}

void QmiModemInfo::step_nas_serving_system() {
    if (!qmi_->nas()) { step_wds_packet_status(); return; }
    qmi_client_nas_get_serving_system(qmi_->nas(), nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* self = static_cast<QmiModemInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_nas_get_serving_system_finish(QMI_CLIENT_NAS(src), res, &err);
            if (out) {
                QmiNasRegistrationState reg; QmiNasAttachState cs, ps;
                QmiNasNetworkType net; GArray* ris;
                if (qmi_message_nas_get_serving_system_output_get_serving_system(out, &reg, &cs, &ps, &net, &ris, nullptr)) {
                    self->data_.registration_state = static_cast<int>(reg);
                    self->data_.ps_attach_state = static_cast<int>(ps);
                }
                guint16 mcc, mnc; const gchar* desc;
                if (qmi_message_nas_get_serving_system_output_get_current_plmn(out, &mcc, &mnc, &desc, nullptr)) {
                    self->data_.plmn_mcc = mcc;
                    self->data_.plmn_mnc = mnc;
                    self->data_.operator_name = desc ? desc : "";
                }
                QmiNasRoamingIndicatorStatus roam;
                if (qmi_message_nas_get_serving_system_output_get_roaming_indicator(out, &roam, nullptr))
                    self->data_.roaming = static_cast<int>(roam);
                qmi_message_nas_get_serving_system_output_unref(out);
            } else {
                SPDLOG_WARN("qmi: nas_get_serving_system: {}", err->message);
                g_error_free(err);
            }
            self->step_wds_packet_status();
        }, this);
}

void QmiModemInfo::step_wds_packet_status() {
    if (!qmi_->wds()) { step_wds_current_settings(); return; }
    qmi_client_wds_get_packet_service_status(qmi_->wds(), nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* self = static_cast<QmiModemInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_wds_get_packet_service_status_finish(QMI_CLIENT_WDS(src), res, &err);
            if (out) {
                QmiWdsConnectionStatus status;
                if (qmi_message_wds_get_packet_service_status_output_get_connection_status(out, &status, nullptr))
                    self->data_.connection_status = static_cast<int>(status);
                qmi_message_wds_get_packet_service_status_output_unref(out);
            } else {
                SPDLOG_WARN("qmi: wds_get_packet_service_status: {}", err->message);
                g_error_free(err);
            }
            self->step_wds_current_settings();
        }, this);
}

void QmiModemInfo::step_wds_current_settings() {
    if (!qmi_->wds()) { step_wds_profile_list(); return; }
    auto* input = qmi_message_wds_get_current_settings_input_new();
    qmi_message_wds_get_current_settings_input_set_requested_settings(input,
        static_cast<QmiWdsRequestedSettings>(
            QMI_WDS_REQUESTED_SETTINGS_DNS_ADDRESS |
            QMI_WDS_REQUESTED_SETTINGS_IP_ADDRESS |
            QMI_WDS_REQUESTED_SETTINGS_GATEWAY_INFO |
            QMI_WDS_REQUESTED_SETTINGS_MTU), nullptr);
    qmi_client_wds_get_current_settings(qmi_->wds(), input, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* self = static_cast<QmiModemInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_wds_get_current_settings_finish(QMI_CLIENT_WDS(src), res, &err);
            if (out) {
                auto& s = self->data_.ip_settings;
                guint32 v; char buf[32];
                auto fmt = [](guint32 a, char* b, size_t l) {
                    auto* p = reinterpret_cast<guint8*>(&a);
                    snprintf(b, l, "%u.%u.%u.%u", p[3], p[2], p[1], p[0]);
                };
                if (qmi_message_wds_get_current_settings_output_get_ipv4_address(out, &v, nullptr)) {
                    fmt(v, buf, sizeof(buf)); s.ipv4_addr = buf;
                }
                if (qmi_message_wds_get_current_settings_output_get_ipv4_gateway_address(out, &v, nullptr)) {
                    fmt(v, buf, sizeof(buf)); s.gateway = buf;
                }
                if (qmi_message_wds_get_current_settings_output_get_primary_ipv4_dns_address(out, &v, nullptr)) {
                    fmt(v, buf, sizeof(buf)); s.dns1 = buf;
                }
                if (qmi_message_wds_get_current_settings_output_get_secondary_ipv4_dns_address(out, &v, nullptr)) {
                    fmt(v, buf, sizeof(buf)); s.dns2 = buf;
                }
                if (qmi_message_wds_get_current_settings_output_get_mtu(out, &v, nullptr)) s.mtu = v;
                qmi_message_wds_get_current_settings_output_unref(out);
            } else {
                SPDLOG_WARN("qmi: wds_get_current_settings: {}", err->message);
                g_error_free(err);
            }
            self->step_wds_profile_list();
        }, this);
    qmi_message_wds_get_current_settings_input_unref(input);
}

void QmiModemInfo::step_wds_profile_list() {
    if (!qmi_->wds()) { step_ping_check(); return; }
    auto* input = qmi_message_wds_get_profile_list_input_new();
    qmi_message_wds_get_profile_list_input_set_profile_type(input, QMI_WDS_PROFILE_TYPE_3GPP, nullptr);
    qmi_client_wds_get_profile_list(qmi_->wds(), input, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* self = static_cast<QmiModemInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_wds_get_profile_list_finish(QMI_CLIENT_WDS(src), res, &err);
            if (out) {
                GArray* list = nullptr;
                if (qmi_message_wds_get_profile_list_output_get_profile_list(out, &list, nullptr) && list) {
                    for (guint i = 0; i < list->len; i++) {
                        auto& e = g_array_index(list, QmiMessageWdsGetProfileListOutputProfileListProfile, i);
                        self->data_.profiles_3gpp.push_back({e.profile_index,
                                                              e.profile_name ? e.profile_name : ""});
                    }
                }
                qmi_message_wds_get_profile_list_output_unref(out);
            } else {
                SPDLOG_WARN("qmi: wds_get_profile_list: {}", err->message);
                g_error_free(err);
            }
            self->step_ping_check();
        }, this);
    qmi_message_wds_get_profile_list_input_unref(input);
}

void QmiModemInfo::step_ping_check() {
    do_ping();
    finish();
}

void QmiModemInfo::do_ping() {
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "ping -c 1 -W 3 -I %s %s 2>&1",
             iface_.c_str(), ping_target_.c_str());
    FILE* fp = popen(cmd, "r");
    if (fp) {
        char buf[512];
        std::string output;
        while (fgets(buf, sizeof(buf), fp))
            output += buf;
        int rc = pclose(fp);
        data_.internet_reachable = (rc == 0);
        auto pos = output.find("time=");
        if (pos != std::string::npos)
            data_.ping_latency_ms = static_cast<int>(std::strtof(output.c_str() + pos + 5, nullptr));
    }
}

void QmiModemInfo::finish() {
    SPDLOG_INFO("qmi: telem - RSSI: {} RAT: {} RSRP: {} SNR: {:.1f} reg: {} roam: {} conn: {} ping: {}",
                 data_.rssi_dbm, data_.radio_interface, data_.lte_rsrp, data_.lte_snr,
                 data_.registration_state, data_.roaming, data_.connection_status,
                 data_.internet_reachable ? "OK" : "FAIL");
    SPDLOG_INFO("qmi: telem - PLMN: {}-{} {} IP: {} GW: {} DNS: {}/{} MTU: {}",
                 data_.plmn_mcc, data_.plmn_mnc, data_.operator_name,
                 data_.ip_settings.ipv4_addr, data_.ip_settings.gateway,
                 data_.ip_settings.dns1, data_.ip_settings.dns2, data_.ip_settings.mtu);
    if (done_cb_) done_cb_(data_);
}

} // namespace qmi
} // namespace odc
