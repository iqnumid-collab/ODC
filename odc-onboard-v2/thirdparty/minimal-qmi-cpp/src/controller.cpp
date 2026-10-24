#include "controller.h"
#include "kafka/event_publisher.h"
#include "log.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>

namespace uqmi {

// --- Transition table ---
const Transition Controller::transition_table[] = {
    // From               Event                  To                    Guard                Action
    { State::IDLE,        Event::START,          State::DEVICE_OPENING, nullptr,            act_open_device },
    { State::IDLE,        Event::DEVICE_DETECTED,State::DEVICE_OPENING, nullptr,            act_open_device },
    { State::DEVICE_OPENING, Event::DEVICE_OPENED, State::INITIALIZING, nullptr,           act_alloc_clients },
    { State::DEVICE_OPENING, Event::TIMEOUT,     State::ERROR,          nullptr,            act_enter_error },
    { State::DEVICE_OPENING, Event::DEVICE_REMOVED, State::IDLE,       nullptr,            act_device_removed },
    { State::INITIALIZING, Event::CLIENTS_READY, State::INITIALIZING,   nullptr,            act_collect_hw_info },
    { State::INITIALIZING, Event::HW_INFO_DONE, State::INITIALIZING,   nullptr,            act_set_online },
    { State::INITIALIZING, Event::MODEM_ONLINE,  State::SIM_AUTH,       nullptr,            act_check_sim },
    { State::INITIALIZING, Event::TIMEOUT,       State::ERROR,          nullptr,            act_enter_error },
    { State::INITIALIZING, Event::DEVICE_REMOVED, State::IDLE,         nullptr,            act_device_removed },
    { State::SIM_AUTH,    Event::SIM_READY,      State::REGISTERING,    nullptr,            act_start_reg },
    { State::SIM_AUTH,    Event::SIM_PIN_NEEDED, State::SIM_AUTH,       nullptr,            act_verify_pin },
    { State::SIM_AUTH,    Event::SIM_ERROR,      State::ERROR,          nullptr,            act_enter_error },
    { State::SIM_AUTH,    Event::TIMEOUT,        State::ERROR,          nullptr,            act_enter_error },
    { State::SIM_AUTH,    Event::DEVICE_REMOVED, State::IDLE,          nullptr,            act_device_removed },
    { State::REGISTERING, Event::REGISTERED,     State::DATA_CONNECTING,nullptr,            act_start_data },
    { State::REGISTERING, Event::TIMEOUT,        State::ERROR,          nullptr,            act_enter_error },
    { State::REGISTERING, Event::REGISTRATION_FAILED, State::ERROR,    nullptr,            act_enter_error },
    { State::REGISTERING, Event::DEVICE_REMOVED, State::IDLE,          nullptr,            act_device_removed },
    { State::DATA_CONNECTING, Event::DATA_CONNECTED, State::CONNECTED, nullptr,            act_connected },
    { State::DATA_CONNECTING, Event::DATA_FAILED, State::ERROR,        nullptr,            act_enter_error },
    { State::DATA_CONNECTING, Event::TIMEOUT,    State::ERROR,          nullptr,            act_enter_error },
    { State::DATA_CONNECTING, Event::DEVICE_REMOVED, State::IDLE,      nullptr,            act_device_removed },
    { State::CONNECTED,   Event::BEARER_LOST,    State::ERROR,          nullptr,            act_enter_error },
    { State::CONNECTED,   Event::DISCONNECT_REQ, State::DATA_DISCONNECTING, nullptr,       act_stop_data },
    { State::CONNECTED,   Event::FATAL,          State::ERROR,          nullptr,            act_enter_error },
    { State::CONNECTED,   Event::DEVICE_REMOVED, State::IDLE,          nullptr,            act_device_removed },
    { State::DATA_DISCONNECTING, Event::DATA_DISCONNECTED, State::REGISTERING, nullptr,    act_cleanup_iface },
    { State::DATA_DISCONNECTING, Event::TIMEOUT, State::ERROR,         nullptr,            act_enter_error },
    { State::DATA_DISCONNECTING, Event::DEVICE_REMOVED, State::IDLE,   nullptr,            act_device_removed },
    { State::ERROR,       Event::BACKOFF_EXPIRED, State::DEVICE_OPENING, guard_retries_left, act_soft_reset },
    { State::ERROR,       Event::BACKOFF_EXPIRED, State::IDLE,          guard_max_retries,  nullptr },
    { State::ERROR,       Event::DEVICE_REMOVED, State::IDLE,          nullptr,            act_device_removed },
    { State::ERROR,       Event::DEVICE_DETECTED, State::DEVICE_OPENING, nullptr,          act_open_device },
};
const size_t Controller::transition_count = sizeof(transition_table) / sizeof(transition_table[0]);

// --- Device validation ---
static bool validate_device(const std::string& path) {
    if (path.empty()) { LOG_E("device path is empty"); return false; }
    struct stat st;
    if (stat(path.c_str(), &st) != 0) { LOG_E("device %s: does not exist", path.c_str()); return false; }
    if (!S_ISCHR(st.st_mode)) { LOG_E("device %s: not a character device", path.c_str()); return false; }
    if (access(path.c_str(), R_OK | W_OK) != 0) { LOG_E("device %s: no read/write permission", path.c_str()); return false; }
    return true;
}

// --- Constructor / Destructor ---
Controller::Controller(Config c)
    : cfg(std::move(c)), sm_(State::IDLE) {
    sm_.set_transitions(transition_table, transition_count);
    backoff.configure(cfg.backoff_base_ms, cfg.backoff_max_ms, cfg.backoff_jitter_ms, cfg.max_retries);

    orch::Config pool_cfg{.initial_threads = 3, .min_threads = 2, .max_threads = 4};
    pool_cfg.on_error = [](std::exception_ptr ep) {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) { LOG_E("[pool] task error: %s", e.what()); }
        catch (...) { LOG_E("[pool] unknown task error"); }
    };
    pool_ = std::make_unique<orch::ThreadOrchestrator>(pool_cfg);
}

