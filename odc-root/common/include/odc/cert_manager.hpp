#pragma once

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/sha.h>

#include <mutex>
#include <string>
#include <vector>

namespace odc {

struct CaConfig {
    std::string ca_cert_path;
    std::string ca_key_path;
    std::string country{"XX"};
    std::string organization{"odc-root"};
    std::string common_name{"odc-root CA"};
    int key_bits{2048};
    int validity_days{365};
};

struct ClientCert {
    std::string cert_pem;
    std::string key_pem;
    std::string fingerprint;
    std::string common_name;
    std::string serial;
    int64_t not_before{0};
    int64_t not_after{0};
};

struct WgKeypair {
    std::string private_key_b64;  // WireGuard: raw 32-byte key, standard base64
    std::string public_key_b64;
};

enum class CertPurpose {
    Client,  // EKU clientAuth
    Server,  // EKU serverAuth
};

class CertManager {
public:
    CertManager() = default;
    ~CertManager();

    CertManager(const CertManager&) = delete;
    CertManager& operator=(const CertManager&) = delete;

    bool create_ca(const CaConfig& config);
    bool load_ca(const std::string& cert_path, const std::string& key_path);
    bool ca_loaded() const;

    /** PEM of the loaded CA certificate (empty if CA not loaded). */
    std::string ca_cert_pem() const;

    ClientCert issue_client_cert(const std::string& common_name,
                                  int key_bits = 2048, int validity_days = 365);
    ClientCert issue_server_cert(const std::string& common_name,
                                  int key_bits = 2048, int validity_days = 365);

    /** Write leaf cert+key PEMs to disk (creates parent dirs). */
    static bool write_cert_files(const ClientCert& cert,
                                 const std::string& cert_path,
                                 const std::string& key_path);

    bool revoke_cert(const std::string& fingerprint, const std::string& crl_path);
    bool verify_cert(const std::string& cert_pem);

    /** OpenVPN Static key V1 (tls-auth / tls-crypt material) via OpenSSL RAND. */
    static bool write_tls_auth_key(const std::string& path);

    /** WireGuard Curve25519 keypair as raw 32-byte keys (base64), not PEM. */
    static WgKeypair generate_wg_keypair();
    static std::string wg_public_from_private(const std::string& private_key_b64);

    /** @deprecated Prefer generate_wg_keypair(); retained for local CLI PEM tools. */
    static std::string generate_curve25519_private_key();
    static std::string generate_curve25519_public_key(const std::string& private_key);

private:
    ClientCert issue_leaf_cert(const std::string& common_name, CertPurpose purpose,
                               int key_bits, int validity_days);
    static std::string format_fingerprint(const unsigned char* digest, unsigned int len);
    static std::string base64_encode(const unsigned char* data, size_t len);
    static bool base64_decode(const std::string& in, unsigned char* out, size_t out_len);
    static int64_t x509_time_to_unix(const ASN1_TIME* time);
    bool write_ca_files(const std::string& cert_path, const std::string& key_path,
                        X509* cert, EVP_PKEY* key);

    EVP_PKEY* ca_key_{nullptr};
    X509* ca_cert_{nullptr};
    mutable std::mutex mutex_;
    int64_t next_serial_{1};
};

} // namespace odc
