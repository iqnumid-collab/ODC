#pragma once

#include "vpn_types.hpp"
#include "wg_noise.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

namespace odc::hook::wg {

class WgPeer {
public:
    using SendCallback = std::function<bool(const uint8_t* data, size_t len, const std::string& endpoint)>;

    WgPeer(const WgPeerConfig& cfg, int peer_index);
    ~WgPeer();

    bool tick(std::chrono::steady_clock::time_point now);

    // Returns true if a handshake should be initiated now
    bool needs_handshake() const;
    bool needs_keepalive() const;

    // Crypto operations
    bool encrypt_transport(const uint8_t* plaintext, size_t plaintext_len,
                           uint8_t* ciphertext, size_t& out_len,
                           uint32_t& receiver_index);
    bool decrypt_transport(const uint8_t* ciphertext, size_t ciphertext_len,
                           uint8_t* plaintext, size_t& out_len);

    // Incoming message handlers
    bool handle_incoming(const uint8_t* packet, size_t len,
                         std::string& out_endpoint);
    bool handle_handshake_response(const uint8_t* msg, size_t len);

    // Build/send initiation
    bool send_handshake_initiation(const WgKey& static_private);
    bool send_keepalive();

    // Accessors
    WgPeerState state() const { return state_; }
    int index() const { return index_; }
    uint32_t receiver_index() const { return receiver_index_; }
    const WgKey& pubkey() const { return cfg_.public_key; }
    const std::string& endpoint() const { return cfg_.endpoint; }
    bool has_valid_session() const { return session_valid_; }
    void set_send_callback(SendCallback cb) { send_cb_ = std::move(cb); }

    uint64_t rx_bytes() const { return rx_bytes_; }
    uint64_t tx_bytes() const { return tx_bytes_; }

private:
    WgPeerConfig cfg_;
    int index_{0};
    WgPeerState state_{WgPeerState::Unknown};
    uint32_t receiver_index_{0};

    std::unique_ptr<WgNoiseHandshake> handshake_;
    std::chrono::steady_clock::time_point last_handshake_time_;
    int handshake_retries_{0};

    WgKey sending_key_{};
    uint64_t sending_key_counter_{0};
    WgKey receiving_key_{};
    uint64_t receiving_key_counter_{0};
    bool session_valid_{false};
    std::chrono::steady_clock::time_point session_established_time_;

    bool need_keepalive_{false};
    std::chrono::steady_clock::time_point last_data_packet_rx_;

    SendCallback send_cb_;
    uint64_t rx_bytes_{0};
    uint64_t tx_bytes_{0};
    mutable std::mutex mutex_;
};

} // namespace odc::hook::wg