Controller::~Controller() { stop(); }

void Controller::start() {
    running_ = true;

    udev.start([this](bool added, const DeviceInfo& info) {
        if (!running_) return;
        if (added) {
            if (!cfg_device_override_.empty() && info.device_path != cfg_device_override_) {
                LOG_D("udev: ignoring %s (config specifies %s)",
                      info.device_path.c_str(), cfg_device_override_.c_str());
                return;
            }
            if (sm_.state() != State::IDLE && sm_.state() != State::ERROR) {
                LOG_D("udev: ignoring add, already active in state %s", state_name(sm_.state()));
                return;
            }
            cfg.device_path = info.device_path;
            if (!info.iface_name.empty()) cfg.iface_name = info.iface_name;
            LOG_I("modem detected: %s (driver: %s, iface: %s)",
                  cfg.device_path.c_str(), info.driver.c_str(), cfg.iface_name.c_str());

            if (!cfg.config_path.empty()) {
                std::ifstream test(cfg.config_path);
                if (!test.good()) {
                    LOG_I("no config file found, generating default: %s", cfg.config_path.c_str());
                    Config::generate_default(cfg.config_path, info.device_path, info.iface_name);
                    try { cfg = Config::load(cfg.config_path); } catch (...) {}
                    cfg.device_path = info.device_path;
                    if (!info.iface_name.empty()) cfg.iface_name = info.iface_name;
                }
            }
            inject(Event::DEVICE_DETECTED);
        } else {
            LOG_W("modem removed: %s", info.device_path.c_str());
            inject(Event::DEVICE_REMOVED);
        }
    });

    if (sm_.state() == State::IDLE && !cfg.device_path.empty() && cfg.device_path != "/dev/cdc-wdm0") {
        cfg_device_override_ = cfg.device_path;
        if (validate_device(cfg.device_path)) {
            LOG_I("config device %s exists, starting directly", cfg.device_path.c_str());
            inject(Event::START);
        }
    }
}

void Controller::stop() {
    running_ = false;
    disarm_timeout();
    stop_info_timer();
    if (watchdog_id_) { g_source_remove(watchdog_id_); watchdog_id_ = 0; }
    backoff.cancel();
    udev.stop();
    if (pool_) pool_->shutdown();
    qmi.close();
}

void Controller::inject(Event e) {
    if (!running_ && e != Event::START) return;
    sm_.process(e, *this);
}

// --- Timeout helpers ---
void Controller::arm_timeout(uint32_t ms) {
    disarm_timeout();
    timeout_id_ = g_timeout_add(ms, [](gpointer data) -> gboolean {
        auto* c = static_cast<Controller*>(data);
        c->timeout_id_ = 0;
        c->inject(Event::TIMEOUT);
        return G_SOURCE_REMOVE;
    }, this);
}

void Controller::disarm_timeout() {
    if (timeout_id_) { g_source_remove(timeout_id_); timeout_id_ = 0; }
}

