#include "qmi_services.h"
#include "log.h"
#include <cstring>

namespace uqmi {

QmiServices::~QmiServices() { close(); }

void QmiServices::close() {
    if (nas_sig_id_ && nas_) { g_signal_handler_disconnect(nas_, nas_sig_id_); nas_sig_id_ = 0; }
    if (wds_sig_id_ && wds_) { g_signal_handler_disconnect(wds_, wds_sig_id_); wds_sig_id_ = 0; }
    if (device_) {
        if (dms_) qmi_device_release_client(device_, QMI_CLIENT(dms_), QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID, 3, nullptr, nullptr, nullptr);
        if (nas_) qmi_device_release_client(device_, QMI_CLIENT(nas_), QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID, 3, nullptr, nullptr, nullptr);
        if (uim_) qmi_device_release_client(device_, QMI_CLIENT(uim_), QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID, 3, nullptr, nullptr, nullptr);
        if (wda_) qmi_device_release_client(device_, QMI_CLIENT(wda_), QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID, 3, nullptr, nullptr, nullptr);
        if (wds_ && pkt_handle_ == 0) qmi_device_release_client(device_, QMI_CLIENT(wds_), QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID, 3, nullptr, nullptr, nullptr);
        qmi_device_close_async(device_, 5, nullptr, nullptr, nullptr);
        g_object_unref(device_);
    }
    device_ = nullptr; dms_ = nullptr; nas_ = nullptr; wds_ = nullptr; uim_ = nullptr; wda_ = nullptr;
}

// ---- open_device ----
struct OpenCtx { ResultCb cb; QmiServices* self; };

void QmiServices::open_device(const char* path, ResultCb cb) {
    auto* file = g_file_new_for_path(path);
    auto* ctx = new OpenCtx{std::move(cb), this};
    qmi_device_new(file, nullptr,
        [](GObject*, GAsyncResult* res, gpointer data) {
            auto* c = static_cast<OpenCtx*>(data);
            GError* err = nullptr;
            c->self->device_ = qmi_device_new_finish(res, &err);
            if (!c->self->device_) {
                c->cb(false, err->message); g_error_free(err); delete c; return;
            }
            qmi_device_open(c->self->device_,
                static_cast<QmiDeviceOpenFlags>(
                    QMI_DEVICE_OPEN_FLAGS_PROXY |
                    QMI_DEVICE_OPEN_FLAGS_NET_RAW_IP |
                    QMI_DEVICE_OPEN_FLAGS_NET_NO_QOS_HEADER),
                30, nullptr,
                [](GObject* src, GAsyncResult* res2, gpointer data2) {
                    auto* c2 = static_cast<OpenCtx*>(data2);
                    GError* e = nullptr;
                    if (!qmi_device_open_finish(QMI_DEVICE(src), res2, &e)) {
                        c2->cb(false, e->message); g_error_free(e);
                    } else {
                        LOG_I("QMI device opened");
                        c2->cb(true, nullptr);
                    }
                    delete c2;
                }, c);
        }, ctx);
    g_object_unref(file);
}

// ---- allocate_clients ----
void QmiServices::on_client_allocated(QmiDevice* dev, GAsyncResult* res) {
    GError* err = nullptr;
    auto* client = qmi_device_allocate_client_finish(dev, res, &err);
    if (!client) {
        LOG_E("client alloc failed: %s", err->message);
        if (alloc_cb_) { alloc_cb_(false, err->message); alloc_cb_ = nullptr; }
        g_error_free(err); return;
    }
    switch (qmi_client_get_service(QMI_CLIENT(client))) {
        case QMI_SERVICE_DMS: dms_ = QMI_CLIENT_DMS(client); break;
        case QMI_SERVICE_NAS: nas_ = QMI_CLIENT_NAS(client); break;
        case QMI_SERVICE_WDS: wds_ = QMI_CLIENT_WDS(client); break;
        case QMI_SERVICE_UIM: uim_ = QMI_CLIENT_UIM(client); break;
        case QMI_SERVICE_WDA: wda_ = QMI_CLIENT_WDA(client); break;
        default: break;
    }
    if (--clients_pending_ == 0 && alloc_cb_) {
        LOG_I("all QMI clients allocated");
        alloc_cb_(true, nullptr); alloc_cb_ = nullptr;
    }
}

void QmiServices::allocate_clients(ResultCb cb) {
    alloc_cb_ = std::move(cb);
    clients_pending_ = 5;
    auto fn = [](GObject* src, GAsyncResult* res, gpointer data) {
        static_cast<QmiServices*>(data)->on_client_allocated(QMI_DEVICE(src), res);
    };
    qmi_device_allocate_client(device_, QMI_SERVICE_DMS, QMI_CID_NONE, 10, nullptr, fn, this);
    qmi_device_allocate_client(device_, QMI_SERVICE_NAS, QMI_CID_NONE, 10, nullptr, fn, this);
    qmi_device_allocate_client(device_, QMI_SERVICE_WDS, QMI_CID_NONE, 10, nullptr, fn, this);
    qmi_device_allocate_client(device_, QMI_SERVICE_UIM, QMI_CID_NONE, 10, nullptr, fn, this);
    qmi_device_allocate_client(device_, QMI_SERVICE_WDA, QMI_CID_NONE, 10, nullptr, fn, this);
}

// ---- set_modem_online ----
struct OnlineCtx { ResultCb cb; QmiServices* self; };

void QmiServices::set_modem_online(ResultCb cb) {
    auto* ctx = new OnlineCtx{std::move(cb), this};
    qmi_client_dms_get_operating_mode(dms_, nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* c = static_cast<OnlineCtx*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_dms_get_operating_mode_finish(QMI_CLIENT_DMS(src), res, &err);
            if (!out) { c->cb(false, err->message); g_error_free(err); delete c; return; }
            QmiDmsOperatingMode mode;
            qmi_message_dms_get_operating_mode_output_get_mode(out, &mode, nullptr);
            qmi_message_dms_get_operating_mode_output_unref(out);
            if (mode == QMI_DMS_OPERATING_MODE_ONLINE) {
                LOG_I("modem already online"); c->cb(true, nullptr); delete c; return;
            }
            auto* input = qmi_message_dms_set_operating_mode_input_new();
            qmi_message_dms_set_operating_mode_input_set_mode(input, QMI_DMS_OPERATING_MODE_ONLINE, nullptr);
            qmi_client_dms_set_operating_mode(c->self->dms_, input, 30, nullptr,
                [](GObject* src2, GAsyncResult* res2, gpointer data2) {
                    auto* c2 = static_cast<OnlineCtx*>(data2);
                    GError* e = nullptr;
                    auto* o = qmi_client_dms_set_operating_mode_finish(QMI_CLIENT_DMS(src2), res2, &e);
                    if (!o) { c2->cb(false, e->message); g_error_free(e); }
                    else { qmi_message_dms_set_operating_mode_output_unref(o); LOG_I("modem set online"); c2->cb(true, nullptr); }
                    delete c2;
                }, c);
            qmi_message_dms_set_operating_mode_input_unref(input);
        }, ctx);
}

