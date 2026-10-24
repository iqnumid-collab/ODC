#pragma once

#include "mavlink/mav_events.hpp"
#include "qmi/qmi_events.hpp"
#include "rtsp/rtsp_events.hpp"

#include <chrono>
#include <string>
#include <string_view>
#include <variant>
#include <cstdint>

namespace odc {

enum class LogLevel : uint8_t {
    Trace = 0,
    Debug = 1,
    Info    = 2,
    Warn  = 3,
    Error = 4,
    Critical = 5,
    Off   = 6
};

enum class VpnType : uint8_t {
    WireGuard,
    OpenVPN
};

enum class VpnState : uint8_t {
    Idle,
    Connecting,
    Connected,
    Disconnecting,
    Disconnected,
    Error,
    Reconnecting
};

struct LogEvent {
    LogLevel level;
    std::string logger_name;
    std::string message;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct SetLogLevelEvent {
    std::string logger_name;
    LogLevel level;
};

struct MetricsSnapshotEvent {
    uint64_t active_tasks;
    uint64_t pending_tasks;
    uint64_t completed_tasks;
    std::size_t worker_count;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct SystemStatusEvent {
    std::string component;
    std::string status;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct ShutdownEvent {
    int exit_code{0};
    std::string reason;
};

struct VpnStateChangeEvent {
    std::string connection_id;
    VpnType type;
    VpnState old_state;
    VpnState new_state;
    std::string error_message;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct VpnStatsEvent {
    std::string connection_id;
    VpnType type;
    uint64_t bytes_in;
    uint64_t bytes_out;
    uint64_t packets_in;
    uint64_t packets_out;
    double duration_sec;
    int reconnect_count;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct VpnHealthCheckEvent {
    std::string connection_id;
    VpnType type;
    bool healthy;
    double latency_ms;
    int consecutive_failures;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

enum class VpnInterfaceAction : uint8_t {
    InterfaceCreated,
    InterfaceDeleted,
    InterfaceUp,
    InterfaceDown,
    AddressAdded,
    AddressRemoved,
};

struct VpnInterfaceEvent {
    std::string connection_id;
    std::string interface_name;
    VpnInterfaceAction action;
    std::string ip_address;
    int prefix_len{0};
    bool ipv6{false};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct MavExtenderStatusEvent {
    std::string extender_id;
    std::string vpn_connection_id;
    std::string vpn_interface_name;
    std::string target_ip;
    uint16_t target_port{14580};
    bool connected{false};
    uint64_t messages_in{0};
    uint64_t messages_out{0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

using Event = std::variant<
    LogEvent,
    SetLogLevelEvent,
    MetricsSnapshotEvent,
    SystemStatusEvent,
    ShutdownEvent,
    VpnStateChangeEvent,
    VpnStatsEvent,
    VpnHealthCheckEvent,
    VpnInterfaceEvent,
    MavExtenderStatusEvent,
    MavRouterStatusEvent,
    MavEndpointStatusEvent,
    MavRoutingStatsEvent,
    MavLogStatusEvent,
    qmi::QmiStateChangeEvent,
    qmi::QmiConnectionEvent,
    qmi::QmiTelemetryEvent,
    qmi::QmiHwInfoEvent,
    RtspCompressorStatusEvent,
    RtspCompressorEvent
>;

inline std::string_view to_string_view(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:    return "TRACE";
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warn:     return "WARN";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
        case LogLevel::Off:      return "OFF";
    }
    return "UNKNOWN";
}

inline std::string_view to_string_view(VpnType type) {
    switch (type) {
        case VpnType::WireGuard: return "WireGuard";
        case VpnType::OpenVPN:   return "OpenVPN";
    }
    return "Unknown";
}

inline std::string_view to_string_view(VpnState state) {
    switch (state) {
        case VpnState::Idle:          return "idle";
        case VpnState::Connecting:    return "connecting";
        case VpnState::Connected:     return "connected";
        case VpnState::Disconnecting: return "disconnecting";
        case VpnState::Disconnected:  return "disconnected";
        case VpnState::Error:         return "error";
        case VpnState::Reconnecting:  return "reconnecting";
    }
    return "unknown";
}

inline std::string_view to_string_view(VpnInterfaceAction action) {
    switch (action) {
        case VpnInterfaceAction::InterfaceCreated:  return "interface_created";
        case VpnInterfaceAction::InterfaceDeleted:  return "interface_deleted";
        case VpnInterfaceAction::InterfaceUp:       return "interface_up";
        case VpnInterfaceAction::InterfaceDown:     return "interface_down";
        case VpnInterfaceAction::AddressAdded:      return "address_added";
        case VpnInterfaceAction::AddressRemoved:    return "address_removed";
    }
    return "unknown";
}

inline std::string_view mav_autopilot_name(uint8_t type) {
    switch (type) {
        case 0:  return "Generic";
        case 1:  return "Reserved";
        case 2:  return "SLUGS";
        case 3:  return "ArduPilot";
        case 4:  return "OpenPilot";
        case 5:  return "Generic v2";
        case 6:  return "PX4";
        case 7:  return "SMACCMPilot";
        case 8:  return "AutoQuad";
        case 9:  return "ArduPilotMega PLANNER";
        case 10: return "AI Brain";
        case 11: return "FP";
        case 12: return "PX4v5";
        case 13: return "Bagel";
        default: return "Unknown";
    }
}

inline std::string_view mav_system_status_name(uint8_t status) {
    switch (status) {
        case 0:  return "Uninitialized";
        case 1:  return "Boot";
        case 2:  return "Calibrating";
        case 3:  return "Standby";
        case 4:  return "Active";
        case 5:  return "Critical";
        case 6:  return "Emergency";
        case 7:  return "PowerOff";
        case 8:  return "Flight Termination";
        default: return "Unknown";
    }
}

}
