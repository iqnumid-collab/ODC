#pragma once

#include "vpn_types.hpp"
#include <chrono>
#include <cstdint>

namespace odc::hook::wg {

struct NoiseHandshakeResult {
    bool success{false};
    WgKey sending_key{};
    WgKey receiving_key{};
    uint64_t sending_key_counter{0};
    uint64_t receiving_key_counter{0};
};

class WgNoiseHandshake {
public:
    WgNoiseHandshake(
        const WgKey& static_private,
        const WgKey& peer_static_public,
        const WgKey& preshared_key = {});

    bool build_initiation(
        uint8_t out_msg[WG_HANDSHAKE_INIT_LEN],
        uint32_t& out_sender_index);

    NoiseHandshakeResult process_response(
        const uint8_t msg[WG_HANDSHAKE_RESP_LEN],
        uint32_t sender_index);

    void process_cookie_reply(const uint8_t msg[WG_COOKIE_REPLY_LEN]);
    bool is_complete() const { return handshake_complete_; }

private:
    WgKey static_private_;
    WgKey peer_static_public_;
    WgKey preshared_key_;
    WgKey ephemeral_private_;
    WgKey ephemeral_public_;
    uint32_t last_sender_index_{0};
    bool handshake_complete_{false};

    WgCookie last_cookie_{};
    bool has_cookie_reply_{false};
    std::chrono::steady_clock::time_point last_cookie_time_;

    // Hash chain state
    uint8_t chain_key_[WG_HASH_LEN]{};
    uint8_t hash_state_[WG_HASH_LEN]{};

    static bool dh(WgKey& out, const WgKey& private_key, const WgKey& public_key);
    static bool generate_keypair(WgKey& private_key, WgKey& public_key);
    static void hmac(uint8_t* out, const uint8_t* key, size_t key_len,
                     const uint8_t* data, size_t data_len);
    static void kdf(uint8_t* key1, size_t key1_len,
                    uint8_t* key2, size_t key2_len,
                    const uint8_t* seed, size_t seed_len,
                    const uint8_t* label, size_t label_len);
    bool mix_key(const uint8_t* input, size_t len);
    bool mix_hash(const uint8_t* input, size_t len);
};

} // namespace odc::hook::wg
