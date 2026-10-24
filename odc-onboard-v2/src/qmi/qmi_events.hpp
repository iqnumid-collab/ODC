#pragma once
#include <string>
#include <chrono>
#include <cstdint>

namespace odc {
namespace qmi {

enum class QmiState : uint8_t {
    Idle,
    DeviceOpening,
    Initializing,
    SimAuth,
    Registering,
    DataConnecting,
    Connected,
    DataDisconnecting,
    Error
};

struct QmiStateChangeEvent {
    std::string device_path;
    QmiState old_state;
    QmiState new_state;
    std::string error_message;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct QmiConnectionEvent {
    std::string device_path;
    std::string iface_name;
    std::string ipv4_address;
    std::string ipv6_address;
    std::string gateway;
    std::string dns1;
    std::string dns2;
    uint32_t mtu{0};
    uint32_t prefix{32};
    uint8_t  ipv6_prefix{64};
    std::string pdn_id; // empty = primary
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct QmiTelemetryEvent {
    std::string device_path;
    int      operating_mode{-1};
    int      link_layer_protocol{-1};
    int8_t   rssi_dbm{0};
    int      radio_interface{-1};
    int16_t  lte_rsrp{0};
    float    lte_snr{0.0f};
    int      registration_state{-1};
    int      ps_attach_state{-1};
    uint16_t plmn_mcc{0};
    uint16_t plmn_mnc{0};
    std::string operator_name;
    int      roaming{-1};
    int      connection_status{-1};
    bool     internet_reachable{false};
    int      ping_latency_ms{-1};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct QmiHwInfoEvent {
    std::string device_path;
    std::string manufacturer;
    std::string model;
    std::string revision;
    std::string hw_revision;
    std::string imei;
    std::string imsi;
    std::string iccid;
    std::string esn;
    std::string meid;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

inline const char* to_string_view(QmiState state) {
    switch (state) {
        case QmiState::Idle: return "IDLE";
        case QmiState::DeviceOpening: return "DEVICE_OPENING";
        case QmiState::Initializing: return "INITIALIZING";
        case QmiState::SimAuth: return "SIM_AUTH";
        case QmiState::Registering: return "REGISTERING";
        case QmiState::DataConnecting: return "DATA_CONNECTING";
        case QmiState::Connected: return "CONNECTED";
        case QmiState::DataDisconnecting: return "DATA_DISCONNECTING";
        case QmiState::Error: return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace qmi
} // namespace odc
