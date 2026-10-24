#pragma once

#include "odc/yaml_config.hpp"
#include "odc/sqlite_db.hpp"
#include "odc/cert_manager.hpp"
#include "odc/ipc_server.hpp"
#include "odc/logging.hpp"

#include "ws_server.hpp"
#include "webhook_client.hpp"
#include "data_store.hpp"
#include "ws_cert_generator.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace odc {

class RegistryManager {
public:
    explicit RegistryManager(const std::string& config_path);
    ~RegistryManager();

    bool initialize();
    bool start();
    void stop();
    void run();

    DataStore& store() { return *data_store_; }
    WsServer* ws_server() { return ws_server_.get(); }
    WebhookClient* webhook_client() { return webhook_client_.get(); }
    const std::string& config_path() const { return config_path_; }

private:
    void load_config();
    void setup_ipc();
    void setup_websocket();
    void setup_webhooks();

    // IPC handlers
    nlohmann::json handle_tunnel_client_connected(const nlohmann::json& params);
    nlohmann::json handle_tunnel_client_disconnected(const nlohmann::json& params);
    nlohmann::json handle_tunnel_server_started(const nlohmann::json& params);
    nlohmann::json handle_tunnel_server_stopped(const nlohmann::json& params);
    nlohmann::json handle_tunnel_cert_issued(const nlohmann::json& params);
    nlohmann::json handle_tunnel_error(const nlohmann::json& params);
    nlohmann::json handle_ping(const nlohmann::json& params);
    nlohmann::json handle_get_status(const nlohmann::json& params);
    nlohmann::json handle_services_list(const nlohmann::json& params);
    nlohmann::json handle_services_register(const nlohmann::json& params);
    nlohmann::json handle_services_unregister(const nlohmann::json& params);

    // WebSocket handlers
    void on_ws_message(const std::string& connection_id, const nlohmann::json& data);

    // Event relay
    void relay_event(const std::string& topic, const std::string& event_type,
                     const nlohmann::json& data);

    std::string config_path_;
    YAML::Node config_;

    SqliteDb db_;
    CertManager cert_mgr_;

    std::unique_ptr<IpcServer> ipc_server_;
    std::unique_ptr<DataStore> data_store_;
    std::unique_ptr<WsServer> ws_server_;
    std::unique_ptr<WebhookClient> webhook_client_;
    std::unique_ptr<WsCertGenerator> ws_cert_gen_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    std::string ipc_socket_path_;
    std::string db_path_;
};

} // namespace odc
