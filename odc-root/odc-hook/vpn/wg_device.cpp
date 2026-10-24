#include "wg_device.hpp"
#include "wg_aead.hpp"

#include <cstring>
#include <spdlog/spdlog.h>

namespace odc::hook::wg {

namespace {

// Portable IPv4 header struct — avoids platform-specific <netinet/ip.h>
// Layout matches standard IPv4 header (20 bytes, RFC 791)
#pragma pack(push, 1)
struct ipv4_header {
    uint8_t  version_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  saddr[4];
    uint8_t  daddr[4];
};
#pragma pack(pop)

static_assert(sizeof(ipv4_header) == 20, "IPv4 header must be 20 bytes");

} // namespace


WgDevice::WgDevice(asio::io_context& io, TunDevice&& tun)
    : io_(io), tun_(std::move(tun))
{}

WgDevice::~WgDevice() { stop(); }

bool WgDevice::start(const WgDeviceConfig& cfg) {
    cfg_ = cfg;
    running_ = true;

    // Create peers
    for (size_t i = 0; i < cfg_.peers.size(); i++) {
        auto peer = std::make_unique<WgPeer>(cfg_.peers[i], static_cast<int>(i));
        peer->set_send_callback([this](const uint8_t* data, size_t len, const std::string& endpoint) {
            if (!udp_socket_ || !udp_socket_->is_open()) return false;
            asio::error_code ec;
            asio::ip::udp::resolver resolver(io_);
            auto results = resolver.resolve(
                endpoint.substr(0, endpoint.find(':')),
                endpoint.substr(endpoint.find(':') + 1), ec);
            if (ec) {
                SPDLOG_WARN("WgDevice: failed to resolve {}: {}", endpoint, ec.message());
                return false;
            }
            udp_socket_->send_to(asio::buffer(data, len), *results.begin(), 0, ec);
            return !ec;
        });
        peers_.push_back(std::move(peer));
    }

    // Create UDP socket
    udp_socket_ = std::make_unique<asio::ip::udp::socket>(io_);
    asio::error_code ec;
    udp_socket_->open(asio::ip::udp::v4(), ec);
    if (ec) {
        SPDLOG_ERROR("WgDevice: failed to open UDP socket: {}", ec.message());
        return false;
    }
    udp_socket_->bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), cfg_.listen_port), ec);
    if (ec) {
        SPDLOG_ERROR("WgDevice: failed to bind UDP port {}: {}", cfg_.listen_port, ec.message());
        return false;
    }

    // Initiate handshakes for all peers
    for (auto& peer : peers_) {
        peer->send_handshake_initiation(cfg_.private_key);
    }

    // Start I/O threads
    tun_thread_ = std::thread(&WgDevice::tun_loop, this);
    udp_thread_ = std::thread(&WgDevice::udp_loop, this);
    timer_thread_ = std::thread(&WgDevice::timer_loop, this);

    SPDLOG_INFO("WgDevice: started ({} peers)", peers_.size());
    return true;
}

void WgDevice::stop() {
    running_ = false;
    if (tun_thread_.joinable()) tun_thread_.join();
    if (udp_thread_.joinable()) udp_thread_.join();
    if (timer_thread_.joinable()) timer_thread_.join();
    if (udp_socket_ && udp_socket_->is_open()) {
        asio::error_code ec;
        udp_socket_->close(ec);
    }
    tun_.close();
}

WgPeerState WgDevice::peer_state(size_t index) const {
    if (index >= peers_.size()) return WgPeerState::Unknown;
    return peers_[index]->state();
}

uint64_t WgDevice::peer_rx_bytes(size_t index) const {
    if (index >= peers_.size()) return 0;
    return peers_[index]->rx_bytes();
}

uint64_t WgDevice::peer_tx_bytes(size_t index) const {
    if (index >= peers_.size()) return 0;
    return peers_[index]->tx_bytes();
}

