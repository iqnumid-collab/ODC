#include "registry_manager.hpp"

#include <thread>

namespace odc {

RegistryManager::RegistryManager(const std::string& config_path)
    : config_path_(config_path) {
}

RegistryManager::~RegistryManager() {
    stop();
}

bool RegistryManager::initialize() {
    auto logger = get_logger("registry_manager");

    load_config();

    if (!db_.open(db_path_)) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to open database: {}", db_path_);
        return false;
    }

    std::string ca_cert_path = get_value<std::string>(config_["ca"], "cert_path")
        .value_or("/etc/odc-root/ca/ca.crt");
    std::string ca_key_path = get_value<std::string>(config_["ca"], "key_path")
        .value_or("/etc/odc-root/ca/ca.key");

    if (std::filesystem::exists(ca_cert_path) && std::filesystem::exists(ca_key_path)) {
        if (!cert_mgr_.load_ca(ca_cert_path, ca_key_path)) {
            SPDLOG_LOGGER_WARN(logger, "Failed to load CA — cert generation disabled");
        }
    } else {
        CaConfig ca_cfg;
        ca_cfg.ca_cert_path = ca_cert_path;
        ca_cfg.ca_key_path = ca_key_path;
        int key_bits = get_value<int>(config_["ca"], "key_bits").value_or(2048);
        int validity_days = get_value<int>(config_["ca"], "validity_days").value_or(365);
        ca_cfg.key_bits = key_bits;
        ca_cfg.validity_days = validity_days;

        if (!cert_mgr_.create_ca(ca_cfg)) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to create CA");
            return false;
        }
    }

    data_store_ = std::make_unique<DataStore>(db_);
    if (!data_store_->initialize()) return false;

    ws_cert_gen_ = std::make_unique<WsCertGenerator>(cert_mgr_);

    setup_websocket();
    setup_webhooks();
    setup_ipc();

    SPDLOG_LOGGER_INFO(logger, "Registry manager initialized");
    return true;
}

bool RegistryManager::start() {
    if (running_) return true;

    auto logger = get_logger("registry_manager");

    if (!ipc_server_->start()) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to start IPC server");
        return false;
    }

    if (ws_server_ && !ws_server_->start()) {
        SPDLOG_LOGGER_WARN(logger, "Failed to start WebSocket server (non-fatal)");
    }

    running_ = true;
    SPDLOG_LOGGER_INFO(logger, "Registry manager started");
    return true;
}

void RegistryManager::stop() {
    if (!running_) return;
    running_ = false;
    stop_requested_ = true;

    auto logger = get_logger("registry_manager");

    if (ws_server_) ws_server_->stop();
    if (ipc_server_) ipc_server_->stop();

    db_.close();

    SPDLOG_LOGGER_INFO(logger, "Registry manager stopped");
}

