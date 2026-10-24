#include "qmi_modem_controller.hpp"
#include "qmi_usb_reset.hpp"
#include "core/event_bus.hpp"
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <functional>

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
    { QmiState::Error,            SmEvent::START,             QmiState::DeviceOpening,       nullptr,                  act_open_device },
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

PdnConfig QmiModemController::primary_pdn() const {
    auto pdns = effective_pdns();
    return pdns.empty() ? PdnConfig{} : pdns.front();
}

std::vector<PdnConfig> QmiModemController::effective_pdns() const {
    if (!cfg.pdns.empty()) {
        return cfg.pdns;
    }
    PdnConfig p;
    p.id = "internet";
    p.apn = cfg.apn;
    p.ip_type = cfg.ip_type;
    p.iface = cfg.iface_name;
    p.profile_index = cfg.profile_index;
    p.auth_type = cfg.auth_type;
    p.username = cfg.username;
    p.password = cfg.password;
    return {p};
}

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
    if (settle_timer_id_) { g_source_remove(settle_timer_id_); settle_timer_id_ = 0; }
    backoff.cancel();

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
            if (should_auto_open() || hard_reset_pending_) {
                hard_reset_pending_ = false;
                inject(SmEvent::DEVICE_DETECTED);
            } else {
                SPDLOG_INFO("qmi: auto_connect=false; modem armed, waiting for request_connect()");
            }
        } else {
            SPDLOG_WARN("qmi: modem removed: {}", info.device_path);
            inject(SmEvent::DEVICE_REMOVED);
        }
    }, ctx_);

    if (sm_.state() == QmiState::Idle && !cfg.device_path.empty()) {
        if (cfg.device_path != "/dev/cdc-wdm0") {
            cfg_device_override_ = cfg.device_path;
        }
        if (validate_device(cfg.device_path) && should_auto_open()) {
            SPDLOG_INFO("qmi: config device {} exists, starting directly", cfg.device_path);
            inject(SmEvent::START);
        } else if (validate_device(cfg.device_path) && !should_auto_open()) {
            SPDLOG_INFO("qmi: device {} present; auto_connect=false, waiting for request_connect()",
                        cfg.device_path);
        }
    }

    while (running_) {
        g_main_context_iteration(ctx_, TRUE);
    }

    qmi.close();
    watcher.stop();

    g_main_context_pop_thread_default(ctx_);
}

