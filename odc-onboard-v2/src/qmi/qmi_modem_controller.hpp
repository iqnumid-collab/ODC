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
#include <vector>

namespace odc {

class EventBus;

namespace qmi {

struct PdnRuntimeStatus {
    std::string id;
    std::string iface;
    std::string ipv4_address;
    std::string ipv6_address;
    bool connected{false};
};

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

    /** Arm watcher (via start); begin connect path when auto_connect or after this call. */
    bool request_connect();
    bool connect();
    bool disconnect();
    /** Operator / recovery hard reset (DMS and/or USB per config). */
    bool request_hard_reset();
    ModemData get_telemetry() const;
    int pin_retries_left() const { return pin_retries_left_; }
    int soft_fail_count() const { return soft_fail_count_; }
    int active_profile_index() const { return active_profile_index_; }
    const std::string& last_reset_stage() const { return last_reset_stage_; }
    std::vector<PdnRuntimeStatus> pdn_status() const;
    /** Async list of modem 3GPP profiles (requires open WDS client). */
    void list_profiles(std::function<void(bool ok, const std::vector<ProfileInfo>& profiles, const char* err)> cb);

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
    guint           settle_timer_id_ = 0;
    bool            info_collecting_ = false;
    std::string     cfg_device_override_;
    bool            connect_requested_ = false;
    int             pin_retries_left_ = -1;
    int             soft_fail_count_ = 0;
    int             active_profile_index_ = -1;
    std::string     last_reset_stage_;
    bool            hard_reset_pending_ = false;
    std::vector<PdnConfig> pdn_queue_;
    size_t          pdn_index_ = 0;
    std::vector<PdnRuntimeStatus> pdn_status_;

    void glib_thread_func();
    void arm_timeout(uint32_t ms);
    void disarm_timeout();
    void start_watchdog();
    void start_info_timer();
    void stop_info_timer();
    bool should_auto_open() const { return cfg.auto_connect || connect_requested_; }

    void publish_state_change(QmiState old_state, QmiState new_state, const std::string& error = "");
    PdnConfig primary_pdn() const;
    std::vector<PdnConfig> effective_pdns() const;
    void begin_hard_reset_sequence();
    void finish_hard_reset_and_reopen();
    void start_extra_pdns_after_primary();

    // Transition actions
    static void act_open_device(QmiModemController& c);
    static void act_alloc_clients(QmiModemController& c);
    static void act_collect_hw_info(QmiModemController& c);
    static void act_set_online(QmiModemController& c);
    static void act_fcc_unlock(QmiModemController& c);
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