void Controller::start_watchdog() {
    if (watchdog_id_) return;
    watchdog_id_ = g_timeout_add_seconds(cfg.watchdog_interval_s, [](gpointer data) -> gboolean {
        auto* c = static_cast<Controller*>(data);
        if (c->sm_.state() == State::CONNECTED) LOG_D("watchdog: connected, OK");
        return G_SOURCE_CONTINUE;
    }, this);
}

void Controller::start_info_timer() {
    stop_info_timer();
    if (cfg.modem_info_interval_s == 0) return;
    info_timer_id_ = g_timeout_add_seconds(cfg.modem_info_interval_s, [](gpointer data) -> gboolean {
        auto* c = static_cast<Controller*>(data);
        if (c->sm_.state() != State::CONNECTED || c->info_collecting_) return G_SOURCE_CONTINUE;
        c->info_collecting_ = true;
        c->modem_info.collect_all(c->qmi, [c](const ModemData& d) {
            c->last_modem_data = d;
            c->info_collecting_ = false;
            // Publish telemetry after each collection
            if (c->publisher_)
                c->publisher_->publish_telemetry(d, c->cfg);
        }, c->cfg.ping_target, c->cfg.iface_name, c->pool_.get());
        return G_SOURCE_CONTINUE;
    }, this);
}

void Controller::stop_info_timer() {
    if (info_timer_id_) { g_source_remove(info_timer_id_); info_timer_id_ = 0; }
    info_collecting_ = false;
}

// --- Guards ---
bool Controller::guard_retries_left(Controller& c) { return !c.backoff.exhausted(); }
bool Controller::guard_max_retries(Controller& c)  { return c.backoff.exhausted(); }

// --- Actions ---
void Controller::act_open_device(Controller& c) {
    if (!validate_device(c.cfg.device_path)) {
        LOG_E("device validation failed for %s", c.cfg.device_path.c_str());
        c.inject(Event::TIMEOUT);
        return;
    }
    LOG_I("opening QMI device: %s", c.cfg.device_path.c_str());
    c.arm_timeout(c.cfg.open_timeout_ms);
    c.qmi.open_device(c.cfg.device_path.c_str(), [&c](bool ok, const char* err) {
        c.disarm_timeout();
        if (ok) c.inject(Event::DEVICE_OPENED);
        else { LOG_E("open_device: %s", err); c.inject(Event::TIMEOUT); }
    });
}

void Controller::act_alloc_clients(Controller& c) {
    LOG_I("allocating QMI clients (DMS, NAS, WDS, UIM, WDA)");
    c.arm_timeout(c.cfg.init_timeout_ms);
    c.qmi.allocate_clients([&c](bool ok, const char* err) {
        if (ok) c.inject(Event::CLIENTS_READY);
        else { LOG_E("alloc_clients: %s", err); c.inject(Event::TIMEOUT); }
    });
}

void Controller::act_collect_hw_info(Controller& c) {
    LOG_I("collecting modem hardware identification");
    c.hw_collector.collect(c.qmi, [&c](const ModemHwInfo& info) {
        c.hw_info = info;
        c.inject(Event::HW_INFO_DONE);
    });
}

void Controller::act_set_online(Controller& c) {
    LOG_I("checking/setting modem operating mode to ONLINE");
    c.qmi.set_modem_online([&c](bool ok, const char* err) {
        c.disarm_timeout();
        if (ok) c.inject(Event::MODEM_ONLINE);
        else { LOG_E("set_online: %s", err); c.inject(Event::TIMEOUT); }
    });
}

void Controller::act_check_sim(Controller& c) {
    LOG_I("checking SIM card status");
    c.arm_timeout(c.cfg.sim_timeout_ms);
    c.qmi.get_sim_status([&c](bool ready, bool pin_needed, const char* err) {
        c.disarm_timeout();
        if (ready) { LOG_I("SIM ready"); c.inject(Event::SIM_READY); }
        else if (pin_needed) { LOG_W("SIM PIN required"); c.inject(Event::SIM_PIN_NEEDED); }
        else { LOG_E("SIM: %s", err ? err : "error"); c.inject(Event::SIM_ERROR); }
    });
}

void Controller::act_verify_pin(Controller& c) {
    if (c.cfg.pin.empty()) {
        LOG_E("PIN required but not configured");
        c.inject(Event::SIM_ERROR);
        return;
    }
    LOG_I("verifying SIM PIN");
    c.arm_timeout(c.cfg.sim_timeout_ms);
    c.qmi.verify_pin(c.cfg.pin.c_str(), [&c](bool ok, const char* err) {
        c.disarm_timeout();
        if (ok) { LOG_I("SIM PIN verified"); c.inject(Event::SIM_READY); }
        else { LOG_E("PIN verify failed: %s", err); c.inject(Event::SIM_ERROR); }
    });
}