void QmiModemController::inject(SmEvent e) {
    if (!running_ && e != SmEvent::START) return;

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

bool QmiModemController::request_connect() {
    connect_requested_ = true;
    if (!running_ || !ctx_) {
        return false;
    }

    g_main_context_invoke(ctx_, [](gpointer data) -> gboolean {
        auto* c = static_cast<QmiModemController*>(data);
        if (c->sm_.state() != QmiState::Idle && c->sm_.state() != QmiState::Error) {
            return G_SOURCE_REMOVE;
        }
        if (!c->cfg.device_path.empty() && validate_device(c->cfg.device_path)) {
            SPDLOG_INFO("qmi: request_connect starting on {}", c->cfg.device_path);
            c->inject(SmEvent::START);
        } else {
            SPDLOG_INFO("qmi: request_connect armed; waiting for modem detection");
        }
        return G_SOURCE_REMOVE;
    }, this);
    if (ctx_) {
        g_main_context_wakeup(ctx_);
    }
    return true;
}

bool QmiModemController::connect() {
    if (sm_.state() >= QmiState::Connected) return true;
    return request_connect();
}

bool QmiModemController::disconnect() {
    if (sm_.state() < QmiState::Connected) return true;
    connect_requested_ = false;
    if (!ctx_) {
        inject(SmEvent::DISCONNECT_REQ);
        return true;
    }
    g_main_context_invoke(ctx_, [](gpointer data) -> gboolean {
        auto* c = static_cast<QmiModemController*>(data);
        c->inject(SmEvent::DISCONNECT_REQ);
        return G_SOURCE_REMOVE;
    }, this);
    g_main_context_wakeup(ctx_);
    return true;
}

bool QmiModemController::request_hard_reset() {
    if (!running_ || !ctx_) {
        return false;
    }
    g_main_context_invoke(ctx_, [](gpointer data) -> gboolean {
        auto* c = static_cast<QmiModemController*>(data);
        c->begin_hard_reset_sequence();
        return G_SOURCE_REMOVE;
    }, this);
    g_main_context_wakeup(ctx_);
    return true;
}

void QmiModemController::begin_hard_reset_sequence() {
    last_reset_stage_ = "start";
    SPDLOG_WARN("qmi: hard reset sequence (mode={})", cfg.hard_reset_mode);
    disarm_timeout();
    stop_info_timer();
    backoff.cancel();
    connect_requested_ = true; // reopen after settle

    const bool want_dms = cfg.hard_reset_mode == "dms" || cfg.hard_reset_mode == "dms_then_usb";
    const bool want_usb = cfg.hard_reset_mode == "usb" || cfg.hard_reset_mode == "dms_then_usb";

    auto after_dms = [this, want_usb]() {
        qmi.close();
        net.deconfigure(cfg.iface_name);
        net.bring_down(cfg.iface_name);
        if (want_usb) {
            last_reset_stage_ = "usb";
            usb_reset_device(cfg.device_path);
        }
        sm_.force_state(QmiState::Idle);
        finish_hard_reset_and_reopen();
    };

    if (want_dms && qmi.dms()) {
        last_reset_stage_ = "dms";
        qmi.reset_modem([after_dms](bool ok, const char* err) {
            if (!ok) {
                SPDLOG_WARN("qmi: DMS reset failed: {}", err ? err : "?");
            }
            after_dms();
        });
        return;
    }

    after_dms();
}

void QmiModemController::finish_hard_reset_and_reopen() {
    soft_fail_count_ = 0;
    hard_reset_pending_ = true;
    last_reset_stage_ = "settle";
    if (settle_timer_id_) {
        g_source_remove(settle_timer_id_);
        settle_timer_id_ = 0;
    }
    settle_timer_id_ = g_timeout_add(cfg.hard_reset_settle_ms, [](gpointer data) -> gboolean {
        auto* c = static_cast<QmiModemController*>(data);
        c->settle_timer_id_ = 0;
        c->last_reset_stage_ = "reopen";
        c->hard_reset_pending_ = false;
        if (validate_device(c->cfg.device_path)) {
            c->inject(SmEvent::START);
        } else {
            SPDLOG_WARN("qmi: hard reset settle — device not present yet, waiting for hotplug");
            c->hard_reset_pending_ = true;
        }
        return G_SOURCE_REMOVE;
    }, this);
}

ModemData QmiModemController::get_telemetry() const {
    return last_modem_data;
}

std::vector<PdnRuntimeStatus> QmiModemController::pdn_status() const {
    return pdn_status_;
}

void QmiModemController::list_profiles(
    std::function<void(bool ok, const std::vector<ProfileInfo>& profiles, const char* err)> cb) {
    if (!running_ || !ctx_) {
        cb(false, {}, "modem not running");
        return;
    }
    struct ListCtx {
        QmiModemController* self;
        std::function<void(bool, const std::vector<ProfileInfo>&, const char*)> cb;
    };
    auto* lctx = new ListCtx{this, std::move(cb)};
    g_main_context_invoke(ctx_, [](gpointer data) -> gboolean {
        auto* c = static_cast<ListCtx*>(data);
        if (!c->self->qmi.wds()) {
            c->cb(false, {}, "WDS not ready");
            delete c;
            return G_SOURCE_REMOVE;
        }
        c->self->qmi.list_profiles_3gpp(std::move(c->cb));
        delete c;
        return G_SOURCE_REMOVE;
    }, lctx);
    g_main_context_wakeup(ctx_);
}

void QmiModemController::publish_state_change(QmiState old_state, QmiState new_state, const std::string& error) {
    if (!bus_) return;

    QmiStateChangeEvent evt;
    evt.device_path = cfg.device_path;
    evt.old_state = old_state;
    evt.new_state = new_state;
    evt.error_message = error;

    bus_->publish(evt);

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
        if (!ok) {
            SPDLOG_ERROR("qmi: set_online: {}", err);
            c.inject(SmEvent::TIMEOUT);
            return;
        }
        if (c.cfg.fcc_unlock.enabled) {
            act_fcc_unlock(c);
            return;
        }
        c.inject(SmEvent::MODEM_ONLINE);
    });
}

