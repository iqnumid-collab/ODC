#include "wg_peer.hpp"
#include "wg_aead.hpp"

#include <cstring>
#include <spdlog/spdlog.h>

namespace odc::hook::wg {

WgPeer::WgPeer(const WgPeerConfig& cfg, int peer_index)
    : cfg_(cfg), index_(peer_index)
{
    last_data_packet_rx_ = std::chrono::steady_clock::now();
}

WgPeer::~WgPeer() = default;

bool WgPeer::tick(std::chrono::steady_clock::time_point now) {
    std::lock_guard lock(mutex_);

    if (session_valid_) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - session_established_time_).count();
        bool rekey_by_time = (age > REKEY_AFTER_TIME_SEC);
        bool rekey_by_msgs = (sending_key_counter_ > REKEY_AFTER_MESSAGES);

        if (rekey_by_time || rekey_by_msgs) {
            SPDLOG_DEBUG("WgPeer[{}]: rekey triggered (age={}s, msgs={})",
                         index_, age, sending_key_counter_);
            state_ = WgPeerState::HandshakeInitiationSent;
            session_valid_ = false;
            last_handshake_time_ = now;
            handshake_retries_ = 0;
            return true;
        }

        auto rx_gap = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_data_packet_rx_).count();
        if (rx_gap > REJECT_AFTER_TIME_SEC) {
            SPDLOG_DEBUG("WgPeer[{}]: no data for {}s, marking dead", index_, rx_gap);
            state_ = WgPeerState::Dead;
            session_valid_ = false;
        }

        if (need_keepalive_) {
            auto tx_gap = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_data_packet_rx_).count();
            if (tx_gap >= KEEPALIVE_TIMEOUT_SEC) {
                return true; // trigger keepalive
            }
        }
    }

    if (state_ == WgPeerState::HandshakeInitiationSent) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_handshake_time_).count();
        if (elapsed > HANDSHAKE_RETRY_SEC) {
            if (handshake_retries_ < 5) {
                last_handshake_time_ = now;
                handshake_retries_++;
                return true;  // retry handshake
            } else {
                SPDLOG_WARN("WgPeer[{}]: handshake failed after {} retries", index_, handshake_retries_);
                state_ = WgPeerState::Dead;
            }
        }
    }

    return false;
}

bool WgPeer::needs_handshake() const {
    return state_ == WgPeerState::Unknown || state_ == WgPeerState::Dead;
}

bool WgPeer::needs_keepalive() const {
    return need_keepalive_ && session_valid_;
}

bool WgPeer::send_handshake_initiation(const WgKey& static_private) {
    handshake_ = std::make_unique<WgNoiseHandshake>(static_private, cfg_.public_key, cfg_.preshared_key);

    uint8_t msg[WG_HANDSHAKE_INIT_LEN];
    uint32_t sender_index;
    if (!handshake_->build_initiation(msg, sender_index)) {
        return false;
    }

    if (send_cb_) {
        if (send_cb_(msg, WG_HANDSHAKE_INIT_LEN, cfg_.endpoint)) {
            state_ = WgPeerState::HandshakeInitiationSent;
            last_handshake_time_ = std::chrono::steady_clock::now();
            handshake_retries_ = 0;
            SPDLOG_DEBUG("WgPeer[{}]: sent handshake initiation to {}", index_, cfg_.endpoint);
            return true;
        }
    }
    return false;
}

bool WgPeer::handle_handshake_response(const uint8_t* msg, size_t len) {
    std::lock_guard lock(mutex_);

    if (!handshake_ || len < WG_HANDSHAKE_RESP_LEN) return false;

    auto result = handshake_->process_response(msg, 0);
    if (!result.success) {
        SPDLOG_WARN("WgPeer[{}]: handshake response processing failed", index_);
        return false;
    }

    sending_key_ = result.sending_key;
    receiving_key_ = result.receiving_key;
    sending_key_counter_ = 0;
    receiving_key_counter_ = 0;
    session_valid_ = true;
    session_established_time_ = std::chrono::steady_clock::now();
    state_ = WgPeerState::Connected;
    SPDLOG_INFO("WgPeer[{}]: session established", index_);
    return true;
}

bool WgPeer::encrypt_transport(const uint8_t* plaintext, size_t plaintext_len,
                               uint8_t* ciphertext, size_t& out_len,
                               uint32_t& receiver_index)
{
    std::lock_guard lock(mutex_);
    if (!session_valid_) return false;

    ciphertext[0] = static_cast<uint8_t>(WgMessageType::TransportData);
    std::memcpy(ciphertext + 4, &receiver_index_, 4);

    uint64_t counter = sending_key_counter_++;
    WgTag tag;
    if (!WgAead::encrypt(plaintext, plaintext_len,
                         ciphertext, 8, // AD = type byte + receiver index
                         sending_key_, counter,
                         ciphertext + 16, tag)) {
        return false;
    }

    std::memcpy(ciphertext + 16, plaintext, plaintext_len);
    std::memcpy(ciphertext + 16 + plaintext_len, tag.data(), WG_TAG_LEN);

    out_len = 16 + plaintext_len + WG_TAG_LEN;
    tx_bytes_ += plaintext_len;
    return true;
}

bool WgPeer::decrypt_transport(const uint8_t* ciphertext, size_t ciphertext_len,
                               uint8_t* plaintext, size_t& out_len)
{
    std::lock_guard lock(mutex_);
    if (!session_valid_) return false;

    size_t data_len = ciphertext_len - 16 - WG_TAG_LEN;
    if (data_len > 65535) return false;

    uint32_t rx_counter;
    std::memcpy(&rx_counter, ciphertext + 12, 4);

    WgTag tag;
    std::memcpy(tag.data(), ciphertext + 16 + data_len, WG_TAG_LEN);

    if (!WgAead::decrypt(ciphertext + 16, data_len,
                         ciphertext, 8, // AD = type byte + counter
                         receiving_key_, rx_counter,
                         plaintext, tag)) {
        return false;
    }

    receiving_key_counter_ = std::max<uint64_t>(receiving_key_counter_, static_cast<uint64_t>(rx_counter) + 1);
    out_len = data_len;
    rx_bytes_ += data_len;
    last_data_packet_rx_ = std::chrono::steady_clock::now();
    return true;
}

bool WgPeer::handle_incoming(const uint8_t* packet, size_t len,
                             std::string& out_endpoint)
{
    if (len < 4) return false;
    uint8_t msg_type = packet[0];

    if (msg_type == static_cast<uint8_t>(WgMessageType::HandshakeResponse)) {
        return handle_handshake_response(packet, len);
    }

    if (msg_type == static_cast<uint8_t>(WgMessageType::TransportData)) {
        std::lock_guard lock(mutex_);
        last_data_packet_rx_ = std::chrono::steady_clock::now();
        return true;
    }

    return false;
}

bool WgPeer::send_keepalive() {
    need_keepalive_ = false;
    uint8_t keepalive_pkt[32]{};
    keepalive_pkt[0] = static_cast<uint8_t>(WgMessageType::TransportData);
    if (send_cb_) {
        return send_cb_(keepalive_pkt, 32, cfg_.endpoint);
    }
    return false;
}

} // namespace odc::hook::wg