// ---- get_sim_status ----
struct SimCtx { std::function<void(bool, bool, const char*)> cb; };

void QmiServices::get_sim_status(std::function<void(bool ready, bool pin_needed, const char* err)> cb) {
    auto* ctx = new SimCtx{std::move(cb)};
    qmi_client_uim_get_card_status(uim_, nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* c = static_cast<SimCtx*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_uim_get_card_status_finish(QMI_CLIENT_UIM(src), res, &err);
            if (!out) { c->cb(false, false, err->message); g_error_free(err); delete c; return; }
            GArray* cards = nullptr;
            qmi_message_uim_get_card_status_output_get_card_status(out, nullptr, nullptr, nullptr, nullptr, &cards, nullptr);
            if (!cards || cards->len == 0) {
                qmi_message_uim_get_card_status_output_unref(out);
                c->cb(false, false, "no SIM"); delete c; return;
            }
            auto& slot = g_array_index(cards, QmiMessageUimGetCardStatusOutputCardStatusCardsElement, 0);
            bool present = (slot.card_state == QMI_UIM_CARD_STATE_PRESENT);
            bool pin_needed = false, ready = false;
            if (present && slot.applications->len > 0) {
                auto& app = g_array_index(slot.applications, QmiMessageUimGetCardStatusOutputCardStatusCardsElementApplicationsElementV2, 0);
                pin_needed = (app.pin1_state == QMI_UIM_PIN_STATE_ENABLED_NOT_VERIFIED);
                ready = (app.pin1_state == QMI_UIM_PIN_STATE_ENABLED_VERIFIED ||
                         app.pin1_state == QMI_UIM_PIN_STATE_DISABLED);
            }
            qmi_message_uim_get_card_status_output_unref(out);
            c->cb(ready, pin_needed, present ? nullptr : "SIM absent");
            delete c;
        }, ctx);
}

// ---- verify_pin ----
struct PinCtx { ResultCb cb; };