void QmiModemController::act_fcc_unlock(QmiModemController& c) {
    SPDLOG_INFO("qmi: FCC unlock enabled (method={})", c.cfg.fcc_unlock.method);
    c.qmi.fcc_unlock(c.cfg.fcc_unlock.method, [&c](bool ok, const char* err) {
        if (!ok) {
            SPDLOG_ERROR("qmi: FCC unlock failed: {}", err ? err : "?");
            c.publish_state_change(c.sm_.state(), c.sm_.state(), "fcc unlock failed");
            c.inject(SmEvent::TIMEOUT);
            return;
        }
        c.inject(SmEvent::MODEM_ONLINE);
    });
}

void QmiModemController::act_check_sim(QmiModemController& c) {
    c.arm_timeout(c.cfg.sim_timeout_ms);
    c.qmi.get_sim_status([&c](bool ready, bool pin_needed, int pin_retries, const char* err) {
        c.disarm_timeout();
        c.pin_retries_left_ = pin_retries;
        if (ready) { c.inject(SmEvent::SIM_READY); }
        else if (pin_needed) { SPDLOG_WARN("qmi: SIM PIN required (retries_left={})", pin_retries); c.inject(SmEvent::SIM_PIN_NEEDED); }
        else { SPDLOG_ERROR("qmi: SIM: {}", err ? err : "error"); c.inject(SmEvent::SIM_ERROR); }
    });
}

