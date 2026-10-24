#include "odc/cert_manager.hpp"

#include "odc/logging.hpp"

#include <openssl/rand.h>

#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace odc {

namespace {

bool ensure_parent_dirs(const std::string& path) {
    std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        return !ec;
    }
    return true;
}

} // namespace

CertManager::~CertManager() {
    std::lock_guard lock(mutex_);
    if (ca_key_) EVP_PKEY_free(ca_key_);
    if (ca_cert_) X509_free(ca_cert_);
}

bool CertManager::ca_loaded() const {
    return ca_key_ != nullptr && ca_cert_ != nullptr;
}

std::string CertManager::ca_cert_pem() const {
    std::lock_guard lock(mutex_);
    if (!ca_cert_) return {};
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return {};
    if (!PEM_write_bio_X509(bio, ca_cert_)) {
        BIO_free(bio);
        return {};
    }
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string pem(data, static_cast<size_t>(len));
    BIO_free(bio);
    return pem;
}

bool CertManager::load_ca(const std::string& cert_path, const std::string& key_path) {
    std::lock_guard lock(mutex_);

    std::ifstream cert_file(cert_path);
    if (!cert_file) return false;
    std::string cert_pem((std::istreambuf_iterator<char>(cert_file)),
                          std::istreambuf_iterator<char>());

    BIO* bio = BIO_new_mem_buf(cert_pem.c_str(), static_cast<int>(cert_pem.size()));
    if (!bio) return false;

    if (ca_cert_) X509_free(ca_cert_);
    ca_cert_ = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!ca_cert_) return false;

    if (!key_path.empty()) {
        std::ifstream key_file(key_path);
        if (!key_file) return false;
        std::string key_pem((std::istreambuf_iterator<char>(key_file)),
                             std::istreambuf_iterator<char>());

        bio = BIO_new_mem_buf(key_pem.c_str(), static_cast<int>(key_pem.size()));
        if (!bio) return false;

        if (ca_key_) EVP_PKEY_free(ca_key_);
        ca_key_ = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);

        return ca_key_ != nullptr;
    }

    return true;
}