void QmiServices::verify_pin(const char* pin, ResultCb cb) {
    auto* ctx = new PinCtx{std::move(cb)};
    auto* input = qmi_message_uim_verify_pin_input_new();
    qmi_message_uim_verify_pin_input_set_info(input, QMI_UIM_PIN_ID_PIN1, pin, nullptr);
    GArray* session_data = g_array_new(FALSE, FALSE, 1);
    qmi_message_uim_verify_pin_input_set_session(input, QMI_UIM_SESSION_TYPE_CARD_SLOT_1, session_data, nullptr);
    qmi_client_uim_verify_pin(uim_, input, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* c = static_cast<PinCtx*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_uim_verify_pin_finish(QMI_CLIENT_UIM(src), res, &err);
            if (!out) { c->cb(false, err->message); g_error_free(err); }
            else { qmi_message_uim_verify_pin_output_unref(out); c->cb(true, nullptr); }
            delete c;
        }, ctx);
    qmi_message_uim_verify_pin_input_unref(input);
    g_array_unref(session_data);
}

// ---- register_indications ----
void QmiServices::register_indications(
    std::function<void(bool registered, bool roaming)> nas_cb,
    std::function<void(bool connected)> wds_cb)
{
    using NasCb = std::function<void(bool, bool)>;
    auto* ncb = new NasCb(std::move(nas_cb));
    nas_sig_id_ = g_signal_connect(nas_, "serving-system",
        G_CALLBACK(+[](QmiClientNas*, QmiIndicationNasServingSystemOutput* out, gpointer data) {
            QmiNasRegistrationState reg;
            qmi_indication_nas_serving_system_output_get_serving_system(out, &reg, nullptr, nullptr, nullptr, nullptr, nullptr);
            bool registered = (reg == QMI_NAS_REGISTRATION_STATE_REGISTERED);
            bool roaming = false; // simplified
            (*static_cast<NasCb*>(data))(registered, roaming);
        }), ncb);

    auto* nas_input = qmi_message_nas_register_indications_input_new();
    qmi_message_nas_register_indications_input_set_serving_system_events(nas_input, TRUE, nullptr);
    qmi_client_nas_register_indications(nas_, nas_input, 10, nullptr, nullptr, nullptr);
    qmi_message_nas_register_indications_input_unref(nas_input);

    // Query current state — if already registered, the indication won't fire
    auto* ncb2 = new NasCb(*ncb);
    qmi_client_nas_get_serving_system(nas_, nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* cb = static_cast<NasCb*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_nas_get_serving_system_finish(QMI_CLIENT_NAS(src), res, &err);
            if (!out) { g_clear_error(&err); delete cb; return; }
            QmiNasRegistrationState reg;
            qmi_message_nas_get_serving_system_output_get_serving_system(out, &reg, nullptr, nullptr, nullptr, nullptr, nullptr);
            qmi_message_nas_get_serving_system_output_unref(out);
            (*cb)(reg == QMI_NAS_REGISTRATION_STATE_REGISTERED, false);
            delete cb;
        }, ncb2);

    using WdsCb = std::function<void(bool)>;
    auto* wcb = new WdsCb(std::move(wds_cb));
    wds_sig_id_ = g_signal_connect(wds_, "packet-service-status",
        G_CALLBACK(+[](QmiClientWds*, QmiIndicationWdsPacketServiceStatusOutput* out, gpointer data) {
            QmiWdsConnectionStatus status;
            qmi_indication_wds_packet_service_status_output_get_connection_status(out, &status, nullptr, nullptr);
            (*static_cast<WdsCb*>(data))(status == QMI_WDS_CONNECTION_STATUS_CONNECTED);
        }), wcb);
}

// ---- start_network ----
struct StartCtx { ResultCb cb; QmiServices* self; };

void QmiServices::start_network(const char* apn, int ip_type, const char* auth,
                                 const char* user, const char* pass, ResultCb cb) {
    auto* ctx = new StartCtx{std::move(cb), this};
    auto* input = qmi_message_wds_start_network_input_new();
    if (apn) qmi_message_wds_start_network_input_set_apn(input, apn, nullptr);
    auto qmi_ip = (ip_type == 6) ? QMI_WDS_IP_FAMILY_IPV6 :
                  (ip_type == 46) ? QMI_WDS_IP_FAMILY_UNSPECIFIED : QMI_WDS_IP_FAMILY_IPV4;
    qmi_message_wds_start_network_input_set_ip_family_preference(input, qmi_ip, nullptr);
    if (auth && std::strcmp(auth, "none") != 0) {
        auto a = (std::strcmp(auth, "pap") == 0) ? QMI_WDS_AUTHENTICATION_PAP : QMI_WDS_AUTHENTICATION_CHAP;
        qmi_message_wds_start_network_input_set_authentication_preference(input, a, nullptr);
        if (user) qmi_message_wds_start_network_input_set_username(input, user, nullptr);
        if (pass) qmi_message_wds_start_network_input_set_password(input, pass, nullptr);
    }
    qmi_client_wds_start_network(wds_, input, 60, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* c = static_cast<StartCtx*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_wds_start_network_finish(QMI_CLIENT_WDS(src), res, &err);
            if (!out) { c->cb(false, err->message); g_error_free(err); delete c; return; }
            guint32 handle;
            qmi_message_wds_start_network_output_get_packet_data_handle(out, &handle, nullptr);
            c->self->pkt_handle_ = handle;
            qmi_message_wds_start_network_output_unref(out);
            LOG_I("network started, handle=%u", handle);
            c->cb(true, nullptr); delete c;
        }, ctx);
    qmi_message_wds_start_network_input_unref(input);
}