void QmiModemController::act_verify_pin(QmiModemController& c) {
    if (c.cfg.pin.empty()) {
        SPDLOG_ERROR("qmi: PIN required but not configured");
        c.inject(SmEvent::SIM_ERROR);
        return;
    }
    if (c.pin_retries_left_ >= 0 && c.pin_retries_left_ < 2) {
        SPDLOG_ERROR("qmi: refusing PIN verify: only {} attempt(s) left (need >= 2)",
                     c.pin_retries_left_);
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
    auto primary = c.primary_pdn();
    const std::string iface = primary.iface.empty() ? c.cfg.iface_name : primary.iface;
    c.net.set_raw_ip(iface);

    c.qmi.register_indications(
        [&c](bool registered, bool roaming) {
            if (!registered) {
                return;
            }
            if (roaming && !c.cfg.allow_roaming) {
                SPDLOG_ERROR("qmi: registered while roaming but allow_roaming=false");
                c.disarm_timeout();
                c.publish_state_change(c.sm_.state(), c.sm_.state(),
                                       "roaming not allowed by config");
                c.inject(SmEvent::REGISTRATION_FAILED);
                return;
            }
            c.disarm_timeout();
            c.inject(SmEvent::REGISTERED);
        },
        [&c](bool connected) {
            if (!connected && c.sm_.state() == QmiState::Connected)
                c.inject(SmEvent::BEARER_LOST);
        });
}

void QmiModemController::act_start_data(QmiModemController& c) {
    auto primary = c.primary_pdn();
    const std::string iface = primary.iface.empty() ? c.cfg.iface_name : primary.iface;
    c.cfg.iface_name = iface;

    auto do_start = [&c, primary, iface]() {
        SPDLOG_INFO("qmi: activating data (APN: {}, profile: {}, IP type: {})",
                     primary.apn.empty() ? "(default)" : primary.apn,
                     c.active_profile_index_, primary.ip_type);
        c.arm_timeout(c.cfg.data_timeout_ms);

        auto on_started = [&c, iface, primary](bool ok, const char* err) {
            if (!ok) {
                SPDLOG_ERROR("qmi: start_network: {}", err);
                c.disarm_timeout();
                c.inject(SmEvent::DATA_FAILED);
                return;
            }
            if (c.cfg.modem_autoconnect) {
                c.qmi.set_modem_autoconnect(true, [](bool, const char*) {});
            }
            c.qmi.get_ip_settings([&c, iface, primary](bool ok2, const IpSettings& s, const char* err2) {
                c.disarm_timeout();
                if (!ok2) {
                    SPDLOG_ERROR("qmi: get_settings: {}", err2);
                    c.inject(SmEvent::DATA_FAILED);
                    return;
                }
                c.net.bring_up(iface);
                c.net.configure(iface, s);

                if (c.bus_) {
                    QmiConnectionEvent con;
                    con.device_path = c.cfg.device_path;
                    con.iface_name  = iface;
                    con.ipv4_address = s.ipv4_addr;
                    con.ipv6_address = s.ipv6_addr;
                    con.gateway      = s.gateway;
                    con.dns1         = s.dns1;
                    con.dns2         = s.dns2;
                    con.mtu          = s.mtu;
                    con.prefix       = s.prefix;
                    con.ipv6_prefix  = s.ipv6_prefix;
                    con.pdn_id       = primary.id;
                    c.bus_->publish(con);
                }
                {
                    PdnRuntimeStatus st;
                    st.id = primary.id.empty() ? "internet" : primary.id;
                    st.iface = iface;
                    st.ipv4_address = s.ipv4_addr;
                    st.ipv6_address = s.ipv6_addr;
                    st.connected = true;
                    c.pdn_status_.clear();
                    c.pdn_status_.push_back(std::move(st));
                }
                c.inject(SmEvent::DATA_CONNECTED);
            });
        };

        if (c.active_profile_index_ >= 0) {
            c.qmi.start_network_by_profile(static_cast<guint8>(c.active_profile_index_),
                                           primary.ip_type, on_started);
        } else {
            c.qmi.start_network(
                primary.apn.empty() ? nullptr : primary.apn.c_str(),
                primary.ip_type,
                primary.auth_type.c_str(),
                primary.username.empty() ? nullptr : primary.username.c_str(),
                primary.password.empty() ? nullptr : primary.password.c_str(),
                on_started);
        }
    };

    c.active_profile_index_ = primary.profile_index;
    if (c.cfg.ensure_profile && !primary.apn.empty()) {
        if (primary.profile_index >= 0) {
            c.qmi.modify_profile_3gpp(
                static_cast<guint8>(primary.profile_index),
                primary.apn.c_str(), primary.ip_type, primary.auth_type.c_str(),
                primary.username.empty() ? nullptr : primary.username.c_str(),
                primary.password.empty() ? nullptr : primary.password.c_str(),
                [&c, do_start](bool ok, const char* err) {
                    if (!ok) {
                        SPDLOG_WARN("qmi: modify_profile failed ({}), creating new", err ? err : "?");
                        c.qmi.create_profile_3gpp(
                            c.primary_pdn().apn.c_str(), c.primary_pdn().ip_type,
                            c.primary_pdn().auth_type.c_str(),
                            c.primary_pdn().username.empty() ? nullptr : c.primary_pdn().username.c_str(),
                            c.primary_pdn().password.empty() ? nullptr : c.primary_pdn().password.c_str(),
                            [&c, do_start](bool ok2, int idx, const char* err2) {
                                if (!ok2) {
                                    SPDLOG_ERROR("qmi: create_profile: {}", err2);
                                    c.inject(SmEvent::DATA_FAILED);
                                    return;
                                }
                                c.active_profile_index_ = idx;
                                do_start();
                            });
                        return;
                    }
                    c.active_profile_index_ = c.primary_pdn().profile_index;
                    do_start();
                });
            return;
        }
        c.qmi.create_profile_3gpp(
            primary.apn.c_str(), primary.ip_type, primary.auth_type.c_str(),
            primary.username.empty() ? nullptr : primary.username.c_str(),
            primary.password.empty() ? nullptr : primary.password.c_str(),
            [&c, do_start](bool ok, int idx, const char* err) {
                if (!ok) {
                    SPDLOG_ERROR("qmi: create_profile: {}", err);
                    c.inject(SmEvent::DATA_FAILED);
                    return;
                }
                c.active_profile_index_ = idx;
                do_start();
            });
        return;
    }

    do_start();
}

void QmiModemController::start_extra_pdns_after_primary() {
    auto pdns = effective_pdns();
    if (pdns.size() <= 1) {
        return;
    }
    for (size_t i = 1; i < pdns.size(); ++i) {
        const auto pdn = pdns[i];
        const std::string iface = pdn.iface.empty() ? cfg.iface_name : pdn.iface;
        qmi.allocate_extra_wds(pdn.id, iface, [this, pdn, iface](bool ok, const char* err) {
            if (!ok) {
                SPDLOG_ERROR("qmi: extra WDS for {}: {}", pdn.id, err ? err : "?");
                return;
            }
            net.set_raw_ip(iface);
            qmi.start_session_network(
                pdn.id,
                pdn.apn.empty() ? nullptr : pdn.apn.c_str(),
                pdn.ip_type,
                pdn.auth_type.c_str(),
                pdn.username.empty() ? nullptr : pdn.username.c_str(),
                pdn.password.empty() ? nullptr : pdn.password.c_str(),
                pdn.profile_index,
                [this, pdn, iface](bool ok2, const char* err2) {
                    if (!ok2) {
                        SPDLOG_ERROR("qmi: PDN {} start failed: {}", pdn.id, err2 ? err2 : "?");
                        return;
                    }
                    qmi.get_session_ip_settings(pdn.id, [this, pdn, iface](bool ok3, const IpSettings& s, const char*) {
                        if (!ok3) return;
                        net.bring_up(iface);
                        net.configure(iface, s);
                        if (bus_) {
                            QmiConnectionEvent con;
                            con.device_path = cfg.device_path;
                            con.iface_name = iface;
                            con.ipv4_address = s.ipv4_addr;
                            con.ipv6_address = s.ipv6_addr;
                            con.pdn_id = pdn.id;
                            bus_->publish(con);
                        }
                        {
                            PdnRuntimeStatus st;
                            st.id = pdn.id;
                            st.iface = iface;
                            st.ipv4_address = s.ipv4_addr;
                            st.ipv6_address = s.ipv6_addr;
                            st.connected = true;
                            bool updated = false;
                            for (auto& existing : pdn_status_) {
                                if (existing.id == pdn.id) {
                                    existing = st;
                                    updated = true;
                                    break;
                                }
                            }
                            if (!updated) pdn_status_.push_back(std::move(st));
                        }
                        SPDLOG_INFO("qmi: secondary PDN {} connected on {}", pdn.id, iface);
                    });
                });
        });
    }
}

void QmiModemController::act_connected(QmiModemController& c) {
    c.backoff.reset();
    c.soft_fail_count_ = 0;
    c.last_reset_stage_.clear();
    c.start_watchdog();
    c.start_info_timer();
    c.start_extra_pdns_after_primary();
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
    c.pdn_status_.clear();
    c.arm_timeout(15000);
    // Stop extra sessions first (best-effort)
    for (const auto& s : c.qmi.sessions()) {
        c.qmi.stop_session_network(s.id, [](bool, const char*) {});
        c.net.deconfigure(s.iface);
        c.net.bring_down(s.iface);
    }
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
    c.pdn_status_.clear();
    c.disarm_timeout();
    c.stop_info_timer();
    SPDLOG_WARN("qmi: entering ERROR state, attempt {}/{}", c.backoff.attempt() + 1, c.cfg.max_retries);
    c.qmi.close();
    c.net.deconfigure(c.cfg.iface_name);
    c.net.bring_down(c.cfg.iface_name);
    c.backoff.start([&c]() { c.inject(SmEvent::BACKOFF_EXPIRED); });
}

void QmiModemController::act_soft_reset(QmiModemController& c) {
    c.soft_fail_count_++;
    SPDLOG_INFO("qmi: soft reset — reopening device (soft_fail={})", c.soft_fail_count_);

    if (c.cfg.hard_reset_after_soft_fails > 0 &&
        c.soft_fail_count_ > c.cfg.hard_reset_after_soft_fails) {
        SPDLOG_WARN("qmi: escalating to hard reset after {} soft fails", c.soft_fail_count_);
        // Device already closed in act_enter_error; USB reset then reopen.
        c.last_reset_stage_ = "usb";
        if (c.cfg.hard_reset_mode != "dms") {
            usb_reset_device(c.cfg.device_path);
        }
        c.soft_fail_count_ = 0;
        c.hard_reset_pending_ = true;
        c.settle_timer_id_ = g_timeout_add(c.cfg.hard_reset_settle_ms, [](gpointer data) -> gboolean {
            auto* ctrl = static_cast<QmiModemController*>(data);
            ctrl->settle_timer_id_ = 0;
            ctrl->hard_reset_pending_ = false;
            ctrl->last_reset_stage_ = "reopen";
            act_open_device(*ctrl);
            return G_SOURCE_REMOVE;
        }, &c);
        return;
    }

    act_open_device(c);
}

void QmiModemController::act_device_removed(QmiModemController& c) {
    c.pdn_status_.clear();
    SPDLOG_WARN("qmi: modem removed — cleaning up, returning to IDLE");
    c.disarm_timeout();
    c.stop_info_timer();
    c.backoff.cancel();
    c.backoff.reset();
    c.soft_fail_count_ = 0;
    if (c.watchdog_id_) { g_source_remove(c.watchdog_id_); c.watchdog_id_ = 0; }
    if (c.settle_timer_id_) { g_source_remove(c.settle_timer_id_); c.settle_timer_id_ = 0; }
    c.qmi.close();
    c.net.deconfigure(c.cfg.iface_name);
    c.net.bring_down(c.cfg.iface_name);
}

} // namespace qmi
} // namespace odc
