#pragma once

#include <sodium.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace odc {

class CredentialCrypto {
public:
    static bool init() {
        return sodium_init() >= 0;
    }

    static std::vector<uint8_t> encrypt(const std::string& plaintext, const std::string& key_path = "") {
        std::vector<uint8_t> key(crypto_secretbox_KEYBYTES);
        std::vector<uint8_t> nonce(crypto_secretbox_NONCEBYTES);

        if (!key_path.empty() && std::filesystem::exists(key_path)) {
            std::ifstream ifs(key_path, std::ios::binary);
            ifs.read(reinterpret_cast<char*>(key.data()), key.size());
            ifs.read(reinterpret_cast<char*>(nonce.data()), nonce.size());
        } else {
            randombytes_buf(key.data(), key.size());
            randombytes_buf(nonce.data(), nonce.size());
        }

        std::vector<uint8_t> ciphertext(plaintext.size() + crypto_secretbox_MACBYTES);

        if (crypto_secretbox_easy(ciphertext.data(),
                                   reinterpret_cast<const uint8_t*>(plaintext.data()),
                                   plaintext.size(),
                                   nonce.data(),
                                   key.data()) != 0) {
            throw std::runtime_error("Encryption failed");
        }

        std::vector<uint8_t> result;
        result.reserve(nonce.size() + ciphertext.size());
        result.insert(result.end(), nonce.begin(), nonce.end());
        result.insert(result.end(), ciphertext.begin(), ciphertext.end());

        return result;
    }

    static std::string decrypt(const std::vector<uint8_t>& data, const std::string& key_path = "") {
        if (data.size() < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES) {
            throw std::runtime_error("Encrypted data too short");
        }

        std::vector<uint8_t> key(crypto_secretbox_KEYBYTES);
        std::vector<uint8_t> nonce(data.begin(), data.begin() + crypto_secretbox_NONCEBYTES);
        std::vector<uint8_t> ciphertext(data.begin() + crypto_secretbox_NONCEBYTES, data.end());

        if (!key_path.empty() && std::filesystem::exists(key_path)) {
            std::ifstream ifs(key_path, std::ios::binary);
            ifs.read(reinterpret_cast<char*>(key.data()), key.size());
        } else {
            throw std::runtime_error("Key file required for decryption");
        }

        std::vector<uint8_t> plaintext(ciphertext.size() - crypto_secretbox_MACBYTES);

        if (crypto_secretbox_open_easy(plaintext.data(),
                                        ciphertext.data(),
                                        ciphertext.size(),
                                        nonce.data(),
                                        key.data()) != 0) {
            throw std::runtime_error("Decryption failed - wrong key or corrupted data");
        }

        return std::string(plaintext.begin(), plaintext.end());
    }

    static bool save_key(const std::string& key_path, const std::vector<uint8_t>& key,
                         const std::vector<uint8_t>& nonce) {
        std::ofstream ofs(key_path, std::ios::binary);
        if (!ofs) {
            return false;
        }
        ofs.write(reinterpret_cast<const char*>(key.data()), key.size());
        ofs.write(reinterpret_cast<const char*>(nonce.data()), nonce.size());
        return true;
    }

    static bool encrypt_file(const std::filesystem::path& input_path,
                              const std::filesystem::path& output_path,
                              const std::filesystem::path& key_path) {
        std::ifstream ifs(input_path, std::ios::binary);
        if (!ifs) {
            SPDLOG_ERROR("Cannot open input file: {}", input_path.string());
            return false;
        }

        std::string content((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
        ifs.close();

        std::vector<uint8_t> key(crypto_secretbox_KEYBYTES);
        std::vector<uint8_t> nonce(crypto_secretbox_NONCEBYTES);
        randombytes_buf(key.data(), key.size());
        randombytes_buf(nonce.data(), nonce.size());

        std::vector<uint8_t> ciphertext(content.size() + crypto_secretbox_MACBYTES);

        if (crypto_secretbox_easy(ciphertext.data(),
                                   reinterpret_cast<const uint8_t*>(content.data()),
                                   content.size(),
                                   nonce.data(),
                                   key.data()) != 0) {
            SPDLOG_ERROR("Encryption failed for: {}", input_path.string());
            return false;
        }

        std::ofstream ofs(output_path, std::ios::binary);
        if (!ofs) {
            SPDLOG_ERROR("Cannot open output file: {}", output_path.string());
            return false;
        }

        ofs.write(reinterpret_cast<const char*>(nonce.data()), nonce.size());
        ofs.write(reinterpret_cast<const char*>(ciphertext.data()), ciphertext.size());
        ofs.close();

        if (!save_key(key_path.string(), key, nonce)) {
            SPDLOG_ERROR("Cannot save key file: {}", key_path.string());
            return false;
        }

        SPDLOG_INFO("Encrypted {} -> {}", input_path.string(), output_path.string());
        return true;
    }

    static std::string decrypt_file(const std::filesystem::path& encrypted_path,
                                     const std::filesystem::path& key_path) {
        std::ifstream ifs(encrypted_path, std::ios::binary);
        if (!ifs) {
            throw std::runtime_error("Cannot open encrypted file: " + encrypted_path.string());
        }

        std::vector<uint8_t> nonce(crypto_secretbox_NONCEBYTES);
        ifs.read(reinterpret_cast<char*>(nonce.data()), nonce.size());

        std::vector<uint8_t> ciphertext((std::istreambuf_iterator<char>(ifs)),
                                         std::istreambuf_iterator<char>());
        ifs.close();

        std::ifstream kifs(key_path, std::ios::binary);
        if (!kifs) {
            throw std::runtime_error("Cannot open key file: " + key_path.string());
        }

        std::vector<uint8_t> key(crypto_secretbox_KEYBYTES);
        kifs.read(reinterpret_cast<char*>(key.data()), key.size());
        kifs.close();

        std::vector<uint8_t> plaintext(ciphertext.size() - crypto_secretbox_MACBYTES);

        if (crypto_secretbox_open_easy(plaintext.data(),
                                        ciphertext.data(),
                                        ciphertext.size(),
                                        nonce.data(),
                                        key.data()) != 0) {
            throw std::runtime_error("Decryption failed - wrong key or corrupted data");
        }

        return std::string(plaintext.begin(), plaintext.end());
    }
};

}
