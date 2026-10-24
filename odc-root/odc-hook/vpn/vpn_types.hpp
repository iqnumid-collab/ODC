#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace odc::hook {

enum class VpnType : uint8_t {
    WireGuard = 0,
    OpenVPN   = 1,
};

enum class VpnState : uint8_t {
    Idle          = 0,
    Connecting    = 1,
    Connected     = 2,
    Disconnecting = 3,
    Disconnected  = 4,
    Error         = 5,
    Reconnecting  = 6,
};

struct VpnStats {
    uint64_t bytes_in{0};
    uint64_t bytes_out{0};
    uint64_t packets_in{0};
    uint64_t packets_out{0};
    double duration_sec{0};
    int reconnect_count{0};
};

inline const char* to_string(VpnType t) {
    switch (t) {
        case VpnType::WireGuard: return "wireguard";
        case VpnType::OpenVPN:   return "openvpn";
    }
    return "unknown";
}

inline const char* to_string(VpnState s) {
    switch (s) {
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

namespace wg {

using WgKey   = std::array<uint8_t, 32>;
using WgNonce = std::array<uint8_t, 12>;
using WgTag   = std::array<uint8_t, 16>;
using WgCookie = std::array<uint8_t, 16>;

enum class WgMessageType : uint8_t {
    Invalid              = 0,
    HandshakeInitiation  = 1,
    HandshakeResponse    = 2,
    CookieReply          = 3,
    TransportData        = 4,
};

enum class WgPeerState : uint8_t {
    Unknown,
    HandshakeInitiationSent,
    HandshakeResponseReceived,
    Connected,
    Dead,
};

struct WgPeerConfig {
    WgKey public_key{};
    WgKey preshared_key{};
    std::string endpoint;
    std::vector<std::string> allowed_ips;
    uint16_t persistent_keepalive_interval{25};
};

struct WgDeviceConfig {
    WgKey private_key{};
    uint16_t listen_port{0};
    std::vector<WgPeerConfig> peers;
    std::string tun_name{"odc-wg"};
    std::string tun_address{"10.0.0.2"};
    int tun_prefix_len{24};
    int tun_mtu{1420};
};

constexpr size_t  WG_MESSAGE_PREFIX_LEN   = 4;
constexpr size_t  WG_HANDSHAKE_INIT_LEN   = 148;
constexpr size_t  WG_HANDSHAKE_RESP_LEN   = 92;
constexpr size_t  WG_COOKIE_REPLY_LEN     = 64;
constexpr size_t  WG_DATA_OVERHEAD        = 32;
constexpr size_t  WG_TAG_LEN              = 16;
constexpr size_t  WG_KEY_LEN              = 32;
constexpr size_t  WG_AEAD_NONCE_LEN       = 12;
constexpr size_t  WG_COOKIE_LEN           = 16;
constexpr size_t  WG_HASH_LEN             = 32;
constexpr size_t  WG_IDENTIFIER_LEN       = 32;

constexpr uint64_t REKEY_AFTER_MESSAGES   = 1ULL << 60;
constexpr int      REKEY_AFTER_TIME_SEC    = 120;
constexpr int      REJECT_AFTER_TIME_SEC   = 180;
constexpr int      KEEPALIVE_TIMEOUT_SEC   = 10;
constexpr int      HANDSHAKE_RETRY_SEC     = 5;

} // namespace wg

struct OpenVPNConfig {
    std::string config_path;
    std::string credentials_path;
    std::string interface_name{"odc-ovpn"};
    int max_retries{5};
    int connect_timeout_sec{30};
    bool route_safety_block_default{true};
};

} // namespace odc::hook
