#pragma once
#include "qmi_config.hpp"
#include "qmi_events.hpp"
#include "qmi_state_machine.hpp"
#include "qmi_services.hpp"
#include "qmi_net_iface.hpp"
#include "qmi_modem_info.hpp"
#include "qmi_hw_info.hpp"
#include "qmi_watcher.hpp"
#include <glib.h>
#include <memory>
#include <atomic>
#include <thread>
#include <string>

namespace odc {

class EventBus;

namespace qmi {

class QmiModemController {
public:
    explicit QmiModemController(QmiConfig cfg);
    ~QmiModemController();

    QmiModemController(const QmiModemController&) = delete;
    QmiModemController& operator=(const QmiModemController&) = delete;

    void start(EventBus* bus);
    void stop();

    void inject(SmEvent e);

    QmiState sm_state() const { return sm_.state(); }
    QmiState current_qmi_state() const;

    bool connect();
    bool disconnect();
    ModemData get_telemetry() const;

    // Config transport
    QmiConfig      cfg;
    QmiServices    qmi;
    QmiNetIface    net;
    QmiBackoff     backoff;
    QmiModemInfo   modem_info;
    ModemData      last_modem_data;
    QmiHwInfo      hw_collector;
    ModemHwInfo    hw_info;
    QmiWatcher     watcher;

private:
    QmiStateMachine sm_{QmiState::Idle};
    EventBus*       bus_ = nullptr;
    std::thread     glib_thread_;
    GMainContext*   ctx_ = nullptr;
    bool            running_ = false;
    guint           timeout_id_ = 0;
    guint           watchdog_id_ = 0;
    guint           info_timer_id_ = 0;
    bool            info_collecting_ = false;
    std::string     cfg_device_override_;

    void glib_thread_func();
    void arm_timeout(uint32_t ms);
    void disarm_timeout();
    void start_watchdog();
    void start_info_timer();
    void stop_info_timer();

    void publish_state_change(QmiState old_state, QmiState new_state, const std::string& error = "");

    // Transition actions
    static void act_open_device(QmiModemController& c);
    static void act_alloc_clients(QmiModemController& c);
    static void act_collect_hw_info(QmiModemController& c);
    static void act_set_online(QmiModemController& c);
    static void act_check_sim(QmiModemController& c);
    static void act_verify_pin(QmiModemController& c);
    static void act_start_reg(QmiModemController& c);
    static void act_start_data(QmiModemController& c);
    static void act_connected(QmiModemController& c);
    static void act_stop_data(QmiModemController& c);
    static void act_cleanup_iface(QmiModemController& c);
    static void act_enter_error(QmiModemController& c);
    static void act_soft_reset(QmiModemController& c);
    static void act_device_removed(QmiModemController& c);

    // Guards
    static bool guard_retries_left(QmiModemController& c);
    static bool guard_max_retries(QmiModemController& c);

    static const Transition transition_table[];
    static const std::size_t transition_count;
};

} // namespace qmi
} // namespace odc
