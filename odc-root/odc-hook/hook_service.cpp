#include "hook_service.hpp"
#include "odc/logging.hpp"
#include "odc/platform.hpp"

#ifdef ODC_HOOK_HAS_VPN
#include "vpn/vpn_manager.hpp"
#endif

namespace odc::hook {

HookService::HookService(const std::string& config_path)
    : config_path_(config_path) {
}

HookService::~HookService() {
    stop();
}

bool HookService::initialize() {
    auto logger = get_logger("hook_service");

    load_config();

    std::string ca_cert_path = odc::get_value<std::string>(config_["mtls"], "ca_cert_path")
        .value_or("");
    if (!ca_cert_path.empty() && std::filesystem::exists(ca_cert_path)) {
        std::string ca_key_path;
        cert_mgr_.load_ca(ca_cert_path, "");
    }

    setup_event_bus();
    setup_thread_pool();
    setup_ws_client();
    setup_hook_ipc();

#ifdef ODC_HOOK_HAS_VPN
    vpn_manager_ = std::make_unique<VpnClientManager>(io_ctx_);
    vpn_manager_->set_event_sender([this](const nlohmann::json& evt) {
        if (ws_client_ && ws_client_->is_connected()) {
            ws_client_->send_json(evt);
        }
    });
#endif

    signals_ = std::make_unique<asio::signal_set>(io_ctx_);

    SPDLOG_LOGGER_INFO(logger, "Hook service initialized");
    return true;
}

bool HookService::start() {
    if (running_) return true;

    auto logger = get_logger("hook_service");

    platform::setup_signal_handler(*signals_, [this]() {
        auto logger = get_logger("hook_service");
        SPDLOG_LOGGER_INFO(logger, "Shutdown signal received");
        stop_requested_ = true;
        io_ctx_.stop();
    });

    ws_client_->connect();
    thread_pool_->start();

    if (ipc_server_) {
        ipc_server_->start();
        SPDLOG_LOGGER_INFO(logger, "Hook IPC server started on {}", hook_ipc_socket_path_);
    }

    running_ = true;
    SPDLOG_LOGGER_INFO(logger, "Hook service started");
    return true;
}

void HookService::stop() {
    if (!running_) return;

    auto logger = get_logger("hook_service");
    running_ = false;
    stop_requested_ = true;

    if (ipc_server_) ipc_server_->stop();
    ws_client_->disconnect();
    thread_pool_->stop();
    event_bus_->shutdown();

#ifdef ODC_HOOK_HAS_VPN
    vpn_manager_.reset();
#endif

    io_ctx_.stop();

    SPDLOG_LOGGER_INFO(logger, "Hook service stopped");
}

void HookService::run() {
    auto logger = get_logger("hook_service");
    SPDLOG_LOGGER_INFO(logger, "Hook service running");

    try {
        io_ctx_.run();
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger, "IO context error: {}", e.what());
    }
}

// ── Private ─────────────────────────────────────────────────────────────────

void HookService::load_config() {
    auto logger = get_logger("hook_service");

    config_ = odc::load_yaml(config_path_);
    if (config_.IsNull()) {
        SPDLOG_LOGGER_WARN(logger, "Config file not found: {} — using defaults",
                           config_path_);
        config_["version"] = 1;
    }

    auto general = config_["general"];
    std::string log_level_str = odc::get_value<std::string>(general, "log_level")
        .value_or("info");
    odc::set_log_level(spdlog::level::from_str(log_level_str));

    hook_ipc_socket_path_ = odc::get_value<std::string>(config_["general"], "hook_ipc_socket")
        .value_or("/run/odc-root/hook.sock");

    event_bus_capacity_ = odc::get_value<int>(config_["event_bus"], "queue_capacity")
        .value_or(4096);

    pool_threads_ = static_cast<size_t>(
        odc::get_value<int>(config_["thread_pool"], "num_threads").value_or(0));

    int timeout_ms = odc::get_value<int>(config_["thread_pool"], "event_timeout_ms")
        .value_or(1000);
    consume_timeout_ = std::chrono::milliseconds(timeout_ms);

    auto server = config_["server"];
    ws_host_ = odc::get_value<std::string>(server, "host").value_or("127.0.0.1");
    ws_port_ = static_cast<uint16_t>(
        odc::get_value<int>(server, "port").value_or(9443));
}

