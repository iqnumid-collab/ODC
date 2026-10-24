#include "vpn_cert_generator.hpp"
#include "odc/logging.hpp"

namespace odc {

VpnCertGenerator::VpnCertGenerator(CertManager& cert_mgr, SqliteDb& db)
    : cert_mgr_(cert_mgr), db_(db) {
}

VpnClientCredentials VpnCertGenerator::generate_for_client(
    const std::string& client_id,
    const std::string& tunnel_type,
    const std::string& common_name) {

    VpnClientCredentials creds;
    creds.client_id = client_id;
    creds.common_name = common_name;
    creds.tunnel_type = tunnel_type;

    auto logger = get_logger("vpn_cert_gen");

    if (tunnel_type == "openvpn") {
        if (!cert_mgr_.ca_loaded()) {
            SPDLOG_LOGGER_ERROR(logger, "CA not loaded — cannot generate OpenVPN cert");
            return creds;
        }

        auto cert = cert_mgr_.issue_client_cert(common_name, 2048, 365);
        if (cert.cert_pem.empty()) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to generate certificate for '{}'", common_name);
            return creds;
        }

        creds.cert_pem = cert.cert_pem;
        creds.key_pem = cert.key_pem;
        creds.fingerprint = cert.fingerprint;
    }

    if (tunnel_type == "wireguard") {
        auto kp = CertManager::generate_wg_keypair();
        if (kp.private_key_b64.empty() || kp.public_key_b64.empty()) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to generate WireGuard keypair for '{}'",
                                common_name);
            return creds;
        }
        creds.wg_private_key = kp.private_key_b64;
        creds.wg_public_key = kp.public_key_b64;
    }

    auto ins = db_.prepare(R"(
        INSERT OR REPLACE INTO vpn_clients
            (id, tunnel_type, common_name, cert_fingerprint, public_key)
        VALUES (?, ?, ?, ?, ?)
    )");
    if (ins.valid()) {
        ins.bind(1, client_id);
        ins.bind(2, tunnel_type);
        ins.bind(3, common_name);
        ins.bind(4, creds.fingerprint);
        ins.bind(5, creds.wg_public_key);
        ins.execute();
    }

    SPDLOG_LOGGER_INFO(logger, "Generated {} credentials for client '{}' ({})",
                       tunnel_type, client_id, common_name);
    return creds;
}

bool VpnCertGenerator::revoke(const std::string& client_id, const std::string& crl_path) {
    auto logger = get_logger("vpn_cert_gen");

    auto sel = db_.prepare("SELECT cert_fingerprint FROM vpn_clients WHERE id = ?");
    if (!sel.valid()) return false;
    sel.bind(1, client_id);
    if (!sel.step()) {
        SPDLOG_LOGGER_WARN(logger, "Client '{}' not found for revocation", client_id);
        return false;
    }

    std::string fingerprint = sel.column_text(0);

    if (!cert_mgr_.revoke_cert(fingerprint, crl_path)) {
        return false;
    }

    auto del = db_.prepare("DELETE FROM vpn_clients WHERE id = ?");
    del.bind(1, client_id);
    del.execute();

    SPDLOG_LOGGER_INFO(logger, "Revoked credentials for client '{}'", client_id);
    return true;
}

std::optional<VpnClientCredentials> VpnCertGenerator::get_client_credentials(
    const std::string& client_id) {

    auto sel = db_.prepare("SELECT id, tunnel_type, common_name, cert_fingerprint, "
                           "public_key FROM vpn_clients WHERE id = ?");
    if (!sel.valid()) return std::nullopt;
    sel.bind(1, client_id);
    if (!sel.step()) return std::nullopt;

    VpnClientCredentials creds;
    creds.client_id = sel.column_text(0);
    creds.tunnel_type = sel.column_text(1);
    creds.common_name = sel.column_text(2);
    creds.fingerprint = sel.column_text(3);
    creds.wg_public_key = sel.column_text(4);

    return creds;
}

} // namespace odc
