#pragma once

namespace uqmi {

enum class State {
    IDLE,
    DEVICE_OPENING,
    INITIALIZING,
    SIM_AUTH,
    REGISTERING,
    DATA_CONNECTING,
    CONNECTED,
    DATA_DISCONNECTING,
    POWER_SAVING,
    ERROR,
    _COUNT
};

enum class Event {
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
    INACTIVITY_PSM,
    WAKE,
    TIMEOUT,
    BACKOFF_EXPIRED,
    FATAL,
    _COUNT
};

inline const char* state_name(State s) {
    static const char* names[] = {
        "IDLE", "DEVICE_OPENING", "INITIALIZING", "SIM_AUTH",
        "REGISTERING", "DATA_CONNECTING", "CONNECTED",
        "DATA_DISCONNECTING", "POWER_SAVING", "ERROR"
    };
    auto i = static_cast<int>(s);
    return (i >= 0 && i < static_cast<int>(State::_COUNT)) ? names[i] : "?";
}

inline const char* event_name(Event e) {
    static const char* names[] = {
        "START", "DEVICE_DETECTED", "DEVICE_REMOVED",
        "DEVICE_OPENED", "CLIENTS_READY", "HW_INFO_DONE", "MODEM_ONLINE",
        "SIM_READY", "SIM_PIN_NEEDED", "SIM_ERROR", "REGISTERED",
        "REGISTRATION_FAILED", "DATA_CONNECTED", "DATA_FAILED",
        "BEARER_LOST", "DISCONNECT_REQ", "DATA_DISCONNECTED",
        "INACTIVITY_PSM", "WAKE", "TIMEOUT", "BACKOFF_EXPIRED", "FATAL"
    };
    auto i = static_cast<int>(e);
    return (i >= 0 && i < static_cast<int>(Event::_COUNT)) ? names[i] : "?";
}

} // namespace uqmi