void HookService::setup_ws_client() {
    WsClientConfig cfg;

    auto server = config_["server"];
    cfg.server_host = odc::get_value<std::string>(server, "host").value_or("127.0.0.1");
    cfg.server_port = static_cast<uint16_t>(
        odc::get_value<int>(server, "port").value_or(9443));
    cfg.server_uri = odc::get_value<std::string>(server, "uri").value_or("/ws");
    cfg.reconnect_interval_sec = odc::get_value<int>(server, "reconnect_interval_sec")
        .value_or(5);
    cfg.connect_timeout_sec = odc::get_value<int>(server, "connect_timeout_sec")
        .value_or(10);
    cfg.idle_timeout_sec = odc::get_value<int>(server, "idle_timeout_sec").value_or(300);

    auto mtls = config_["mtls"];
    cfg.client_cert_path = odc::get_value<std::string>(mtls, "client_cert_path").value_or("");
    cfg.client_key_path = odc::get_value<std::string>(mtls, "client_key_path").value_or("");
    cfg.ca_cert_path = odc::get_value<std::string>(mtls, "ca_cert_path").value_or("");
    cfg.verify_server = odc::get_value<bool>(mtls, "verify_server").value_or(true);

    ws_client_ = std::make_unique<WsClient>(io_ctx_, cfg);

    ws_client_->set_on_connected([this]() {
        auto logger = get_logger("hook_service");
        connected_ = true;
        SPDLOG_LOGGER_INFO(logger, "Connected to registry");

        ws_client_->send_json({
            {"type", "subscribe"},
            {"topic", "tunnel.events"}
        });
        ws_client_->send_json({
            {"type", "subscribe"},
            {"topic", "services"}
        });
    });

    ws_client_->set_on_disconnected([this](const std::string& reason) {
        auto logger = get_logger("hook_service");
        connected_ = false;
        SPDLOG_LOGGER_WARN(logger, "Disconnected: {}", reason);
    });

    ws_client_->set_on_message([this](std::string_view payload) {
        on_ws_message(payload);
    });
}

void HookService::setup_event_bus() {
    event_bus_ = std::make_unique<EventBus>(event_bus_capacity_);
}

void HookService::setup_thread_pool() {
    thread_pool_ = std::make_unique<ThreadPool>(
        pool_threads_, *event_bus_,
        [this](const nlohmann::json& event) { process_event(event); },
        consume_timeout_);
}

void HookService::setup_hook_ipc() {
    auto logger = get_logger("hook_service");

    ipc_server_ = std::make_unique<odc::IpcServer>(hook_ipc_socket_path_);

    ipc_server_->register_method("hook.status",
        [this](const nlohmann::json& p) { return handle_hook_status(p); });
    ipc_server_->register_method("hook.ping",
        [this](const nlohmann::json& p) { return handle_hook_ping(p); });

    SPDLOG_LOGGER_INFO(logger, "Hook IPC server registered on {}", hook_ipc_socket_path_);
}

void HookService::on_ws_message(std::string_view payload) {
    if (event_bus_) {
        try {
            auto json = nlohmann::json::parse(payload);
            event_bus_->publish(std::move(json));
        } catch (...) {
            auto logger = get_logger("hook_service");
            SPDLOG_LOGGER_WARN(logger, "Failed to parse WebSocket message as JSON");
        }
    }
}

void HookService::process_event(const nlohmann::json& event) {
    auto logger = get_logger("hook_service");

    std::string type = event.value("type", "unknown");

#ifdef ODC_HOOK_HAS_VPN
    if (type == "vpn.connect") {
        std::string conn_id = event.value("connection_id", "");
        vpn_manager_->connect(conn_id, event);
        return;
    }
    if (type == "vpn.disconnect") {
        vpn_manager_->disconnect(event.value("connection_id", ""));
        return;
    }
    if (type == "vpn.get_status") {
        auto status = vpn_manager_->get_status(event.value("connection_id", ""));
        if (ws_client_ && ws_client_->is_connected()) {
            ws_client_->send_json(status);
        }
        return;
    }
#endif

    std::string topic = event.value("topic", "");

    if (type == "event" && !topic.empty()) {
        const auto& data = event.value("data", nlohmann::json::object());
        std::string event_type = data.value("event", "");

        SPDLOG_LOGGER_INFO(logger, "Event received: topic={} event={}", topic, event_type);
    } else {
        SPDLOG_LOGGER_DEBUG(logger, "Message received: type={}", type);
    }
}

// ── IPC handlers ────────────────────────────────────────────────────────────

nlohmann::json HookService::handle_hook_status(const nlohmann::json&) {
    return {
        {"connected", connected_.load()},
        {"server", ws_host_ + ":" + std::to_string(ws_port_)},
        {"events_received", event_bus_->published_count()},
        {"events_processed", event_bus_->consumed_count()},
        {"thread_pool_active", thread_pool_->active_count()},
    };
}

nlohmann::json HookService::handle_hook_ping(const nlohmann::json&) {
    return {{"pong", true}};
}

} // namespace odc::hook
