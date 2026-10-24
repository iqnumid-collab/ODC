#include "qmi_modem_controller.hpp"
#include "core/event_bus.hpp"
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>

namespace odc {
namespace qmi {

const Transition QmiModemController::transition_table[] = {
    { QmiState::Idle,             SmEvent::START,             QmiState::DeviceOpening,       nullptr,                  act_open_device },
    { QmiState::Idle,             SmEvent::DEVICE_DETECTED,   QmiState::DeviceOpening,       nullptr,                  act_open_device },
    { QmiState::DeviceOpening,    SmEvent::DEVICE_OPENED,     QmiState::Initializing,        nullptr,                  act_alloc_clients },
    { QmiState::DeviceOpening,    SmEvent::TIMEOUT,           QmiState::Error,               nullptr,                  act_enter_error },
    { QmiState::DeviceOpening,    SmEvent::DEVICE_REMOVED,    QmiState::Idle,                nullptr,                  act_device_removed },
    { QmiState::Initializing,     SmEvent::CLIENTS_READY,     QmiState::Initializing,        nullptr,                  act_collect_hw_info },
    { QmiState::Initializing,     SmEvent::HW_INFO_DONE,      QmiState::Initializing,        nullptr,                  act_set_online },
    { QmiState::Initializing,     SmEvent::MODEM_ONLINE,      QmiState::SimAuth,             nullptr,                  act_check_sim },
    { QmiState::Initializing,     SmEvent::TIMEOUT,           QmiState::Error,               nullptr,                  act_enter_error },
    { QmiState::Initializing,     SmEvent::DEVICE_REMOVED,    QmiState::Idle,                nullptr,                  act_device_removed },
    { QmiState::SimAuth,          SmEvent::SIM_READY,         QmiState::Registering,         nullptr,                  act_start_reg },
    { QmiState::SimAuth,          SmEvent::SIM_PIN_NEEDED,    QmiState::SimAuth,             nullptr,                  act_verify_pin },
    { QmiState::SimAuth,          SmEvent::SIM_ERROR,         QmiState::Error,               nullptr,                  act_enter_error },
    { QmiState::SimAuth,          SmEvent::TIMEOUT,           QmiState::Error,               nullptr,                  act_enter_error },
    { QmiState::SimAuth,          SmEvent::DEVICE_REMOVED,    QmiState::Idle,                nullptr,                  act_device_removed },
    { QmiState::Registering,      SmEvent::REGISTERED,        QmiState::DataConnecting,      nullptr,                  act_start_data },
    { QmiState::Registering,      SmEvent::TIMEOUT,           QmiState::Error,               nullptr,                  act_enter_error },
    { QmiState::Registering,      SmEvent::REGISTRATION_FAILED, QmiState::Error,             nullptr,                  act_enter_error },
    { QmiState::Registering,      SmEvent::DEVICE_REMOVED,    QmiState::Idle,                nullptr,                  act_device_removed },
    { QmiState::DataConnecting,   SmEvent::DATA_CONNECTED,    QmiState::Connected,           nullptr,                  act_connected },
    { QmiState::DataConnecting,   SmEvent::DATA_FAILED,       QmiState::Error,               nullptr,                  act_enter_error },
    { QmiState::DataConnecting,   SmEvent::TIMEOUT,           QmiState::Error,               nullptr,                  act_enter_error },
    { QmiState::DataConnecting,   SmEvent::DEVICE_REMOVED,    QmiState::Idle,                nullptr,                  act_device_removed },
    { QmiState::Connected,        SmEvent::BEARER_LOST,       QmiState::Error,               nullptr,                  act_enter_error },
    { QmiState::Connected,        SmEvent::DISCONNECT_REQ,    QmiState::DataDisconnecting,   nullptr,                  act_stop_data },
    { QmiState::Connected,        SmEvent::FATAL,             QmiState::Error,               nullptr,                  act_enter_error },
    { QmiState::Connected,        SmEvent::DEVICE_REMOVED,    QmiState::Idle,                nullptr,                  act_device_removed },
    { QmiState::DataDisconnecting, SmEvent::DATA_DISCONNECTED, QmiState::Registering,        nullptr,                  act_cleanup_iface },
    { QmiState::DataDisconnecting, SmEvent::TIMEOUT,          QmiState::Error,               nullptr,                  act_enter_error },
    { QmiState::DataDisconnecting, SmEvent::DEVICE_REMOVED,   QmiState::Idle,                nullptr,                  act_device_removed },
    { QmiState::Error,            SmEvent::BACKOFF_EXPIRED,   QmiState::DeviceOpening,       guard_retries_left,      act_soft_reset },
    { QmiState::Error,            SmEvent::BACKOFF_EXPIRED,   QmiState::Idle,                guard_max_retries,       nullptr },
    { QmiState::Error,            SmEvent::DEVICE_REMOVED,    QmiState::Idle,                nullptr,                  act_device_removed },
    { QmiState::Error,            SmEvent::DEVICE_DETECTED,   QmiState::DeviceOpening,       nullptr,                  act_open_device },
};
const std::size_t QmiModemController::transition_count = sizeof(transition_table) / sizeof(transition_table[0]);

static bool validate_device(const std::string& path) {
    if (path.empty()) return false;
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    if (!S_ISCHR(st.st_mode)) return false;
    if (access(path.c_str(), R_OK | W_OK) != 0) return false;
    return true;
}

QmiModemController::QmiModemController(QmiConfig c)
    : cfg(std::move(c)) {
    sm_.set_transitions(transition_table, transition_count);
    backoff.configure(cfg.backoff_base_ms, cfg.backoff_max_ms, cfg.backoff_jitter_ms, cfg.max_retries);
}

QmiModemController::~QmiModemController() { stop(); }

void QmiModemController::start(EventBus* bus) {
    bus_ = bus;
    running_ = true;

    ctx_ = g_main_context_new();
    g_main_context_ref(ctx_);

    glib_thread_ = std::thread(&QmiModemController::glib_thread_func, this);
}

void QmiModemController::stop() {
    running_ = false;
    disarm_timeout();
    stop_info_timer();
    if (watchdog_id_) { g_source_remove(watchdog_id_); watchdog_id_ = 0; }
    backoff.cancel();

    // Wake up the GLib context so the thread exits its iteration loop
    if (ctx_) {
        g_main_context_wakeup(ctx_);
    }

    if (glib_thread_.joinable()) glib_thread_.join();

    if (ctx_) {
        g_main_context_unref(ctx_);
        ctx_ = nullptr;
    }
}

void QmiModemController::glib_thread_func() {
    g_main_context_push_thread_default(ctx_);

    // Start watcher on this GLib thread so GUdev signals fire here
    watcher.start([this](bool added, const DeviceInfo& info) {
        if (!running_) return;
        if (added) {
            if (!cfg_device_override_.empty() && info.device_path != cfg_device_override_) {
                SPDLOG_DEBUG("qmi: ignoring {} (config specifies {})",
                              info.device_path, cfg_device_override_);
                return;
            }
            auto state = sm_.state();
            if (state != QmiState::Idle && state != QmiState::Error) {
                SPDLOG_DEBUG("qmi: ignoring add, already active in state {}",
                              to_string_view(state));
                return;
            }
            cfg.device_path = info.device_path;
            if (!info.iface_name.empty()) cfg.iface_name = info.iface_name;
            SPDLOG_INFO("qmi: modem detected: {} (driver: {}, iface: {})",
                         cfg.device_path, info.driver, cfg.iface_name);
            inject(SmEvent::DEVICE_DETECTED);
        } else {
            SPDLOG_WARN("qmi: modem removed: {}", info.device_path);
            inject(SmEvent::DEVICE_REMOVED);
        }
    }, ctx_);

    // If device path already specified and exists, start immediately
    if (sm_.state() == QmiState::Idle && !cfg.device_path.empty()
        && cfg.device_path != "/dev/cdc-wdm0") {
        cfg_device_override_ = cfg.device_path;
        if (validate_device(cfg.device_path)) {
            SPDLOG_INFO("qmi: config device {} exists, starting directly", cfg.device_path);
            inject(SmEvent::START);
        }
    }

    // Run the GLib context iteration loop — wakes on events or when stop() calls wakeup
    while (running_) {
        g_main_context_iteration(ctx_, TRUE);
    }

    // Cleanup on this GLib thread (required for GUdev signal disconnection)
    qmi.close();
    watcher.stop();

    g_main_context_pop_thread_default(ctx_);
}

void QmiModemController::inject(SmEvent e) {
    if (!running_ && e != SmEvent::START) return;

    // Must be called from GLib thread
    QmiState old_state = sm_.state();
    sm_.process(e, *this);
    QmiState new_state = sm_.state();

    if (old_state != new_state) {
        publish_state_change(old_state, new_state);
    }
}

QmiState QmiModemController::current_qmi_state() const {
    return sm_.state();
}

bool QmiModemController::connect() {
    if (sm_.state() >= QmiState::Connected) return true;
    inject(SmEvent::START);
    return true;
}

bool QmiModemController::disconnect() {
    if (sm_.state() < QmiState::Connected) return true;
    inject(SmEvent::DISCONNECT_REQ);
    return true;
}

ModemData QmiModemController::get_telemetry() const {
    return last_modem_data;
}

void QmiModemController::publish_state_change(QmiState old_state, QmiState new_state, const std::string& error) {
    if (!bus_) return;

    QmiStateChangeEvent evt;
    evt.device_path = cfg.device_path;
    evt.old_state = old_state;
    evt.new_state = new_state;
    evt.error_message = error;

    // Publish QMI-specific events
    bus_->publish(evt);

    // Also publish as system status event
    SystemStatusEvent sys;
    sys.component = "qmi_modem";
    sys.status = to_string_view(new_state);
    bus_->publish(sys);
}

void QmiModemController::arm_timeout(uint32_t ms) {
    disarm_timeout();
    timeout_id_ = g_timeout_add(ms, [](gpointer data) -> gboolean {
        auto* c = static_cast<QmiModemController*>(data);
        c->timeout_id_ = 0;
        c->inject(SmEvent::TIMEOUT);
        return G_SOURCE_REMOVE;
    }, this);
}

void QmiModemController::disarm_timeout() {
    if (timeout_id_) { g_source_remove(timeout_id_); timeout_id_ = 0; }
}

void QmiModemController::start_watchdog() {
    if (watchdog_id_) return;
    watchdog_id_ = g_timeout_add_seconds(cfg.watchdog_interval_s, [](gpointer data) -> gboolean {
        auto* c = static_cast<QmiModemController*>(data);
        if (c->sm_.state() == QmiState::Connected)
            SPDLOG_DEBUG("qmi: watchdog OK");
        return G_SOURCE_CONTINUE;
    }, this);
}

void QmiModemController::start_info_timer() {
    stop_info_timer();
    if (cfg.modem_info_interval_s == 0) return;
    info_timer_id_ = g_timeout_add_seconds(cfg.modem_info_interval_s, [](gpointer data) -> gboolean {
        auto* c = static_cast<QmiModemController*>(data);
        if (c->sm_.state() != QmiState::Connected || c->info_collecting_) return G_SOURCE_CONTINUE;
        c->info_collecting_ = true;
        c->modem_info.collect_all(c->qmi, [c](const ModemData& d) {
            c->last_modem_data = d;
            c->info_collecting_ = false;

            // Publish telemetry via EventBus
            if (c->bus_) {
                QmiTelemetryEvent tev;
                tev.device_path        = c->cfg.device_path;
                tev.operating_mode     = d.operating_mode;
                tev.link_layer_protocol = d.link_layer_protocol;
                tev.rssi_dbm           = d.rssi_dbm;
                tev.radio_interface    = d.radio_interface;
                tev.lte_rsrp           = d.lte_rsrp;
                tev.lte_snr            = d.lte_snr;
                tev.registration_state = d.registration_state;
                tev.ps_attach_state    = d.ps_attach_state;
                tev.plmn_mcc           = d.plmn_mcc;
                tev.plmn_mnc           = d.plmn_mnc;
                tev.operator_name      = d.operator_name;
                tev.roaming            = d.roaming;
                tev.connection_status  = d.connection_status;
                tev.internet_reachable = d.internet_reachable;
                tev.ping_latency_ms    = d.ping_latency_ms;
                c->bus_->publish(tev);
            }
        }, c->cfg.ping_target, c->cfg.iface_name);
        return G_SOURCE_CONTINUE;
    }, this);
}

void QmiModemController::stop_info_timer() {
    if (info_timer_id_) { g_source_remove(info_timer_id_); info_timer_id_ = 0; }
    info_collecting_ = false;
}

bool QmiModemController::guard_retries_left(QmiModemController& c) { return !c.backoff.exhausted(); }
bool QmiModemController::guard_max_retries(QmiModemController& c)  { return c.backoff.exhausted(); }

// --- Actions ---
void QmiModemController::act_open_device(QmiModemController& c) {
    if (!validate_device(c.cfg.device_path)) {
        SPDLOG_ERROR("qmi: device validation failed for {}", c.cfg.device_path);
        c.inject(SmEvent::TIMEOUT);
        return;
    }
    SPDLOG_INFO("qmi: opening QMI device: {}", c.cfg.device_path);
    c.arm_timeout(c.cfg.open_timeout_ms);
    c.qmi.open_device(c.cfg.device_path.c_str(), [&c](bool ok, const char* err) {
        c.disarm_timeout();
        if (ok) c.inject(SmEvent::DEVICE_OPENED);
        else { SPDLOG_ERROR("qmi: open_device: {}", err); c.inject(SmEvent::TIMEOUT); }
    });
}

void QmiModemController::act_alloc_clients(QmiModemController& c) {
    c.arm_timeout(c.cfg.init_timeout_ms);
    c.qmi.allocate_clients([&c](bool ok, const char* err) {
        if (ok) c.inject(SmEvent::CLIENTS_READY);
        else { SPDLOG_ERROR("qmi: alloc_clients: {}", err); c.inject(SmEvent::TIMEOUT); }
    });
}

void QmiModemController::act_collect_hw_info(QmiModemController& c) {
    c.hw_collector.collect(c.qmi, [&c](const ModemHwInfo& info) {
        c.hw_info = info;

        // Publish HW info via EventBus
        if (c.bus_) {
            QmiHwInfoEvent evt;
            evt.device_path  = c.cfg.device_path;
            evt.manufacturer = info.manufacturer;
            evt.model        = info.model;
            evt.revision     = info.revision;
            evt.hw_revision  = info.hw_revision;
            evt.imei         = info.imei;
            evt.imsi         = info.imsi;
            evt.iccid        = info.iccid;
            evt.esn          = info.esn;
            evt.meid         = info.meid;
            c.bus_->publish(evt);
        }

        c.inject(SmEvent::HW_INFO_DONE);
    });
}

void QmiModemController::act_set_online(QmiModemController& c) {
    c.qmi.set_modem_online([&c](bool ok, const char* err) {
        if (ok) c.inject(SmEvent::MODEM_ONLINE);
        else { SPDLOG_ERROR("qmi: set_online: {}", err); c.inject(SmEvent::TIMEOUT); }
    });
}

void QmiModemController::act_check_sim(QmiModemController& c) {
    c.arm_timeout(c.cfg.sim_timeout_ms);
    c.qmi.get_sim_status([&c](bool ready, bool pin_needed, const char* err) {
        c.disarm_timeout();
        if (ready) { c.inject(SmEvent::SIM_READY); }
        else if (pin_needed) { SPDLOG_WARN("qmi: SIM PIN required"); c.inject(SmEvent::SIM_PIN_NEEDED); }
        else { SPDLOG_ERROR("qmi: SIM: {}", err ? err : "error"); c.inject(SmEvent::SIM_ERROR); }
    });
}

void QmiModemController::act_verify_pin(QmiModemController& c) {
    if (c.cfg.pin.empty()) {
        SPDLOG_ERROR("qmi: PIN required but not configured");
        c.inject(SmEvent::SIM_ERROR);
        return;
    }
    c.arm_timeout(c.cfg.sim_timeout_ms);
    c.qmi.verify_pin(c.cfg.pin.c_str(), [&c](bool ok, const char* err) {
        c.disarm_timeout();
        if (ok) { c.inject(SmEvent::SIM_READY); }
        else { SPDLOG_ERROR("qmi: PIN verify failed: {}", err); c.inject(SmEvent::SIM_ERROR); }
    });
}

void QmiModemController::act_start_reg(QmiModemController& c) {
    SPDLOG_INFO("qmi: starting registration (timeout: {}s)", c.cfg.reg_timeout_ms / 1000);
    c.arm_timeout(c.cfg.reg_timeout_ms);
    c.net.set_raw_ip(c.cfg.iface_name);

    c.qmi.register_indications(
        [&c](bool registered, bool) {
            if (registered) { c.disarm_timeout(); c.inject(SmEvent::REGISTERED); }
        },
        [&c](bool connected) {
            if (!connected && c.sm_.state() == QmiState::Connected)
                c.inject(SmEvent::BEARER_LOST);
        });
}

void QmiModemController::act_start_data(QmiModemController& c) {
    SPDLOG_INFO("qmi: activating data connection (APN: {}, IP type: {})",
                 c.cfg.apn.empty() ? "(SIM default)" : c.cfg.apn, c.cfg.ip_type);
    c.arm_timeout(c.cfg.data_timeout_ms);
    c.qmi.start_network(
        c.cfg.apn.empty() ? nullptr : c.cfg.apn.c_str(),
        c.cfg.ip_type,
        c.cfg.auth_type.c_str(),
        c.cfg.username.empty() ? nullptr : c.cfg.username.c_str(),
        c.cfg.password.empty() ? nullptr : c.cfg.password.c_str(),
        [&c](bool ok, const char* err) {
            if (!ok) {
                SPDLOG_ERROR("qmi: start_network: {}", err);
                c.disarm_timeout();
                c.inject(SmEvent::DATA_FAILED);
                return;
            }
            c.qmi.get_ip_settings([&c](bool ok2, const IpSettings& s, const char* err2) {
                c.disarm_timeout();
                if (!ok2) {
                    SPDLOG_ERROR("qmi: get_settings: {}", err2);
                    c.inject(SmEvent::DATA_FAILED);
                    return;
                }
                c.net.bring_up(c.cfg.iface_name);
                c.net.configure(c.cfg.iface_name, s);

                // Publish connection event
                if (c.bus_) {
                    QmiConnectionEvent con;
                    con.device_path = c.cfg.device_path;
                    con.iface_name  = c.cfg.iface_name;
                    con.ipv4_address = s.ipv4_addr;
                    con.gateway      = s.gateway;
                    con.dns1         = s.dns1;
                    con.dns2         = s.dns2;
                    con.mtu          = s.mtu;
                    con.prefix       = s.prefix;
                    c.bus_->publish(con);
                }
                c.inject(SmEvent::DATA_CONNECTED);
            });
        });
}

void QmiModemController::act_connected(QmiModemController& c) {
    c.backoff.reset();
    c.start_watchdog();
    c.start_info_timer();
    c.info_collecting_ = true;
    c.modem_info.collect_all(c.qmi, [&c](const ModemData& d) {
        c.last_modem_data = d;
        c.info_collecting_ = false;
        if (c.bus_) {
            QmiTelemetryEvent tev;
            tev.device_path        = c.cfg.device_path;
            tev.operating_mode     = d.operating_mode;
            tev.link_layer_protocol = d.link_layer_protocol;
            tev.rssi_dbm           = d.rssi_dbm;
            tev.radio_interface    = d.radio_interface;
            tev.lte_rsrp           = d.lte_rsrp;
            tev.lte_snr            = d.lte_snr;
            tev.registration_state = d.registration_state;
            tev.ps_attach_state    = d.ps_attach_state;
            tev.plmn_mcc           = d.plmn_mcc;
            tev.plmn_mnc           = d.plmn_mnc;
            tev.operator_name      = d.operator_name;
            tev.roaming            = d.roaming;
            tev.connection_status  = d.connection_status;
            tev.internet_reachable = d.internet_reachable;
            tev.ping_latency_ms    = d.ping_latency_ms;
            c.bus_->publish(tev);
        }
    }, c.cfg.ping_target, c.cfg.iface_name);
    SPDLOG_INFO("qmi: === CONNECTED ===");
}

void QmiModemController::act_stop_data(QmiModemController& c) {
    c.arm_timeout(15000);
    c.net.deconfigure(c.cfg.iface_name);
    c.net.bring_down(c.cfg.iface_name);
    c.qmi.stop_network([&c](bool, const char*) {
        c.disarm_timeout();
        c.inject(SmEvent::DATA_DISCONNECTED);
    });
}

void QmiModemController::act_cleanup_iface(QmiModemController& c) {
    c.net.deconfigure(c.cfg.iface_name);
    c.net.bring_down(c.cfg.iface_name);
}

void QmiModemController::act_enter_error(QmiModemController& c) {
    c.disarm_timeout();
    c.stop_info_timer();
    SPDLOG_WARN("qmi: entering ERROR state, attempt {}/{}", c.backoff.attempt() + 1, c.cfg.max_retries);
    c.qmi.close();
    c.net.deconfigure(c.cfg.iface_name);
    c.net.bring_down(c.cfg.iface_name);
    c.backoff.start([&c]() { c.inject(SmEvent::BACKOFF_EXPIRED); });
}

void QmiModemController::act_soft_reset(QmiModemController& c) {
    SPDLOG_INFO("qmi: soft reset — reopening device");
}

void QmiModemController::act_device_removed(QmiModemController& c) {
    SPDLOG_WARN("qmi: modem removed — cleaning up, returning to IDLE");
    c.disarm_timeout();
    c.stop_info_timer();
    c.backoff.cancel();
    c.backoff.reset();
    if (c.watchdog_id_) { g_source_remove(c.watchdog_id_); c.watchdog_id_ = 0; }
    c.qmi.close();
    c.net.deconfigure(c.cfg.iface_name);
    c.net.bring_down(c.cfg.iface_name);
}

} // namespace qmi
} // namespace odc
