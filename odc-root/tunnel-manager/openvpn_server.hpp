#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace odc {

struct OvpnServerConfig {
    std::string id;
    uint16_t listen_port{1194};
    std::string protocol{"udp"};
    std::string subnet{"10.8.0.0/24"};
    std::string cipher{"AES-256-GCM"};
    std::string ca_cert_path;
    std::string server_cert_path;
    std::string server_key_path;
    std::string dh_params_path;
    std::string tls_auth_key_path;
    std::vector<std::string> push_routes;
    std::vector<std::string> dns_servers;
};

struct OvpnClientInfo {
    std::string common_name;
    std::string assigned_ip;
    std::string remote_addr;
    int64_t connected_at{0};
    uint64_t bytes_rx{0};
    uint64_t bytes_tx{0};
};

class OpenVpnServer {
public:
    explicit OpenVpnServer(const OvpnServerConfig& config);
    ~OpenVpnServer();

    OpenVpnServer(const OpenVpnServer&) = delete;
    OpenVpnServer& operator=(const OpenVpnServer&) = delete;

    bool start();
    void stop();
    bool is_running() const { return running_; }

    std::vector<OvpnClientInfo> list_clients();
    bool disconnect_client(const std::string& common_name);

    using ClientCallback = std::function<void(const OvpnClientInfo& client)>;
    void set_on_client_connected(ClientCallback cb) { on_client_connected_ = std::move(cb); }
    void set_on_client_disconnected(ClientCallback cb) { on_client_disconnected_ = std::move(cb); }

    void set_on_error(std::function<void(const std::string&)> cb) { on_error_ = std::move(cb); }

    void poll_clients();

private:
    struct Impl;

    OvpnServerConfig config_;
    bool running_{false};
    ClientCallback on_client_connected_;
    ClientCallback on_client_disconnected_;
    std::function<void(const std::string&)> on_error_;
    std::unique_ptr<Impl> impl_;
};

} // namespace odc
