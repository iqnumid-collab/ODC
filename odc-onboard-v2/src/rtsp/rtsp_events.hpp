#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <cstdint>

namespace odc {

enum class RtspCompressorEventType : uint8_t {
    Started, Stopped,
    AbrStepDown, AbrStepUp,
    SourceLost, SourceRecovered,
    ClientConnected, ClientDisconnected,
    AuthChanged
};

inline std::string_view to_string_view(RtspCompressorEventType type) {
    switch (type) {
        case RtspCompressorEventType::Started:           return "started";
        case RtspCompressorEventType::Stopped:           return "stopped";
        case RtspCompressorEventType::AbrStepDown:       return "abr_step_down";
        case RtspCompressorEventType::AbrStepUp:         return "abr_step_up";
        case RtspCompressorEventType::SourceLost:        return "source_lost";
        case RtspCompressorEventType::SourceRecovered:   return "source_recovered";
        case RtspCompressorEventType::ClientConnected:   return "client_connected";
        case RtspCompressorEventType::ClientDisconnected: return "client_disconnected";
        case RtspCompressorEventType::AuthChanged:       return "auth_changed";
    }
    return "unknown";
}

struct RtspCompressorStatusEvent {
    std::string connection_id;
    std::string interface_name;
    std::string target_ip;
    bool running{false};
    bool reconnecting{false};
    bool abr_enabled{false};
    int abr_bitrate_kbps{0};
    double bitrate_kbps{0};
    double fps{0};
    double compression_ratio{0};
    int connected_clients{0};
    double worst_client_loss_pct{0};
    std::string error_message;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct RtspCompressorEvent {
    std::string connection_id;
    RtspCompressorEventType type;
    std::string detail;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

}
