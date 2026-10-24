#pragma once

#include "odc/yaml_config.hpp"
#include "odc/sqlite_db.hpp"
#include "odc/cert_manager.hpp"
#include "odc/ipc_client.hpp"
#include "odc/ipc_server.hpp"
#include "odc/logging.hpp"

#include "wireguard_server.hpp"
#include "openvpn_server.hpp"
#include "session_tracker.hpp"
#include "vpn_cert_generator.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace odc {

class TunnelManager {
public:
    explicit TunnelManager(const std::string& config_path);
    ~TunnelManager();

    bool initialize();
    bool start();
    void stop();
    void run();

    SessionTracker& sessions() { return *session_tracker_; }
    WireGuardServer* wg_server() { return wg_server_.get(); }
    OpenVpnServer* ovpn_server() { return ovpn_server_.get(); }
    SqliteDb& db() { return db_; }

    const std::string& config_path() const { return config_path_; }

private:
    void load_config();
    void setup_ipc();
    void setup_tunnel_ipc();
    void setup_servers();
    void notify_registry(const std::string& method, const nlohmann::json& params);
    void poll_loop();

    // IPC handlers for CLI commands
    nlohmann::json handle_status(const nlohmann::json& params);
    nlohmann::json handle_peers_list(const nlohmann::json& params);
    nlohmann::json handle_peers_add(const nlohmann::json& params);
    nlohmann::json handle_peers_remove(const nlohmann::json& params);
    nlohmann::json handle_peers_disconnect(const nlohmann::json& params);
    nlohmann::json handle_health(const nlohmann::json& params);
    nlohmann::json handle_clients_issue(const nlohmann::json& params);

    bool ensure_ovpn_server_material(OvpnServerConfig& cfg);
    bool ensure_wg_private_key(const std::string& key_path, std::string& out_private_b64,
                               std::string& out_public_b64);
    std::string build_ovpn_profile(const VpnClientCredentials& creds,
                                   const std::string& remote_host,
                                   uint16_t remote_port,
                                   const std::string& protocol) const;
    std::string build_wg_profile(const std::string& private_key_b64,
                                 const std::string& server_public_b64,
                                 const std::string& address_cidr,
                                 const std::string& remote_host,
                                 uint16_t remote_port,
                                 const std::string& allowed_ips) const;

    std::string config_path_;
    YAML::Node config_;

    SqliteDb db_;
    CertManager cert_mgr_;

    std::unique_ptr<IpcClient> ipc_client_;
    std::unique_ptr<IpcServer> ipc_server_;
    std::unique_ptr<SessionTracker> session_tracker_;
    std::unique_ptr<VpnCertGenerator> cert_generator_;
    std::unique_ptr<WireGuardServer> wg_server_;
    std::unique_ptr<OpenVpnServer> ovpn_server_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::thread ipc_thread_;

    std::string ipc_socket_path_;
    std::string tunnel_ipc_socket_path_;
    int reconnect_interval_sec_{5};
    int poll_interval_sec_{5};
    std::string db_path_;

    std::string public_endpoint_;
    std::string wg_server_public_b64_;
    uint16_t wg_listen_port_{51820};
    std::string wg_subnet_{"10.0.0.0/24"};
    uint16_t ovpn_listen_port_{1194};
    std::string ovpn_protocol_{"udp"};
    std::string ovpn_tls_auth_path_;
};

} // namespace odc
