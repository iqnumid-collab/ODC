#include "cert_revocation.hpp"
#include "odc/logging.hpp"

#include <openssl/pem.h>

namespace odc {

void CertRevocationList::deny(const std::string& fingerprint) {
    std::unique_lock lock(mutex_);
    denied_.insert(fingerprint);
    auto logger = get_logger("cert_revocation");
    SPDLOG_LOGGER_INFO(logger, "Certificate denied: {}", fingerprint);
}

void CertRevocationList::allow(const std::string& fingerprint) {
    std::unique_lock lock(mutex_);
    denied_.erase(fingerprint);
    auto logger = get_logger("cert_revocation");
    SPDLOG_LOGGER_INFO(logger, "Certificate allowed: {}", fingerprint);
}

bool CertRevocationList::is_denied(const std::string& fingerprint) const {
    std::shared_lock lock(mutex_);
    return denied_.find(fingerprint) != denied_.end();
}

bool CertRevocationList::load_crl(const std::string& crl_path) {
    auto logger = get_logger("cert_revocation");

    BIO* bio = BIO_new_file(crl_path.c_str(), "r");
    if (!bio) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to open CRL file: {}", crl_path);
        return false;
    }

    X509_CRL* crl = PEM_read_bio_X509_CRL(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!crl) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to parse CRL file: {}", crl_path);
        return false;
    }

    STACK_OF(X509_REVOKED)* revoked = X509_CRL_get_REVOKED(crl);
    int count = sk_X509_REVOKED_num(revoked);

    std::unique_lock lock(mutex_);

    for (int i = 0; i < count; ++i) {
        X509_REVOKED* entry = sk_X509_REVOKED_value(revoked, i);
        const ASN1_INTEGER* serial = X509_REVOKED_get0_serialNumber(entry);
        if (serial) {
            BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
            if (bn) {
                char* hex = BN_bn2hex(bn);
                if (hex) {
                    denied_.insert(std::string(hex));
                    OPENSSL_free(hex);
                }
                BN_free(bn);
            }
        }
    }

    X509_CRL_free(crl);

    SPDLOG_LOGGER_INFO(logger, "Loaded CRL from {} — {} certificates revoked",
                       crl_path, count);
    return true;
}

size_t CertRevocationList::denied_count() const {
    std::shared_lock lock(mutex_);
    return denied_.size();
}

} // namespace odc