// ── TUN read loop: raw IP from TUN → encrypt → UDP ────────────────────────
void WgDevice::tun_loop() {
    while (running_) {
        ssize_t n = ::read(tun_.native_handle(), tun_buf_.data(), tun_buf_.size());
        if (n <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto* peer = find_peer_for_ip(tun_buf_.data(), n);
        if (!peer || !peer->has_valid_session()) continue;

        uint8_t enc_buf[65536];
        size_t enc_len;
        uint32_t receiver_idx = peer->receiver_index();
        if (!peer->encrypt_transport(tun_buf_.data(), n, enc_buf, enc_len, receiver_idx)) {
            continue;
        }

        if (udp_socket_ && udp_socket_->is_open()) {
            asio::error_code ec;
            asio::ip::udp::resolver resolver(io_);
            const auto& ep = peer->endpoint();
            auto pos = ep.find(':');
            if (pos != std::string::npos) {
                auto results = resolver.resolve(ep.substr(0, pos), ep.substr(pos + 1), ec);
                if (!ec) {
                    udp_socket_->send_to(asio::buffer(enc_buf, enc_len), *results.begin(), 0, ec);
                }
            }
        }
    }
}

// ── UDP read loop: encrypted UDP from network → decrypt → TUN ──────────────
void WgDevice::udp_loop() {
    while (running_) {
        if (!udp_socket_ || !udp_socket_->is_open()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        asio::error_code ec;
        asio::ip::udp::endpoint remote;
        size_t n = udp_socket_->receive_from(
            asio::buffer(udp_buf_.data(), udp_buf_.size()), remote, 0, ec);

        if (ec == asio::error::operation_aborted) break;
        if (ec) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (n < 4) continue;

        uint8_t msg_type = udp_buf_[0];

        if (msg_type == static_cast<uint8_t>(WgMessageType::HandshakeResponse)) {
            for (auto& peer : peers_) {
                if (peer->state() == WgPeerState::HandshakeInitiationSent ||
                    peer->state() == WgPeerState::Unknown ||
                    peer->state() == WgPeerState::Dead) {
                    if (peer->handle_handshake_response(udp_buf_.data(), n)) {
                        SPDLOG_INFO("WgDevice: peer {} handshake complete", peer->index());
                    }
                }
            }
        }
        else if (msg_type == static_cast<uint8_t>(WgMessageType::TransportData)) {
            uint32_t receiver_idx;
            std::memcpy(&receiver_idx, udp_buf_.data() + 4, 4);
            auto* peer = find_peer_by_receiver(receiver_idx);
            if (!peer) {
                // Try all connected peers
                for (auto& p : peers_) {
                    if (p->has_valid_session()) {
                        size_t plaintext_len;
                        uint8_t plaintext[65536];
                        if (p->decrypt_transport(udp_buf_.data(), n, plaintext, plaintext_len)) {
                            ::write(tun_.native_handle(), plaintext, plaintext_len);
                            break;
                        }
                    }
                }
            } else if (peer->has_valid_session()) {
                size_t plaintext_len;
                uint8_t plaintext[65536];
                if (peer->decrypt_transport(udp_buf_.data(), n, plaintext, plaintext_len)) {
                    ::write(tun_.native_handle(), plaintext, plaintext_len);
                }
            }
        }
        else if (msg_type == static_cast<uint8_t>(WgMessageType::CookieReply) ||
                 msg_type == static_cast<uint8_t>(WgMessageType::HandshakeInitiation)) {
            // Server-side only; ignore on client
        }
    }
}

// ── Timer loop: tick peers every 100ms ─────────────────────────────────────
void WgDevice::timer_loop() {
    while (running_) {
        auto now = std::chrono::steady_clock::now();

        for (auto& peer : peers_) {
            // Check if peer needs handshake initiation
            if (peer->needs_handshake()) {
                peer->send_handshake_initiation(cfg_.private_key);
            }

            // Periodic tick (rekey, dead detection, keepalive)
            if (peer->tick(now)) {
                // tick returned true → retry handshake or send keepalive
                if (peer->needs_handshake()) {
                    peer->send_handshake_initiation(cfg_.private_key);
                }
                if (peer->needs_keepalive()) {
                    peer->send_keepalive();
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ── Packet routing helpers ─────────────────────────────────────────────────
WgPeer* WgDevice::find_peer_for_ip(const uint8_t* ip_packet, size_t len) {
    if (len < 20) return nullptr;
    if ((ip_packet[0] >> 4) != 4) return nullptr; // IPv4 only

    // Match against peer allowed_ips
    for (auto& peer : peers_) {
        (void)peer;
    }

    // Default: first connected peer
    for (auto& peer : peers_) {
        if (peer->has_valid_session()) {
            return peer.get();
        }
    }
    return nullptr;
}

WgPeer* WgDevice::find_peer_by_receiver(uint32_t receiver_index) {
    for (auto& peer : peers_) {
        if (peer->receiver_index() == receiver_index) {
            return peer.get();
        }
    }
    return nullptr;
}

} // namespace odc::hook::wg