bool CertManager::create_ca(const CaConfig& config) {
    std::lock_guard lock(mutex_);

    EVP_PKEY* key = EVP_RSA_gen(config.key_bits);
    if (!key) {
        auto logger = get_logger("cert_manager");
        SPDLOG_LOGGER_ERROR(logger, "Failed to generate CA key");
        return false;
    }

    X509* cert = X509_new();
    if (!cert) {
        if (key) EVP_PKEY_free(key);
        return false;
    }
    X509_set_version(cert, 2);

    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), config.validity_days * 86400L);

    X509_set_pubkey(cert, key);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C",  MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(config.country.c_str()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O",  MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(config.organization.c_str()), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(config.common_name.c_str()), -1, -1, 0);

    X509_set_issuer_name(cert, name);

    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);

    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints,
        const_cast<char*>("critical,CA:TRUE"));
    if (ext) {
        X509_add_ext(cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage,
        const_cast<char*>("critical,keyCertSign,cRLSign"));
    if (ext) {
        X509_add_ext(cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    if (!X509_sign(cert, key, EVP_sha256())) {
        X509_free(cert);
        EVP_PKEY_free(key);
        return false;
    }

    if (!write_ca_files(config.ca_cert_path, config.ca_key_path, cert, key)) {
        X509_free(cert);
        EVP_PKEY_free(key);
        return false;
    }

    if (ca_key_) EVP_PKEY_free(ca_key_);
    if (ca_cert_) X509_free(ca_cert_);
    ca_key_ = key;
    ca_cert_ = cert;
    next_serial_ = 2;

    return true;
}

ClientCert CertManager::issue_client_cert(const std::string& common_name,
                                           int key_bits, int validity_days) {
    return issue_leaf_cert(common_name, CertPurpose::Client, key_bits, validity_days);
}

ClientCert CertManager::issue_server_cert(const std::string& common_name,
                                           int key_bits, int validity_days) {
    return issue_leaf_cert(common_name, CertPurpose::Server, key_bits, validity_days);
}

ClientCert CertManager::issue_leaf_cert(const std::string& common_name, CertPurpose purpose,
                                         int key_bits, int validity_days) {
    std::lock_guard lock(mutex_);

    ClientCert result;
    if (!ca_key_ || !ca_cert_) {
        return result;
    }

    EVP_PKEY* leaf_key = EVP_RSA_gen(key_bits);
    if (!leaf_key) return result;

    X509* leaf_cert = X509_new();
    if (!leaf_cert) {
        EVP_PKEY_free(leaf_key);
        return result;
    }
    X509_set_version(leaf_cert, 2);

    ASN1_INTEGER_set(X509_get_serialNumber(leaf_cert), next_serial_++);
    X509_gmtime_adj(X509_get_notBefore(leaf_cert), 0);
    X509_gmtime_adj(X509_get_notAfter(leaf_cert), validity_days * 86400L);

    X509_set_pubkey(leaf_cert, leaf_key);

    X509_NAME* name = X509_get_subject_name(leaf_cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(common_name.c_str()), -1, -1, 0);

    X509_set_issuer_name(leaf_cert, X509_get_subject_name(ca_cert_));

    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, ca_cert_, leaf_cert, nullptr, nullptr, 0);

    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints,
        const_cast<char*>("critical,CA:FALSE"));
    if (ext) {
        X509_add_ext(leaf_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage,
        const_cast<char*>("critical,digitalSignature,keyEncipherment"));
    if (ext) {
        X509_add_ext(leaf_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    const char* eku = (purpose == CertPurpose::Server) ? "serverAuth" : "clientAuth";
    ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_ext_key_usage, const_cast<char*>(eku));
    if (ext) {
        X509_add_ext(leaf_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    if (!X509_sign(leaf_cert, ca_key_, EVP_sha256())) {
        X509_free(leaf_cert);
        EVP_PKEY_free(leaf_key);
        return result;
    }

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, leaf_cert);
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    result.cert_pem = std::string(data, len);
    BIO_free(bio);

    bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio, leaf_key, nullptr, nullptr, 0, nullptr, nullptr);
    len = BIO_get_mem_data(bio, &data);
    result.key_pem = std::string(data, len);
    BIO_free(bio);

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    X509_digest(leaf_cert, EVP_sha256(), digest, &digest_len);
    result.fingerprint = format_fingerprint(digest, digest_len);

    result.common_name = common_name;
    result.serial = std::to_string(next_serial_ - 1);
    result.not_before = x509_time_to_unix(X509_get_notBefore(leaf_cert));
    result.not_after = x509_time_to_unix(X509_get_notAfter(leaf_cert));

    X509_free(leaf_cert);
    EVP_PKEY_free(leaf_key);

    return result;
}

bool CertManager::write_cert_files(const ClientCert& cert,
                                    const std::string& cert_path,
                                    const std::string& key_path) {
    if (cert.cert_pem.empty() || cert.key_pem.empty()) return false;
    if (!ensure_parent_dirs(cert_path) || !ensure_parent_dirs(key_path)) return false;

    {
        std::ofstream out(cert_path, std::ios::trunc);
        if (!out) return false;
        out << cert.cert_pem;
    }
    {
        std::ofstream out(key_path, std::ios::trunc);
        if (!out) return false;
        out << cert.key_pem;
    }
    std::filesystem::permissions(key_path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);
    return true;
}

bool CertManager::revoke_cert(const std::string& fingerprint, const std::string& crl_path) {
    std::lock_guard lock(mutex_);
    (void)fingerprint;
    (void)crl_path;
    return true;
}

bool CertManager::verify_cert(const std::string& cert_pem) {
    std::lock_guard lock(mutex_);
    if (!ca_cert_) return false;

    BIO* bio = BIO_new_mem_buf(cert_pem.c_str(), static_cast<int>(cert_pem.size()));
    if (!bio) return false;

    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!cert) return false;

    X509_STORE* store = X509_STORE_new();
    X509_STORE_add_cert(store, ca_cert_);

    X509_STORE_CTX* ctx = X509_STORE_CTX_new();
    X509_STORE_CTX_init(ctx, store, cert, nullptr);

    int rc = X509_verify_cert(ctx);

    X509_STORE_CTX_free(ctx);
    X509_STORE_free(store);
    X509_free(cert);

    return rc == 1;
}

bool CertManager::write_tls_auth_key(const std::string& path) {
    if (!ensure_parent_dirs(path)) return false;

    unsigned char key[256];
    if (RAND_bytes(key, sizeof(key)) != 1) return false;

    std::ostringstream oss;
    oss << "#\n# 2048 bit OpenVPN static key\n#\n";
    oss << "-----BEGIN OpenVPN Static key V1-----\n";
    for (size_t i = 0; i < sizeof(key); ++i) {
        if (i > 0 && (i % 16) == 0) oss << '\n';
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(key[i]);
        if ((i % 16) != 15) oss << ' ';
    }
    oss << "\n-----END OpenVPN Static key V1-----\n";

    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << oss.str();
    std::filesystem::permissions(path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);
    return true;
}

WgKeypair CertManager::generate_wg_keypair() {
    WgKeypair kp;
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (!pctx) return kp;

    if (EVP_PKEY_keygen_init(pctx) <= 0 || EVP_PKEY_keygen(pctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        return kp;
    }
    EVP_PKEY_CTX_free(pctx);
    if (!pkey) return kp;

    unsigned char priv[32];
    unsigned char pub[32];
    size_t priv_len = sizeof(priv);
    size_t pub_len = sizeof(pub);
    if (EVP_PKEY_get_raw_private_key(pkey, priv, &priv_len) <= 0 || priv_len != 32 ||
        EVP_PKEY_get_raw_public_key(pkey, pub, &pub_len) <= 0 || pub_len != 32) {
        EVP_PKEY_free(pkey);
        return kp;
    }
    EVP_PKEY_free(pkey);

    kp.private_key_b64 = base64_encode(priv, 32);
    kp.public_key_b64 = base64_encode(pub, 32);
    return kp;
}

std::string CertManager::wg_public_from_private(const std::string& private_key_b64) {
    unsigned char priv[32];
    if (!base64_decode(private_key_b64, priv, 32)) return {};

    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, priv, 32);
    if (!pkey) return {};

    unsigned char pub[32];
    size_t pub_len = sizeof(pub);
    if (EVP_PKEY_get_raw_public_key(pkey, pub, &pub_len) <= 0 || pub_len != 32) {
        EVP_PKEY_free(pkey);
        return {};
    }
    EVP_PKEY_free(pkey);
    return base64_encode(pub, 32);
}

std::string CertManager::generate_curve25519_private_key() {
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (!pctx) return {};

    EVP_PKEY_keygen_init(pctx);
    EVP_PKEY_keygen(pctx, &pkey);
    EVP_PKEY_CTX_free(pctx);

    if (!pkey) return {};

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);

    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);

    BIO_free(bio);
    EVP_PKEY_free(pkey);

    return result;
}