void RegistryManager::run() {
    auto logger = get_logger("registry_manager");
    SPDLOG_LOGGER_INFO(logger, "Registry manager running");

    // Run IPC server in a background thread
    std::thread ipc_thread([this]() {
        if (ipc_server_) ipc_server_->run();
    });

    // Run WebSocket server in the foreground (blocking)
    if (ws_server_) {
        ws_server_->run();
    } else {
        while (!stop_requested_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    if (ipc_thread.joinable()) ipc_thread.join();
}

// ── Config loading ──────────────────────────────────────────────────────────

void RegistryManager::load_config() {
    auto logger = get_logger("registry_manager");

    config_ = load_yaml(config_path_);
    if (config_.IsNull()) {
        SPDLOG_LOGGER_WARN(logger, "Config file not found: {} — using defaults",
                           config_path_);
        config_["version"] = 1;
    }

    auto general = config_["general"];
    std::string log_level_str = get_value<std::string>(general, "log_level").value_or("info");
    set_log_level(spdlog::level::from_str(log_level_str));

    ipc_socket_path_ = get_value<std::string>(config_["ipc"], "socket_path")
        .value_or("/run/odc-root/odc-ipc.sock");

    db_path_ = get_value<std::string>(config_["database"], "path")
        .value_or("/var/lib/odc-root/odc-root.db");
}

// ── IPC setup ───────────────────────────────────────────────────────────────

void RegistryManager::setup_ipc() {
    ipc_server_ = std::make_unique<IpcServer>(ipc_socket_path_);

    ipc_server_->register_method("ping", [this](const nlohmann::json& params) {
        return handle_ping(params);
    });

    ipc_server_->register_method("tunnel.client.connected",
        [this](const nlohmann::json& params) {
            return handle_tunnel_client_connected(params);
        });

    ipc_server_->register_method("tunnel.client.disconnected",
        [this](const nlohmann::json& params) {
            return handle_tunnel_client_disconnected(params);
        });

    ipc_server_->register_method("tunnel.server.started",
        [this](const nlohmann::json& params) {
            return handle_tunnel_server_started(params);
        });

    ipc_server_->register_method("tunnel.server.stopped",
        [this](const nlohmann::json& params) {
            return handle_tunnel_server_stopped(params);
        });

    ipc_server_->register_method("tunnel.cert.issued",
        [this](const nlohmann::json& params) {
            return handle_tunnel_cert_issued(params);
        });

    ipc_server_->register_method("tunnel.error",
        [this](const nlohmann::json& params) {
            return handle_tunnel_error(params);
        });

    ipc_server_->register_method("status",
        [this](const nlohmann::json& params) {
            return handle_get_status(params);
        });

    ipc_server_->register_method("services.list",
        [this](const nlohmann::json& params) {
            return handle_services_list(params);
        });

    ipc_server_->register_method("services.register",
        [this](const nlohmann::json& params) {
            return handle_services_register(params);
        });

    ipc_server_->register_method("services.unregister",
        [this](const nlohmann::json& params) {
            return handle_services_unregister(params);
        });
}

// ── WebSocket setup ─────────────────────────────────────────────────────────

void RegistryManager::setup_websocket() {
    auto ws_cfg = config_["websocket"];
    if (!ws_cfg || ws_cfg.IsNull()) {
        auto logger = get_logger("registry_manager");
        SPDLOG_LOGGER_WARN(logger, "No WebSocket configuration found — WS server disabled");
        return;
    }

    WsServerConfig cfg;
    cfg.listen_port = static_cast<uint16_t>(
        get_value<int>(ws_cfg, "listen_port").value_or(9443));
    cfg.listen_address = get_value<std::string>(ws_cfg, "listen_address").value_or("0.0.0.0");
    cfg.tls_cert_path = get_value<std::string>(ws_cfg, "tls_cert_path").value_or("");
    cfg.tls_key_path = get_value<std::string>(ws_cfg, "tls_key_path").value_or("");
    cfg.url_path = get_value<std::string>(ws_cfg, "url_path").value_or("/ws");
    cfg.max_connections = get_value<int>(ws_cfg, "max_connections").value_or(1000);
    cfg.idle_timeout_sec = get_value<int>(ws_cfg, "idle_timeout_sec").value_or(300);

    // mTLS configuration
    auto mtls_cfg = ws_cfg["mtls"];
    if (mtls_cfg && !mtls_cfg.IsNull()) {
        cfg.mtls.enabled = get_value<bool>(mtls_cfg, "enabled").value_or(true);
        cfg.mtls.ca_cert_path = get_value<std::string>(mtls_cfg, "ca_cert_path").value_or("");
        cfg.mtls.verify_depth = get_value<int>(mtls_cfg, "verify_depth").value_or(2);
        cfg.mtls.crl_path = get_value<std::string>(mtls_cfg, "crl_path").value_or("");
        cfg.mtls.cipher_list = get_value<std::string>(mtls_cfg, "cipher_list")
            .value_or("ECDHE+AESGCM:!aNULL:!MD5:!RC4");
        cfg.mtls.require_client_cert = get_value<bool>(mtls_cfg, "require_client_cert")
            .value_or(true);
    } else {
        // Backward compat: fall back to flat fields
        cfg.mtls.enabled = get_value<bool>(ws_cfg, "verify_client_cert").value_or(false);
        cfg.mtls.ca_cert_path = get_value<std::string>(ws_cfg, "ca_cert_path").value_or("");
        cfg.mtls.require_client_cert = cfg.mtls.enabled;
    }

    ws_server_ = std::make_unique<WsServer>(cfg);

    ws_server_->set_on_client_cert([this](const std::string& conn_id,
                                           const ClientCertInfo& cert) {
        auto logger = get_logger("registry_manager");
        SPDLOG_LOGGER_INFO(logger, "mTLS client authenticated: conn={} cn={} fp={}",
                           conn_id, cert.common_name,
                           cert.fingerprint.substr(0, 23) + "...");
        // Register the authenticated client identity
        data_store_->register_client({
            .id = conn_id,
            .name = cert.common_name,
            .cert_fingerprint = cert.fingerprint,
            .ws_connected = true,
        });
    });

    ws_server_->set_on_disconnect([this](const std::string& conn_id) {
        data_store_->mark_client_disconnected(conn_id);
    });

    ws_server_->set_on_message([this](const std::string& conn_id,
                                       const nlohmann::json& data) {
        on_ws_message(conn_id, data);
    });
}

// ── Webhook setup ───────────────────────────────────────────────────────────

void RegistryManager::setup_webhooks() {
    webhook_client_ = std::make_unique<WebhookClient>();

    auto webhooks = config_["webhooks"];
    if (webhooks && webhooks.IsSequence()) {
        for (const auto& wh : webhooks) {
            WebhookConfig cfg;
            cfg.id = get_value<std::string>(wh, "id").value_or("");
            cfg.url = get_value<std::string>(wh, "url").value_or("");
            cfg.secret = get_value<std::string>(wh, "secret").value_or("");
            cfg.enabled = get_value<bool>(wh, "enabled").value_or(true);

            if (wh["events"] && wh["events"].IsSequence()) {
                for (const auto& evt : wh["events"]) {
                    cfg.events.push_back(evt.as<std::string>());
                }
            }

            if (!cfg.id.empty() && !cfg.url.empty()) {
                webhook_client_->add_webhook(cfg);
            }
        }
    }

    auto logger = get_logger("registry_manager");
    SPDLOG_LOGGER_INFO(logger, "Loaded {} webhooks", webhook_client_->list_webhooks().size());
}

// ── IPC handlers ────────────────────────────────────────────────────────────

nlohmann::json RegistryManager::handle_ping(const nlohmann::json&) {
    return {{"pong", true}, {"timestamp", std::time(nullptr)}};
}

nlohmann::json RegistryManager::handle_get_status(const nlohmann::json&) {
    return {
        {"service", "odc-registry-manager"},
        {"version", "0.1.0"},
        {"ws_connections", ws_server_ ? ws_server_->connection_count() : 0},
        {"services", data_store_->service_count()},
    };
}

nlohmann::json RegistryManager::handle_tunnel_client_connected(const nlohmann::json& params) {
    relay_event("tunnel.events", "tunnel.client.connected", params);
    return {{"ok", true}};
}

nlohmann::json RegistryManager::handle_tunnel_client_disconnected(const nlohmann::json& params) {
    relay_event("tunnel.events", "tunnel.client.disconnected", params);
    return {{"ok", true}};
}

nlohmann::json RegistryManager::handle_tunnel_server_started(const nlohmann::json& params) {
    relay_event("tunnel.events", "tunnel.server.started", params);
    return {{"ok", true}};
}

nlohmann::json RegistryManager::handle_tunnel_server_stopped(const nlohmann::json& params) {
    relay_event("tunnel.events", "tunnel.server.stopped", params);
    return {{"ok", true}};
}

nlohmann::json RegistryManager::handle_tunnel_cert_issued(const nlohmann::json& params) {
    relay_event("tunnel.events", "tunnel.cert.issued", params);
    return {{"ok", true}};
}

nlohmann::json RegistryManager::handle_tunnel_error(const nlohmann::json& params) {
    relay_event("tunnel.events", "tunnel.error", params);
    return {{"ok", true}};
}

nlohmann::json RegistryManager::handle_services_list(const nlohmann::json& params) {
    std::string type_filter;
    if (params.is_object() && params.contains("type")) {
        type_filter = params["type"].get<std::string>();
    }
    auto services = data_store_->list_services(type_filter);

    nlohmann::json result = nlohmann::json::array();
    for (const auto& s : services) {
        result.push_back({
            {"id", s.id},
            {"name", s.name},
            {"type", s.type},
            {"metadata", s.metadata},
            {"registered_at", s.registered_at},
            {"updated_at", s.updated_at},
        });
    }
    return {{"services", result}, {"total", services.size()}};
}

nlohmann::json RegistryManager::handle_services_register(const nlohmann::json& params) {
    ServiceEntry s;
    s.id = params.value("id", "");
    s.name = params.value("name", "");
    s.type = params.value("type", "");
    s.metadata = params.value("metadata", nlohmann::json::object());

    if (!data_store_->register_service(s)) {
        return {{"ok", false}, {"error", "Failed to register service"}};
    }

    relay_event("services", "service.registered", {
        {"id", s.id},
        {"name", s.name},
        {"type", s.type},
    });

    return {{"ok", true}, {"service", params}};
}

nlohmann::json RegistryManager::handle_services_unregister(const nlohmann::json& params) {
    std::string service_id = params.value("id", "");
    if (!data_store_->unregister_service(service_id)) {
        return {{"ok", false}, {"error", "Failed to unregister service"}};
    }

    relay_event("services", "service.unregistered", {{"id", service_id}});

    return {{"ok", true}};
}

// ── WebSocket message handlers ──────────────────────────────────────────────

void RegistryManager::on_ws_message(const std::string& connection_id,
                                     const nlohmann::json& data) {
    std::string msg_type = data.value("type", "");

    if (msg_type == "service.register") {
        const auto& svc = data["service"];
        handle_services_register(svc);
        ws_server_->send_json(connection_id, {
            {"type", "service.registered"},
            {"service", svc}
        });
    } else if (msg_type == "service.unregister") {
        handle_services_unregister(data);
        ws_server_->send_json(connection_id, {
            {"type", "service.unregistered"},
            {"service_id", data.value("service_id", data.value("id", ""))}
        });
    } else if (msg_type == "query") {
        std::string type_filter;
        if (data.contains("filter") && data["filter"].is_object()) {
            type_filter = data["filter"].value("type", "");
        }
        int limit = data.value("limit", 100);

        auto services = data_store_->list_services(type_filter);
        if (limit > 0 && static_cast<int>(services.size()) > limit) {
            services.resize(limit);
        }

        nlohmann::json results = nlohmann::json::array();
        for (const auto& s : services) {
            results.push_back({
                {"id", s.id},
                {"name", s.name},
                {"type", s.type},
                {"metadata", s.metadata},
            });
        }

        ws_server_->send_json(connection_id, {
            {"type", "query_result"},
            {"services", results},
            {"total", data_store_->service_count()}
        });
    } else {
        ws_server_->send_json(connection_id, {
            {"type", "error"},
            {"code", "unknown_type"},
            {"message", "Unknown message type: " + msg_type}
        });
    }
}

// ── Event relay ─────────────────────────────────────────────────────────────

void RegistryManager::relay_event(const std::string& topic,
                                   const std::string& event_type,
                                   const nlohmann::json& data) {
    auto logger = get_logger("registry_manager");
    SPDLOG_LOGGER_DEBUG(logger, "Event: {} = {}", event_type, data.dump());

    // Log to DB
    data_store_->log_event({
        .type = event_type,
        .payload = data,
    });

    // Broadcast to WebSocket subscribers
    if (ws_server_) {
        ws_server_->publish(topic, {
            {"event", event_type},
            {"data", data},
            {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        });
    }

    // Fire webhooks
    if (webhook_client_) {
        webhook_client_->deliver(event_type, data);
    }
}

} // namespace odc
