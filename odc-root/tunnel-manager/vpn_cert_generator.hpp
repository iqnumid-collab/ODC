#pragma once

#include "odc/cert_manager.hpp"
#include "odc/sqlite_db.hpp"

#include <optional>
#include <string>

namespace odc {

struct VpnClientCredentials {
    std::string client_id;
    std::string common_name;
    std::string tunnel_type;
    std::string cert_pem;
    std::string key_pem;
    std::string fingerprint;
    std::string wg_private_key;
    std::string wg_public_key;
};

class VpnCertGenerator {
public:
    explicit VpnCertGenerator(CertManager& cert_mgr, SqliteDb& db);

    VpnClientCredentials generate_for_client(
        const std::string& client_id,
        const std::string& tunnel_type,
        const std::string& common_name);

    bool revoke(const std::string& client_id, const std::string& crl_path);

    std::optional<VpnClientCredentials> get_client_credentials(const std::string& client_id);

private:
    CertManager& cert_mgr_;
    SqliteDb& db_;
};

} // namespace odc
