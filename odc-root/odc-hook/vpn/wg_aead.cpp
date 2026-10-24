#include "wg_aead.hpp"

#include <sodium.h>
#include <cstring>
#include <spdlog/spdlog.h>

namespace odc::hook::wg {

void WgAead::counter_to_nonce(uint64_t counter, WgNonce& nonce) {
    nonce.fill(0);
    for (int i = 0; i < 8; i++) {
        nonce[4 + i] = static_cast<uint8_t>(counter & 0xFF);
        counter >>= 8;
    }
}

bool WgAead::encrypt(
    const uint8_t* plaintext, size_t plaintext_len,
    const uint8_t* ad, size_t ad_len,
    const WgKey& key, uint64_t counter,
    uint8_t* ciphertext, WgTag& tag_out)
{
    WgNonce nonce{};
    counter_to_nonce(counter, nonce);

    unsigned long long mac_len = WG_TAG_LEN;
    int ret = crypto_aead_chacha20poly1305_ietf_encrypt(
        ciphertext, &mac_len,
        plaintext, plaintext_len,
        ad, ad_len,
        nullptr, nonce.data(), key.data());

    if (ret != 0) {
        SPDLOG_ERROR("WgAead::encrypt failed: sodium error {}", ret);
        return false;
    }
    std::memcpy(tag_out.data(), ciphertext + plaintext_len, WG_TAG_LEN);
    return true;
}

bool WgAead::decrypt(
    const uint8_t* ciphertext, size_t ciphertext_len,
    const uint8_t* ad, size_t ad_len,
    const WgKey& key, uint64_t counter,
    uint8_t* plaintext, const WgTag& tag_in)
{
    WgNonce nonce{};
    counter_to_nonce(counter, nonce);

    std::array<uint8_t, 65535 + 32> buf{};
    size_t total_len = ciphertext_len + WG_TAG_LEN;
    if (total_len > buf.size()) {
        SPDLOG_ERROR("WgAead::decrypt: packet too large");
        return false;
    }
    std::memcpy(buf.data(), ciphertext, ciphertext_len);
    std::memcpy(buf.data() + ciphertext_len, tag_in.data(), WG_TAG_LEN);

    unsigned long long msg_len = 0;
    int ret = crypto_aead_chacha20poly1305_ietf_decrypt(
        plaintext, &msg_len,
        nullptr,
        buf.data(), total_len,
        ad, ad_len,
        nonce.data(), key.data());

    if (ret != 0) {
        SPDLOG_DEBUG("WgAead::decrypt: AEAD authentication failed");
        return false;
    }
    return true;
}

} // namespace odc::hook::wg
