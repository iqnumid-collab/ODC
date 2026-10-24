#pragma once

#include "odc/cert_manager.hpp"

#include <string>

namespace odc {

class WsCertGenerator {
public:
    explicit WsCertGenerator(CertManager& cert_mgr);

    ClientCert generate_client_cert(const std::string& common_name);
    bool revoke(const std::string& fingerprint, const std::string& crl_path);

private:
    CertManager& cert_mgr_;
};

} // namespace odc
