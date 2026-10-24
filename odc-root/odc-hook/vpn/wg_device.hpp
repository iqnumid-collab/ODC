#pragma once

#include "vpn_types.hpp"
#include "wg_peer.hpp"
#include "tun_device.hpp"

#include <asio.hpp>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace odc::hook::wg {

class WgDevice {
public:
    WgDevice(asio::io_context& io, TunDevice&& tun);
    ~WgDevice();

    bool start(const WgDeviceConfig& cfg);
    void stop();
    bool is_running() const { return running_; }

    WgPeerState peer_state(size_t index) const;
    uint64_t peer_rx_bytes(size_t index) const;
    uint64_t peer_tx_bytes(size_t index) const;

private:
    asio::io_context& io_;
    TunDevice tun_;
    WgDeviceConfig cfg_;

    std::unique_ptr<asio::ip::udp::socket> udp_socket_;
    std::vector<std::unique_ptr<WgPeer>> peers_;
    std::unordered_map<uint32_t, WgPeer*> peer_by_index_;

    std::thread tun_thread_;
    std::thread udp_thread_;
    std::thread timer_thread_;
    std::atomic<bool> running_{false};

    std::array<uint8_t, 65536> tun_buf_{};
    std::array<uint8_t, 65536> udp_buf_{};

    void tun_loop();
    void udp_loop();
    void timer_loop();

    WgPeer* find_peer_for_ip(const uint8_t* ip_packet, size_t len);
    WgPeer* find_peer_by_receiver(uint32_t receiver_index);
};

} // namespace odc::hook::wg
