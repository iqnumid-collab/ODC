#include "qmi_hw_info.hpp"
#include <spdlog/spdlog.h>

namespace odc {
namespace qmi {

void QmiHwInfo::collect(QmiServices& qmi, HwInfoCb done) {
    qmi_ = &qmi;
    done_ = std::move(done);
    info_ = {};
    step_manufacturer();
}

void QmiHwInfo::step_manufacturer() {
    if (!qmi_->dms()) { step_model(); return; }
    qmi_client_dms_get_manufacturer(qmi_->dms(), nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* s = static_cast<QmiHwInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_dms_get_manufacturer_finish(QMI_CLIENT_DMS(src), res, &err);
            if (out) {
                const gchar* v = nullptr;
                qmi_message_dms_get_manufacturer_output_get_manufacturer(out, &v, nullptr);
                if (v) s->info_.manufacturer = v;
                qmi_message_dms_get_manufacturer_output_unref(out);
            } else { g_error_free(err); }
            s->step_model();
        }, this);
}

void QmiHwInfo::step_model() {
    if (!qmi_->dms()) { step_revision(); return; }
    qmi_client_dms_get_model(qmi_->dms(), nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* s = static_cast<QmiHwInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_dms_get_model_finish(QMI_CLIENT_DMS(src), res, &err);
            if (out) {
                const gchar* v = nullptr;
                qmi_message_dms_get_model_output_get_model(out, &v, nullptr);
                if (v) s->info_.model = v;
                qmi_message_dms_get_model_output_unref(out);
            } else { g_error_free(err); }
            s->step_revision();
        }, this);
}

void QmiHwInfo::step_revision() {
    if (!qmi_->dms()) { step_hw_revision(); return; }
    qmi_client_dms_get_revision(qmi_->dms(), nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* s = static_cast<QmiHwInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_dms_get_revision_finish(QMI_CLIENT_DMS(src), res, &err);
            if (out) {
                const gchar* v = nullptr;
                qmi_message_dms_get_revision_output_get_revision(out, &v, nullptr);
                if (v) s->info_.revision = v;
                qmi_message_dms_get_revision_output_unref(out);
            } else { g_error_free(err); }
            s->step_hw_revision();
        }, this);
}

void QmiHwInfo::step_hw_revision() {
    if (!qmi_->dms()) { step_ids(); return; }
    qmi_client_dms_get_hardware_revision(qmi_->dms(), nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* s = static_cast<QmiHwInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_dms_get_hardware_revision_finish(QMI_CLIENT_DMS(src), res, &err);
            if (out) {
                const gchar* v = nullptr;
                qmi_message_dms_get_hardware_revision_output_get_revision(out, &v, nullptr);
                if (v) s->info_.hw_revision = v;
                qmi_message_dms_get_hardware_revision_output_unref(out);
            } else { g_error_free(err); }
            s->step_ids();
        }, this);
}

void QmiHwInfo::step_ids() {
    if (!qmi_->dms()) { step_imsi(); return; }
    qmi_client_dms_get_ids(qmi_->dms(), nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* s = static_cast<QmiHwInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_dms_get_ids_finish(QMI_CLIENT_DMS(src), res, &err);
            if (out) {
                const gchar* v = nullptr;
                if (qmi_message_dms_get_ids_output_get_imei(out, &v, nullptr) && v)
                    s->info_.imei = v;
                v = nullptr;
                if (qmi_message_dms_get_ids_output_get_esn(out, &v, nullptr) && v)
                    s->info_.esn = v;
                v = nullptr;
                if (qmi_message_dms_get_ids_output_get_meid(out, &v, nullptr) && v)
                    s->info_.meid = v;
                qmi_message_dms_get_ids_output_unref(out);
            } else { g_error_free(err); }
            s->step_imsi();
        }, this);
}

void QmiHwInfo::step_imsi() {
    if (!qmi_->dms()) { step_iccid(); return; }
    qmi_client_dms_uim_get_imsi(qmi_->dms(), nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* s = static_cast<QmiHwInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_dms_uim_get_imsi_finish(QMI_CLIENT_DMS(src), res, &err);
            if (out) {
                const gchar* v = nullptr;
                qmi_message_dms_uim_get_imsi_output_get_imsi(out, &v, nullptr);
                if (v) s->info_.imsi = v;
                qmi_message_dms_uim_get_imsi_output_unref(out);
            } else { g_error_free(err); }
            s->step_iccid();
        }, this);
}

void QmiHwInfo::step_iccid() {
    if (!qmi_->dms()) { finish(); return; }
    qmi_client_dms_uim_get_iccid(qmi_->dms(), nullptr, 10, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* s = static_cast<QmiHwInfo*>(data);
            GError* err = nullptr;
            auto* out = qmi_client_dms_uim_get_iccid_finish(QMI_CLIENT_DMS(src), res, &err);
            if (out) {
                const gchar* v = nullptr;
                qmi_message_dms_uim_get_iccid_output_get_iccid(out, &v, nullptr);
                if (v) s->info_.iccid = v;
                qmi_message_dms_uim_get_iccid_output_unref(out);
            } else { g_error_free(err); }
            s->finish();
        }, this);
}

void QmiHwInfo::finish() {
    SPDLOG_INFO("qmi: HW info - manuf: {} model: {} fw: {} hw: {}",
                 info_.manufacturer, info_.model, info_.revision, info_.hw_revision);
    SPDLOG_INFO("qmi: HW info - IMEI: {} IMSI: {} ICCID: {}",
                 info_.imei, info_.imsi, info_.iccid);
    if (done_) done_(info_);
}

} // namespace qmi
} // namespace odc
