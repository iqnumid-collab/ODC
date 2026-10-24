#pragma once

#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <shared_mutex>
#include <string>
#include <unordered_set>

namespace odc {

class CertRevocationList {
public:
    CertRevocationList() = default;
    ~CertRevocationList() = default;

    void deny(const std::string& fingerprint);
    void allow(const std::string& fingerprint);
    bool is_denied(const std::string& fingerprint) const;

    bool load_crl(const std::string& crl_path);

    size_t denied_count() const;

private:
    std::unordered_set<std::string> denied_;
    mutable std::shared_mutex mutex_;
};

} // namespace odc
