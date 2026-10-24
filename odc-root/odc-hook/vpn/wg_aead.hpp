#pragma once

#include "vpn_types.hpp"
#include <cstddef>

namespace odc::hook::wg {

class WgAead {
public:
    static bool encrypt(
        const uint8_t* plaintext, size_t plaintext_len,
        const uint8_t* ad, size_t ad_len,
        const WgKey& key, uint64_t counter,
        uint8_t* ciphertext, WgTag& tag_out);

    static bool decrypt(
        const uint8_t* ciphertext, size_t ciphertext_len,
        const uint8_t* ad, size_t ad_len,
        const WgKey& key, uint64_t counter,
        uint8_t* plaintext, const WgTag& tag_in);

private:
    static void counter_to_nonce(uint64_t counter, WgNonce& nonce);
};

} // namespace odc::hook::wg
