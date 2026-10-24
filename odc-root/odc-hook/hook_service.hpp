#pragma once

#include "ws_client.hpp"
#include "event_bus.hpp"
#include "thread_pool.hpp"

#include "odc/yaml_config.hpp"
#include "odc/cert_manager.hpp"
#include "odc/ipc_server.hpp"

#ifdef ODC_HOOK_HAS_VPN
#include "vpn/vpn_manager.hpp"
#endif

#include <asio.hpp>

#include <atomic>
#include <memory>
#include <string>

namespace odc::hook {

class HookService {
public:
    explicit HookService(const std::string& config_path);
    ~HookService();

    HookService(const HookService&) = delete;
    HookService& operator=(const HookService&) = delete;

    bool initialize();
    bool start();
    void stop();
    void run();

    EventBus& bus() { return *event_bus_; }
    bool is_connected() const { return connected_.load(); }
    uint64_t events_received() const { return event_bus_->published_count(); }
    uint64_t events_processed() const { return event_bus_->consumed_count(); }

private:
    void load_config();
    void setup_ws_client();
    void setup_event_bus();
    void setup_thread_pool();
    void setup_hook_ipc();
    void on_ws_message(std::string_view payload);
    void process_event(const nlohmann::json& event);

    nlohmann::json handle_hook_status(const nlohmann::json& params);
    nlohmann::json handle_hook_ping(const nlohmann::json& params);

    std::string config_path_;
    YAML::Node config_;

    asio::io_context io_ctx_;
    std::unique_ptr<asio::signal_set> signals_;
    std::unique_ptr<WsClient> ws_client_;
    std::unique_ptr<EventBus> event_bus_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<odc::IpcServer> ipc_server_;

#ifdef ODC_HOOK_HAS_VPN
    std::unique_ptr<VpnClientManager> vpn_manager_;
#endif

    CertManager cert_mgr_;

    std::string hook_ipc_socket_path_;
    std::string ws_host_;
    uint16_t ws_port_{9443};

    size_t event_bus_capacity_{4096};
    size_t pool_threads_{0};
    std::chrono::milliseconds consume_timeout_{1000};

    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
};

} // namespace odc::hook
