#pragma once
#include "qmi_events.hpp"
#include <glib.h>
#include <atomic>
#include <functional>
#include <cstddef>
#include <cstdint>

namespace odc {
namespace qmi {

class QmiModemController;

enum class SmEvent {
    START,
    DEVICE_DETECTED,
    DEVICE_REMOVED,
    DEVICE_OPENED,
    CLIENTS_READY,
    HW_INFO_DONE,
    MODEM_ONLINE,
    SIM_READY,
    SIM_PIN_NEEDED,
    SIM_ERROR,
    REGISTERED,
    REGISTRATION_FAILED,
    DATA_CONNECTED,
    DATA_FAILED,
    BEARER_LOST,
    DISCONNECT_REQ,
    DATA_DISCONNECTED,
    TIMEOUT,
    BACKOFF_EXPIRED,
    FATAL,
    _COUNT
};

inline const char* sm_event_name(SmEvent e) {
    static const char* names[] = {
        "START", "DEVICE_DETECTED", "DEVICE_REMOVED", "DEVICE_OPENED",
        "CLIENTS_READY", "HW_INFO_DONE", "MODEM_ONLINE", "SIM_READY",
        "SIM_PIN_NEEDED", "SIM_ERROR", "REGISTERED", "REGISTRATION_FAILED",
        "DATA_CONNECTED", "DATA_FAILED", "BEARER_LOST", "DISCONNECT_REQ",
        "DATA_DISCONNECTED", "TIMEOUT", "BACKOFF_EXPIRED", "FATAL"
    };
    auto i = static_cast<int>(e);
    return (i >= 0 && i < static_cast<int>(SmEvent::_COUNT)) ? names[i] : "?";
}

using GuardFn  = std::function<bool(QmiModemController&)>;
using ActionFn = std::function<void(QmiModemController&)>;

struct Transition {
    QmiState from;
    SmEvent  event;
    QmiState to;
    GuardFn  guard;
    ActionFn action;
};

class QmiStateMachine {
public:
    explicit QmiStateMachine(QmiState initial) : state_(initial) {}

    void set_transitions(const Transition* table, std::size_t count);
    bool process(SmEvent event, QmiModemController& ctrl);

    QmiState state() const { return state_.load(std::memory_order_acquire); }

private:
    std::atomic<QmiState> state_;
    const Transition* table_ = nullptr;
    std::size_t table_size_ = 0;
};

// Backoff timer using GMainContext scheduling
class QmiBackoff {
public:
    using Callback = std::function<void()>;

    QmiBackoff() = default;
    ~QmiBackoff();

    QmiBackoff(const QmiBackoff&) = delete;
    QmiBackoff& operator=(const QmiBackoff&) = delete;

    void configure(uint32_t base_ms, uint32_t max_ms, uint32_t jitter_ms, int max_retries);
    void start(Callback cb);
    void reset();
    void cancel();

    int  attempt() const { return attempt_; }
    bool exhausted() const { return max_retries_ > 0 && attempt_ >= max_retries_; }

private:
    uint32_t base_ms_ = 1000;
    uint32_t max_ms_ = 60000;
    uint32_t jitter_ms_ = 500;
    int max_retries_ = 5;
    int attempt_ = 0;
    guint timer_id_ = 0;
    Callback cb_;

    uint32_t current_delay_ms() const;
};

} // namespace qmi
} // namespace odc
