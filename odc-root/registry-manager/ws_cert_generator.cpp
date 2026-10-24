#include "ws_cert_generator.hpp"
#include "odc/logging.hpp"

namespace odc {

WsCertGenerator::WsCertGenerator(CertManager& cert_mgr)
    : cert_mgr_(cert_mgr) {
}

ClientCert WsCertGenerator::generate_client_cert(const std::string& common_name) {
    auto logger = get_logger("ws_cert_gen");

    if (!cert_mgr_.ca_loaded()) {
        SPDLOG_LOGGER_ERROR(logger, "CA not loaded — cannot generate client certificate");
        return {};
    }

    return cert_mgr_.issue_client_cert(common_name, 2048, 365);
}

bool WsCertGenerator::revoke(const std::string& fingerprint, const std::string& crl_path) {
    return cert_mgr_.revoke_cert(fingerprint, crl_path);
}

} // namespace odc