// ---- get_ip_settings ----
struct IpCtx { IpResultCb cb; };

static void format_ipv4(guint32 addr, char* buf, size_t len) {
    auto* a = reinterpret_cast<uint8_t*>(&addr);
    snprintf(buf, len, "%u.%u.%u.%u", a[3], a[2], a[1], a[0]);
}

void QmiServices::get_ip_settings(IpResultCb cb) {
    auto* ctx = new IpCtx{std::move(cb)};
    auto* input = qmi_message_wds_get_current_settings_input_new();
    qmi_message_wds_get_current_settings_input_set_requested_settings(input,
        static_cast<QmiWdsRequestedSettings>(
            QMI_WDS_REQUESTED_SETTINGS_DNS_ADDRESS |
            QMI_WDS_REQUESTED_SETTINGS_IP_ADDRESS |
            QMI_WDS_REQUESTED_SETTINGS_GATEWAY_INFO |
            QMI_WDS_REQUESTED_SETTINGS_MTU), nullptr);
    qmi_client_wds_get_current_settings(wds_, input, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* c = static_cast<IpCtx*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_wds_get_current_settings_finish(QMI_CLIENT_WDS(src), res, &err);
            if (!out) { c->cb(false, {}, err->message); g_error_free(err); delete c; return; }
            IpSettings s;
            guint32 v; char buf[32];
            if (qmi_message_wds_get_current_settings_output_get_ipv4_address(out, &v, nullptr)) { format_ipv4(v, buf, sizeof(buf)); s.ipv4_addr = buf; }
            if (qmi_message_wds_get_current_settings_output_get_ipv4_gateway_address(out, &v, nullptr)) { format_ipv4(v, buf, sizeof(buf)); s.gateway = buf; }
            if (qmi_message_wds_get_current_settings_output_get_ipv4_gateway_subnet_mask(out, &v, nullptr)) {
                uint32_t mask = v, p = 0;
                for (; mask; mask &= mask - 1) p++;
                if (p > 0 && p <= 32) s.prefix = p;
            }
            if (qmi_message_wds_get_current_settings_output_get_primary_ipv4_dns_address(out, &v, nullptr)) { format_ipv4(v, buf, sizeof(buf)); s.dns1 = buf; }
            if (qmi_message_wds_get_current_settings_output_get_secondary_ipv4_dns_address(out, &v, nullptr)) { format_ipv4(v, buf, sizeof(buf)); s.dns2 = buf; }
            if (qmi_message_wds_get_current_settings_output_get_mtu(out, &v, nullptr)) s.mtu = v;
            qmi_message_wds_get_current_settings_output_unref(out);
            LOG_I("IP: %s/%u gw: %s dns: %s/%s mtu: %u", s.ipv4_addr.c_str(), s.prefix, s.gateway.c_str(), s.dns1.c_str(), s.dns2.c_str(), s.mtu);
            c->cb(true, s, nullptr); delete c;
        }, ctx);
    qmi_message_wds_get_current_settings_input_unref(input);
}

// ---- stop_network ----
struct StopCtx { ResultCb cb; QmiServices* self; };

void QmiServices::stop_network(ResultCb cb) {
    if (pkt_handle_ == 0) { cb(true, nullptr); return; }
    auto* ctx = new StopCtx{std::move(cb), this};
    auto* input = qmi_message_wds_stop_network_input_new();
    qmi_message_wds_stop_network_input_set_packet_data_handle(input, pkt_handle_, nullptr);
    qmi_client_wds_stop_network(wds_, input, 30, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* c = static_cast<StopCtx*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_wds_stop_network_finish(QMI_CLIENT_WDS(src), res, &err);
            c->self->pkt_handle_ = 0;
            if (!out) { c->cb(false, err->message); g_error_free(err); }
            else { qmi_message_wds_stop_network_output_unref(out); LOG_I("network stopped"); c->cb(true, nullptr); }
            delete c;
        }, ctx);
    qmi_message_wds_stop_network_input_unref(input);
}

} // namespace uqmi
