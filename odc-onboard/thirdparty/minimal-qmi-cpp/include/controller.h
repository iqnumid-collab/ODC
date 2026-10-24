#pragma once
#include "state_machine.h"
#include "qmi_services.h"
#include "net_iface.h"
#include "backoff.h"
#include "config.h"
#include "modem_info.h"
#include "hw_info.h"
#include "udev_monitor.h"
#include <thread_orchestrator/thread_orchestrator.hpp>
#include <glib.h>
#include <memory>

namespace uqmi {

class EventPublisher; // forward declaration

class Controller {
public:
    explicit Controller(Config cfg);
    ~Controller();

    void start();
    void stop();
    void inject(Event e);

    // Set optional Kafka publisher (called from main before start())
    void set_publisher(EventPublisher* pub) { publisher_ = pub; }

    // State accessor (safe to call from any thread — atomic)
    State sm_state() const { return sm_.state(); }

    // Thread pool for background work (Kafka, blocking I/O)
    orch::ThreadOrchestrator& pool() { return *pool_; }

    // Accessed by guards/actions
    Config      cfg;
    QmiServices qmi;
    NetIface    net;
    Backoff     backoff;
    ModemInfo       modem_info;
    ModemData       last_modem_data;
    HwInfoCollector hw_collector;
    ModemHwInfo     hw_info;
    UdevMonitor     udev;

private:
    StateMachine   sm_;
    std::unique_ptr<orch::ThreadOrchestrator> pool_;
    EventPublisher* publisher_ = nullptr;
    guint        timeout_id_ = 0;
    guint        watchdog_id_ = 0;
    guint        info_timer_id_ = 0;
    bool         info_collecting_ = false;
    bool         running_ = false;
    std::string  cfg_device_override_;

    void arm_timeout(uint32_t ms);
    void disarm_timeout();
    void start_watchdog();
    void start_info_timer();
    void stop_info_timer();

    // Transition actions
    static void act_open_device(Controller& c);
    static void act_alloc_clients(Controller& c);
    static void act_collect_hw_info(Controller& c);
    static void act_set_online(Controller& c);
    static void act_check_sim(Controller& c);
    static void act_verify_pin(Controller& c);
    static void act_start_reg(Controller& c);
    static void act_start_data(Controller& c);
    static void act_configure_iface(Controller& c);
    static void act_stop_data(Controller& c);
    static void act_cleanup_iface(Controller& c);
    static void act_enter_error(Controller& c);
    static void act_soft_reset(Controller& c);
    static void act_connected(Controller& c);
    static void act_device_removed(Controller& c);

    // Guards
    static bool guard_retries_left(Controller& c);
    static bool guard_max_retries(Controller& c);

    static const Transition transition_table[];
    static const size_t     transition_count;
};

} // namespace uqmi