std::string CertManager::generate_curve25519_public_key(const std::string& private_key_pem) {
    BIO* bio = BIO_new_mem_buf(private_key_pem.c_str(),
                               static_cast<int>(private_key_pem.size()));
    if (!bio) return {};

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return {};

    bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(bio, pkey);

    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);

    BIO_free(bio);
    EVP_PKEY_free(pkey);

    return result;
}

std::string CertManager::base64_encode(const unsigned char* data, size_t len) {
    static const char* kTable =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < len) {
        unsigned int n = (static_cast<unsigned int>(data[i]) << 16) |
                         (static_cast<unsigned int>(data[i + 1]) << 8) |
                         static_cast<unsigned int>(data[i + 2]);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back(kTable[(n >> 6) & 63]);
        out.push_back(kTable[n & 63]);
        i += 3;
    }
    if (i < len) {
        unsigned int n = static_cast<unsigned int>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<unsigned int>(data[i + 1]) << 8;
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back(i + 1 < len ? kTable[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

bool CertManager::base64_decode(const std::string& in, unsigned char* out, size_t out_len) {
    static int decode[256];
    static bool table_ready = false;
    if (!table_ready) {
        for (int i = 0; i < 256; ++i) decode[i] = -1;
        const char* k =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) decode[static_cast<unsigned char>(k[i])] = i;
        table_ready = true;
    }

    size_t o = 0;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') {
            if (c == '=') break;
            continue;
        }
        if (decode[c] < 0) return false;
        val = (val << 6) + decode[c];
        valb += 6;
        if (valb >= 0) {
            if (o >= out_len) return false;
            out[o++] = static_cast<unsigned char>((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return o == out_len;
}

std::string CertManager::format_fingerprint(const unsigned char* digest,
                                             unsigned int len) {
    std::ostringstream oss;
    for (unsigned int i = 0; i < len; ++i) {
        if (i > 0) oss << ':';
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(digest[i]);
    }
    return oss.str();
}

int64_t CertManager::x509_time_to_unix(const ASN1_TIME* time) {
    if (!time) return 0;

    int day, sec;
    if (ASN1_TIME_diff(&day, &sec, nullptr, time)) {
        std::time_t now = std::time(nullptr);
        return static_cast<int64_t>(now) + sec + day * 86400;
    }
    return 0;
}

bool CertManager::write_ca_files(const std::string& cert_path,
                                  const std::string& key_path,
                                  X509* cert, EVP_PKEY* key) {
    if (!ensure_parent_dirs(cert_path) || !ensure_parent_dirs(key_path)) return false;

    BIO* bio = BIO_new_file(cert_path.c_str(), "w");
    if (!bio) return false;
    int rc = PEM_write_bio_X509(bio, cert);
    BIO_free(bio);
    if (!rc) return false;

    bio = BIO_new_file(key_path.c_str(), "w");
    if (!bio) return false;
    rc = PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr);
    BIO_free(bio);

    return rc == 1;
}

} // namespace odc