void Controller::act_start_reg(Controller& c) {
    LOG_I("starting network registration (timeout: %us)", c.cfg.reg_timeout_ms / 1000);
    c.arm_timeout(c.cfg.reg_timeout_ms);
    c.net.set_raw_ip(c.cfg.iface_name);

    c.qmi.register_indications(
        [&c](bool registered, bool /*roaming*/) {
            if (registered) { c.disarm_timeout(); c.inject(Event::REGISTERED); }
        },
        [&c](bool connected) {
            if (!connected && c.sm_.state() == State::CONNECTED)
                c.inject(Event::BEARER_LOST);
        });
}

void Controller::act_start_data(Controller& c) {
    LOG_I("activating data connection (APN: %s, IP type: %d)",
          c.cfg.apn.empty() ? "(SIM default)" : c.cfg.apn.c_str(), c.cfg.ip_type);
    c.arm_timeout(c.cfg.data_timeout_ms);
    c.qmi.start_network(
        c.cfg.apn.empty() ? nullptr : c.cfg.apn.c_str(),
        c.cfg.ip_type,
        c.cfg.auth_type.c_str(),
        c.cfg.username.empty() ? nullptr : c.cfg.username.c_str(),
        c.cfg.password.empty() ? nullptr : c.cfg.password.c_str(),
        [&c](bool ok, const char* err) {
            if (!ok) { LOG_E("start_network: %s", err); c.disarm_timeout(); c.inject(Event::DATA_FAILED); return; }
            c.qmi.get_ip_settings([&c](bool ok2, const IpSettings& s, const char* err2) {
                c.disarm_timeout();
                if (!ok2) { LOG_E("get_settings: %s", err2); c.inject(Event::DATA_FAILED); return; }
                c.net.bring_up(c.cfg.iface_name);
                c.net.configure(c.cfg.iface_name, s);
                c.inject(Event::DATA_CONNECTED);
            });
        });
}

void Controller::act_connected(Controller& c) {
    c.backoff.reset();
    c.start_watchdog();
    c.start_info_timer();
    c.info_collecting_ = true;
    c.modem_info.collect_all(c.qmi, [&c](const ModemData& d) {
        c.last_modem_data = d;
        c.info_collecting_ = false;
        if (c.publisher_)
            c.publisher_->publish_telemetry(d, c.cfg);
    }, c.cfg.ping_target, c.cfg.iface_name, c.pool_.get());

    // Publish CONNECTED state event with hw_info
    if (c.publisher_)
        c.publisher_->publish_state(State::CONNECTED, c.hw_info, c.cfg);

    LOG_I("=== CONNECTED ===");
}

void Controller::act_stop_data(Controller& c) {
    LOG_I("disconnecting data session");
    c.arm_timeout(15000);
    c.net.deconfigure(c.cfg.iface_name);
    c.net.bring_down(c.cfg.iface_name);
    c.qmi.stop_network([&c](bool, const char*) {
        c.disarm_timeout();
        c.inject(Event::DATA_DISCONNECTED);
    });
}

void Controller::act_cleanup_iface(Controller& c) {
    c.net.deconfigure(c.cfg.iface_name);
    c.net.bring_down(c.cfg.iface_name);
}

void Controller::act_enter_error(Controller& c) {
    c.disarm_timeout();
    c.stop_info_timer();
    LOG_W("entering ERROR state, attempt %d/%d", c.backoff.attempt() + 1, c.cfg.max_retries);
    c.qmi.close();
    c.net.deconfigure(c.cfg.iface_name);
    c.net.bring_down(c.cfg.iface_name);

    if (c.publisher_)
        c.publisher_->publish_state(State::ERROR, {}, c.cfg);

    c.backoff.start([&c]() { c.inject(Event::BACKOFF_EXPIRED); });
}

void Controller::act_soft_reset(Controller& c) {
    LOG_I("soft reset — reopening device");
}

void Controller::act_device_removed(Controller& c) {
    LOG_W("modem removed — cleaning up, returning to IDLE");
    c.disarm_timeout();
    c.stop_info_timer();
    c.backoff.cancel();
    c.backoff.reset();
    if (c.watchdog_id_) { g_source_remove(c.watchdog_id_); c.watchdog_id_ = 0; }
    c.qmi.close();
    c.net.deconfigure(c.cfg.iface_name);
    c.net.bring_down(c.cfg.iface_name);

    if (c.publisher_)
        c.publisher_->publish_state(State::IDLE, {}, c.cfg);

    LOG_I("waiting for modem hotplug...");
}

} // namespace uqmi
