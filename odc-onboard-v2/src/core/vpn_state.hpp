#pragma once

#include <cstdint>

namespace odc {

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

} // namespace odc
