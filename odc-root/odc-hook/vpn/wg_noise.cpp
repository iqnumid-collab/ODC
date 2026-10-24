#include "wg_noise.hpp"
#include "wg_aead.hpp"

#include <sodium.h>
#include <cstring>
#include <algorithm>
#include <random>
#include <spdlog/spdlog.h>

namespace odc::hook::wg {

// ── Noise protocol label constants ─────────────────────────────────────────
static constexpr const char* NOISE_LABEL_INITIATION = "Noise_IKpsk2_25519_ChaChaPoly_BLAKE2s";
static constexpr const char* LABEL_MAC1 = "mac1----";
static constexpr const char* LABEL_COOKIE = "cookie--";

WgNoiseHandshake::WgNoiseHandshake(
    const WgKey& static_private,
    const WgKey& peer_static_public,
    const WgKey& preshared_key)
    : static_private_(static_private)
    , peer_static_public_(peer_static_public)
    , preshared_key_(preshared_key)
{}

bool WgNoiseHandshake::dh(WgKey& out, const WgKey& private_key, const WgKey& public_key) {
    return crypto_scalarmult_curve25519(out.data(), private_key.data(), public_key.data()) == 0;
}

bool WgNoiseHandshake::generate_keypair(WgKey& private_key, WgKey& public_key) {
    crypto_box_keypair(public_key.data(), private_key.data());
    return true;
}

void WgNoiseHandshake::hmac(uint8_t* out, const uint8_t* key, size_t key_len,
                            const uint8_t* data, size_t data_len) {
    crypto_generichash_state st;
    crypto_generichash_init(&st, key, key_len, WG_HASH_LEN);
    crypto_generichash_update(&st, data, data_len);
    crypto_generichash_final(&st, out, WG_HASH_LEN);
}

void WgNoiseHandshake::kdf(uint8_t* key1, size_t key1_len,
                           uint8_t* key2, size_t key2_len,
                           const uint8_t* seed, size_t seed_len,
                           const uint8_t* label, size_t label_len) {
    uint8_t tmp[WG_HASH_LEN];
    crypto_generichash_state st;

    crypto_generichash_init(&st, nullptr, 0, WG_HASH_LEN);
    crypto_generichash_update(&st, seed, seed_len);
    crypto_generichash_update(&st, label, label_len);
    crypto_generichash_final(&st, tmp, WG_HASH_LEN);

    if (key1 && key1_len > 0) {
        hmac(key1, tmp, WG_HASH_LEN, reinterpret_cast<const uint8_t*>("\x01"), 1);
    }
    if (key2 && key2_len > 0) {
        uint8_t tmp2[WG_HASH_LEN];
        hmac(tmp2, tmp, WG_HASH_LEN, reinterpret_cast<const uint8_t*>("\x01"), 1);
        hmac(key2, tmp2, WG_HASH_LEN, reinterpret_cast<const uint8_t*>("\x02"), 1);
    }
}

bool WgNoiseHandshake::mix_key(const uint8_t* input, size_t len) {
    WgKey new_key;
    uint8_t tmp[WG_HASH_LEN];

    uint8_t zero[WG_HASH_LEN]{};
    if (std::memcmp(chain_key_, zero, WG_HASH_LEN) == 0 && len == 0) {
        // First mix: chain_key = input
        if (len != WG_HASH_LEN || !input) return false;
        std::memcpy(chain_key_, input, WG_HASH_LEN);
        return true;
    }

    kdf(chain_key_, WG_HASH_LEN, tmp, WG_HASH_LEN, chain_key_, WG_HASH_LEN,
        input, len);
    std::memcpy(new_key.data(), tmp, WG_KEY_LEN);
    return true;
}

bool WgNoiseHandshake::mix_hash(const uint8_t* input, size_t len) {
    crypto_generichash_state st;
    crypto_generichash_init(&st, nullptr, 0, WG_HASH_LEN);
    crypto_generichash_update(&st, hash_state_, WG_HASH_LEN);
    crypto_generichash_update(&st, input, len);
    crypto_generichash_final(&st, hash_state_, WG_HASH_LEN);
    return true;
}

// ── Build handshake initiation (148 bytes) ─────────────────────────────────

// Simple HMAC that chains label + peer_pub for mac1 key (must be before first use)
static void hmackey_alias(uint8_t* out, const uint8_t* label, size_t label_len,
                     const uint8_t* data, size_t data_len) {
    crypto_generichash_state st;
    crypto_generichash_init(&st, nullptr, 0, WG_HASH_LEN);
    crypto_generichash_update(&st, label, label_len);
    if (data && data_len > 0) {
        crypto_generichash_update(&st, data, data_len);
    }
    crypto_generichash_final(&st, out, WG_HASH_LEN);
}

bool WgNoiseHandshake::build_initiation(
    uint8_t out_msg[WG_HANDSHAKE_INIT_LEN],
    uint32_t& out_sender_index)
{
    handshake_complete_ = false;

    generate_keypair(ephemeral_private_, ephemeral_public_);

    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist;
    out_sender_index = dist(gen);
    last_sender_index_ = out_sender_index;

    // C = H(identifier)
    std::memcpy(hash_state_, NOISE_LABEL_INITIATION,
                std::min(sizeof(NOISE_LABEL_INITIATION), WG_HASH_LEN));
    if (std::strlen(NOISE_LABEL_INITIATION) < WG_HASH_LEN) {
        std::memset(hash_state_ + std::strlen(NOISE_LABEL_INITIATION), 0,
                    WG_HASH_LEN - std::strlen(NOISE_LABEL_INITIATION));
    }
    mix_hash(peer_static_public_.data(), WG_KEY_LEN);

    // CK = empty → initialized with first DH
    std::memset(chain_key_, 0, WG_HASH_LEN);

    uint8_t dh_result[WG_KEY_LEN];
    // mix_key(DH(ephemeral_priv, server_static_pub))
    dh(reinterpret_cast<WgKey&>(dh_result), ephemeral_private_, peer_static_public_);
    mix_key(dh_result, WG_KEY_LEN);
    mix_hash(ephemeral_public_.data(), WG_KEY_LEN);
    // mix_key(DH(ephemeral_priv, server_static_pub)) — second time
    dh(reinterpret_cast<WgKey&>(dh_result), ephemeral_private_, peer_static_public_);
    mix_key(dh_result, WG_KEY_LEN);
    // mix_key(DH(static_priv, server_static_pub))
    dh(reinterpret_cast<WgKey&>(dh_result), static_private_, peer_static_public_);
    mix_key(dh_result, WG_KEY_LEN);

    // Encrypt static_public and timestamp with chain_key
    uint8_t tau[WG_HASH_LEN];
    hmac(tau, chain_key_, WG_HASH_LEN, hash_state_, WG_HASH_LEN);
    mix_hash(tau, WG_HASH_LEN);

    WgTag tag;
    uint8_t enc_buf[48 + 16 + 16];
    WgAead::encrypt(static_private_.data(), WG_KEY_LEN,
                    hash_state_, WG_HASH_LEN,
                    reinterpret_cast<const WgKey&>(chain_key_), 0,
                    enc_buf, tag);

    uint64_t timestamp = static_cast<uint64_t>(
        std::chrono::system_clock::now().time_since_epoch().count());
    uint8_t ts_bytes[12]{};
    for (int i = 0; i < 12; i++) {
        ts_bytes[i] = static_cast<uint8_t>(timestamp & 0xFF);
        timestamp >>= 8;
    }

    WgTag tag2;
    uint8_t enc_ts[12 + 16];
    WgAead::encrypt(ts_bytes, 12,
                    hash_state_, WG_HASH_LEN,
                    reinterpret_cast<const WgKey&>(chain_key_), 1,
                    enc_ts, tag2);

    // Compose message
    std::memset(out_msg, 0, WG_HANDSHAKE_INIT_LEN);
    out_msg[0] = static_cast<uint8_t>(WgMessageType::HandshakeInitiation);
    std::memcpy(out_msg + 4, &out_sender_index, 4);
    std::memcpy(out_msg + 8, ephemeral_public_.data(), WG_KEY_LEN);
    std::memcpy(out_msg + 40, enc_buf, 48);     // enc_static + tag
    std::memcpy(out_msg + 88, enc_ts, 28);       // enc_timestamp + tag

    // mac1 = HMAC(LABEL_MAC1 || server_pub, msg[0..-32])
    uint8_t hmac_key[WG_HASH_LEN];
    hmackey_alias(hmac_key, reinterpret_cast<const uint8_t*>(LABEL_MAC1), 8,
                  peer_static_public_.data(), WG_KEY_LEN);
    hmac(out_msg + 116, hmac_key, WG_HASH_LEN, out_msg, 116);

    if (has_cookie_reply_) {
        hmac(out_msg + 132, last_cookie_.data(), WG_COOKIE_LEN, out_msg, 132);
    }

    SPDLOG_DEBUG("WgNoise::built handshake initiation, sender_index={}", out_sender_index);
    return true;
}

// ── Process handshake response (92 bytes) ──────────────────────────────────
NoiseHandshakeResult WgNoiseHandshake::process_response(
    const uint8_t msg[WG_HANDSHAKE_RESP_LEN],
    uint32_t sender_index)
{
    NoiseHandshakeResult result;

    if (msg[0] != static_cast<uint8_t>(WgMessageType::HandshakeResponse)) {
        SPDLOG_WARN("WgNoise: unexpected message type {}", msg[0]);
        return result;
    }

    uint32_t rx_sender;
    std::memcpy(&rx_sender, msg + 4, 4);

    WgKey peer_ephemeral;
    std::memcpy(peer_ephemeral.data(), msg + 8, WG_KEY_LEN);

    mix_hash(peer_ephemeral.data(), WG_KEY_LEN);

    uint8_t dh_result[WG_KEY_LEN];
    // mix_key(DH(ephemeral_priv, peer_ephemeral))
    dh(reinterpret_cast<WgKey&>(dh_result), ephemeral_private_, peer_ephemeral);
    mix_key(dh_result, WG_KEY_LEN);
    // mix_key(DH(static_priv, peer_ephemeral))
    dh(reinterpret_cast<WgKey&>(dh_result), static_private_, peer_ephemeral);
    mix_key(dh_result, WG_KEY_LEN);

    uint8_t tau[WG_HASH_LEN];
    hmac(tau, chain_key_, WG_HASH_LEN, hash_state_, WG_HASH_LEN);
    mix_hash(tau, WG_HASH_LEN);

    // Decrypt empty payload
    WgTag rx_tag;
    std::memcpy(rx_tag.data(), msg + 40 + 0, WG_TAG_LEN);

    uint8_t dummy[WG_TAG_LEN];
    if (!WgAead::decrypt(nullptr, 0,
                         hash_state_, WG_HASH_LEN,
                         reinterpret_cast<const WgKey&>(chain_key_), 0,
                         dummy, rx_tag)) {
        SPDLOG_WARN("WgNoise: response AEAD decrypt failed");
        return result;
    }

    // Derive sending and receiving keys
    uint8_t derived[WG_HASH_LEN * 2];
    uint8_t empty[WG_HASH_LEN]{};
    kdf(chain_key_, WG_HASH_LEN, derived, WG_HASH_LEN * 2,
        empty, 0, empty, 0);

    std::memcpy(result.sending_key.data(), derived, WG_KEY_LEN);
    std::memcpy(result.receiving_key.data(), derived + WG_HASH_LEN, WG_KEY_LEN);
    result.sending_key_counter = 0;
    result.receiving_key_counter = 0;
    result.success = true;
    handshake_complete_ = true;

    SPDLOG_DEBUG("WgNoise: handshake complete, sender_index={}", rx_sender);
    return result;
}

void WgNoiseHandshake::process_cookie_reply(const uint8_t msg[WG_COOKIE_REPLY_LEN]) {
    if (msg[0] != static_cast<uint8_t>(WgMessageType::CookieReply)) return;
    std::memcpy(last_cookie_.data(), msg + 8, WG_COOKIE_LEN);
    has_cookie_reply_ = true;
    last_cookie_time_ = std::chrono::steady_clock::now();
}

} // namespace odc::hook::wg
