#include "tunnel_manager.hpp"

#include <csignal>
#include <chrono>
#include <fstream>
#include <sstream>

namespace odc {

TunnelManager::TunnelManager(const std::string& config_path)
    : config_path_(config_path) {
}

TunnelManager::~TunnelManager() {
    stop();
}

bool TunnelManager::initialize() {
    auto logger = get_logger("tunnel_manager");

    load_config();

    // Database
    if (!db_.open(db_path_)) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to open database: {}", db_path_);
        return false;
    }

    // Cert manager
    std::string ca_cert_path = get_value<std::string>(config_["ca"], "cert_path")
        .value_or("/etc/odc-root/ca/ca.crt");
    std::string ca_key_path = get_value<std::string>(config_["ca"], "key_path")
        .value_or("/etc/odc-root/ca/ca.key");

    if (std::filesystem::exists(ca_cert_path) && std::filesystem::exists(ca_key_path)) {
        if (!cert_mgr_.load_ca(ca_cert_path, ca_key_path)) {
            SPDLOG_LOGGER_WARN(logger, "Failed to load CA — cert generation disabled");
        } else {
            SPDLOG_LOGGER_INFO(logger, "CA loaded from {}", ca_cert_path);
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
        SPDLOG_LOGGER_INFO(logger, "Created new CA: {} / {}", ca_cert_path, ca_key_path);
    }

    // Session tracker
    session_tracker_ = std::make_unique<SessionTracker>(db_);
    if (!session_tracker_->initialize()) {
        return false;
    }

    // Cert generator
    cert_generator_ = std::make_unique<VpnCertGenerator>(cert_mgr_, db_);

    // Setup servers
    setup_servers();

    // Setup IPC
    setup_ipc();
    setup_tunnel_ipc();

    SPDLOG_LOGGER_INFO(logger, "Tunnel manager initialized");
    return true;
}

bool TunnelManager::start() {
    if (running_) return true;

    auto logger = get_logger("tunnel_manager");

    // Start WireGuard server
    if (wg_server_) {
        if (!wg_server_->start()) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to start WireGuard server");
            return false;
        }
        SPDLOG_LOGGER_INFO(logger, "WireGuard server started");
    }

    // Start OpenVPN server
    if (ovpn_server_) {
        if (!ovpn_server_->start()) {
            SPDLOG_LOGGER_WARN(logger, "Failed to start OpenVPN server (non-fatal)");
        } else {
            SPDLOG_LOGGER_INFO(logger, "OpenVPN server started");
        }
    }

    // Connect IPC to registry
    if (ipc_client_) {
        ipc_client_->connect();
    }

    // Start tunnel IPC server for CLI commands (accept loop on dedicated thread)
    if (ipc_server_) {
        if (!ipc_server_->start()) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to start tunnel IPC server");
            return false;
        }
        ipc_thread_ = std::thread([this]() {
            ipc_server_->run();
        });
    }

    // Notify registry about started servers
    if (wg_server_ && wg_server_->is_running()) {
        notify_registry("tunnel.server.started", {
            {"server_id", "wg-main"},
            {"type", "wireguard"},
            {"running", true}
        });
    }

    if (ovpn_server_ && ovpn_server_->is_running()) {
        notify_registry("tunnel.server.started", {
            {"server_id", "ovpn-main"},
            {"type", "openvpn"},
            {"running", true}
        });
    }

    running_ = true;
    SPDLOG_LOGGER_INFO(logger, "Tunnel manager started");
    return true;
}

void TunnelManager::stop() {
    if (!running_) return;

    auto logger = get_logger("tunnel_manager");
    stop_requested_ = true;
    running_ = false;

    // Notify registry before shutdown
    if (wg_server_ && wg_server_->is_running()) {
        notify_registry("tunnel.server.stopped", {
            {"server_id", "wg-main"},
            {"type", "wireguard"}
        });
    }

    if (ovpn_server_ && ovpn_server_->is_running()) {
        notify_registry("tunnel.server.stopped", {
            {"server_id", "ovpn-main"},
            {"type", "openvpn"}
        });
    }

    if (wg_server_) wg_server_->stop();
    if (ovpn_server_) ovpn_server_->stop();
    if (ipc_server_) ipc_server_->stop();
    if (ipc_thread_.joinable()) {
        ipc_thread_.join();
    }
    if (ipc_client_) ipc_client_->disconnect();

    db_.close();

    SPDLOG_LOGGER_INFO(logger, "Tunnel manager stopped");
}

void TunnelManager::run() {
    auto logger = get_logger("tunnel_manager");
    SPDLOG_LOGGER_INFO(logger, "Tunnel manager running");

    while (!stop_requested_) {
        if (!running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        // Poll WireGuard peers for connect/disconnect detection
        if (wg_server_ && wg_server_->is_running()) {
            wg_server_->poll_peers();
        }

        // Poll OpenVPN status for client connect/disconnect detection
        if (ovpn_server_ && ovpn_server_->is_running()) {
            ovpn_server_->poll_clients();
        }

        std::this_thread::sleep_for(std::chrono::seconds(poll_interval_sec_));
    }
}

// ── Private methods ─────────────────────────────────────────────────────────

void TunnelManager::load_config() {
    auto logger = get_logger("tunnel_manager");

    config_ = load_yaml(config_path_);
    if (config_.IsNull()) {
        SPDLOG_LOGGER_WARN(logger, "Config file not found or invalid: {} — using defaults",
                           config_path_);
        config_["version"] = 1;
    }

    auto general = config_["general"];
    std::string log_level_str = get_value<std::string>(general, "log_level").value_or("info");
    auto level = spdlog::level::from_str(log_level_str);
    set_log_level(level);

    ipc_socket_path_ = get_value<std::string>(config_["ipc"], "socket_path")
        .value_or("/run/odc-root/odc-ipc.sock");
    tunnel_ipc_socket_path_ = get_value<std::string>(config_["ipc"], "tunnel_ipc_socket_path")
        .value_or("/run/odc-root/tunnel-ipc.sock");
    reconnect_interval_sec_ = get_value<int>(config_["ipc"], "reconnect_interval_sec")
        .value_or(5);

    db_path_ = get_value<std::string>(config_["database"], "path")
        .value_or("/var/lib/odc-root/odc-root.db");

    poll_interval_sec_ = get_value<int>(config_["general"], "poll_interval_sec")
        .value_or(5);
    public_endpoint_ = get_value<std::string>(config_["general"], "public_endpoint")
        .value_or("192.168.10.16");
}

void TunnelManager::setup_ipc() {
    ipc_client_ = std::make_unique<IpcClient>(ipc_socket_path_);
    ipc_client_->set_reconnect_interval(reconnect_interval_sec_);

    ipc_client_->set_on_disconnect([this]() {
        auto logger = get_logger("tunnel_manager");
        SPDLOG_LOGGER_WARN(logger, "IPC connection to registry-manager lost");
    });
}

void TunnelManager::setup_tunnel_ipc() {
    auto logger = get_logger("tunnel_manager");

    ipc_server_ = std::make_unique<IpcServer>(tunnel_ipc_socket_path_);

    ipc_server_->register_method("tunnel.status",
        [this](const nlohmann::json& p) { return handle_status(p); });
    ipc_server_->register_method("tunnel.peers.list",
        [this](const nlohmann::json& p) { return handle_peers_list(p); });
    ipc_server_->register_method("tunnel.peers.add",
        [this](const nlohmann::json& p) { return handle_peers_add(p); });
    ipc_server_->register_method("tunnel.peers.remove",
        [this](const nlohmann::json& p) { return handle_peers_remove(p); });
    ipc_server_->register_method("tunnel.peers.disconnect",
        [this](const nlohmann::json& p) { return handle_peers_disconnect(p); });
    ipc_server_->register_method("tunnel.health",
        [this](const nlohmann::json& p) { return handle_health(p); });
    ipc_server_->register_method("tunnel.clients.issue",
        [this](const nlohmann::json& p) { return handle_clients_issue(p); });

    SPDLOG_LOGGER_INFO(logger, "Tunnel IPC server registered on {}", tunnel_ipc_socket_path_);
}

void TunnelManager::setup_servers() {
    auto logger = get_logger("tunnel_manager");
    auto servers = config_["servers"];

    if (!servers || !servers.IsSequence()) {
        SPDLOG_LOGGER_WARN(logger, "No servers configured in YAML");
        return;
    }

    for (const auto& server : servers) {
        std::string type = get_value<std::string>(server, "type").value_or("");
        bool enabled = get_value<bool>(server, "enabled").value_or(true);
        std::string id = get_value<std::string>(server, "id").value_or("default");

        if (!enabled) {
            SPDLOG_LOGGER_INFO(logger, "Server '{}' ({}) is disabled", id, type);
            continue;
        }

        auto srv_cfg = server["config"];

        if (type == "wireguard") {
            WgServerConfig wg_cfg;
            wg_cfg.interface_name = get_value<std::string>(srv_cfg, "interface_name")
                .value_or("wg-root");
            wg_cfg.listen_port = static_cast<uint16_t>(
                get_value<int>(srv_cfg, "listen_port").value_or(51820));
            wg_cfg.subnet = get_value<std::string>(srv_cfg, "subnet").value_or("10.0.0.0/24");
            wg_cfg.mtu = get_value<int>(srv_cfg, "mtu").value_or(1420);
            wg_cfg.keepalive_sec = static_cast<uint16_t>(
                get_value<int>(srv_cfg, "keepalive_sec").value_or(25));

            // Load private key from file, or create a persistent WireGuard keypair in-process.
            std::string key_path = get_value<std::string>(srv_cfg, "private_key_path")
                .value_or("/etc/odc-root/wg/server.key");
            if (!key_path.empty() && std::filesystem::exists(key_path)) {
                std::ifstream kf(key_path);
                std::getline(kf, wg_cfg.private_key);
                if (!wg_cfg.private_key.empty() && wg_cfg.private_key.back() == '\r') {
                    wg_cfg.private_key.pop_back();
                }
                // Reject legacy PEM / non-WG keys (must be raw 32-byte base64).
                if (CertManager::wg_public_from_private(wg_cfg.private_key).empty()) {
                    SPDLOG_LOGGER_WARN(logger,
                        "Invalid WireGuard key at {} — regenerating", key_path);
                    wg_cfg.private_key.clear();
                }
            }

            std::string wg_pub;
            if (wg_cfg.private_key.empty()) {
                if (!ensure_wg_private_key(key_path, wg_cfg.private_key, wg_pub)) {
                    SPDLOG_LOGGER_ERROR(logger, "Failed to create WireGuard server key at {}",
                                        key_path);
                    continue;
                }
            } else {
                std::string pub_path = key_path + ".pub";
                if (std::filesystem::exists(pub_path)) {
                    std::ifstream pf(pub_path);
                    std::getline(pf, wg_pub);
                }
                if (wg_pub.empty()) {
                    wg_pub = CertManager::wg_public_from_private(wg_cfg.private_key);
                    if (!wg_pub.empty()) {
                        std::ofstream(pub_path) << wg_pub << '\n';
                    }
                }
            }

            wg_listen_port_ = wg_cfg.listen_port;
            wg_subnet_ = wg_cfg.subnet;
            wg_server_public_b64_ = wg_pub;

            wg_server_ = std::make_unique<WireGuardServer>(wg_cfg);

            wg_server_->set_on_peer_connected([this, id](const WgPeerInfo& peer) {
                auto logger = get_logger("tunnel_manager");
                SPDLOG_LOGGER_INFO(logger, "WireGuard peer connected: {}", peer.public_key.substr(0, 16));

                std::string client_id = "wg-" + peer.public_key.substr(0, 16);

                session_tracker_->register_session({
                    .client_id = client_id,
                    .server_id = id,
                    .tunnel_type = "wireguard",
                    .common_name = client_id,
                    .public_key = peer.public_key,
                    .remote_endpoint = peer.endpoint_addr,
                });

                session_tracker_->mark_connected(client_id, peer.allowed_ip);

                notify_registry("tunnel.client.connected", {
                    {"client_id", client_id},
                    {"server_id", id},
                    {"tunnel_type", "wireguard"},
                    {"assigned_ip", peer.allowed_ip},
                    {"public_key", peer.public_key},
                });
            });

            wg_server_->set_on_peer_disconnected([this, id](const WgPeerInfo& peer) {
                std::string client_id = "wg-" + peer.public_key.substr(0, 16);
                session_tracker_->mark_disconnected(client_id);

                notify_registry("tunnel.client.disconnected", {
                    {"client_id", client_id},
                    {"server_id", id},
                    {"tunnel_type", "wireguard"},
                });
            });

        } else if (type == "openvpn") {
            OvpnServerConfig ovpn_cfg;
            ovpn_cfg.id = id;
            ovpn_cfg.listen_port = static_cast<uint16_t>(
                get_value<int>(srv_cfg, "listen_port").value_or(1194));
            ovpn_cfg.protocol = get_value<std::string>(srv_cfg, "protocol").value_or("udp");
            ovpn_cfg.subnet = get_value<std::string>(srv_cfg, "subnet").value_or("10.8.0.0/24");
            ovpn_cfg.cipher = get_value<std::string>(srv_cfg, "cipher").value_or("AES-256-GCM");
            ovpn_cfg.ca_cert_path = get_value<std::string>(srv_cfg, "ca_cert_path").value_or("");
            ovpn_cfg.server_cert_path = get_value<std::string>(srv_cfg, "server_cert_path").value_or("");
            ovpn_cfg.server_key_path = get_value<std::string>(srv_cfg, "server_key_path").value_or("");
            ovpn_cfg.dh_params_path = get_value<std::string>(srv_cfg, "dh_params_path").value_or("");
            ovpn_cfg.tls_auth_key_path = get_value<std::string>(srv_cfg, "tls_auth_key_path").value_or("");

            if (srv_cfg["push_routes"] && srv_cfg["push_routes"].IsSequence()) {
                for (const auto& route : srv_cfg["push_routes"]) {
                    ovpn_cfg.push_routes.push_back(route.as<std::string>());
                }
            }

            if (srv_cfg["dns_servers"] && srv_cfg["dns_servers"].IsSequence()) {
                for (const auto& dns : srv_cfg["dns_servers"]) {
                    ovpn_cfg.dns_servers.push_back(dns.as<std::string>());
                }
            }

            if (!ensure_ovpn_server_material(ovpn_cfg)) {
                SPDLOG_LOGGER_ERROR(logger, "OpenVPN server material missing and could not be created");
                continue;
            }

            ovpn_listen_port_ = ovpn_cfg.listen_port;
            ovpn_protocol_ = ovpn_cfg.protocol;
            ovpn_tls_auth_path_ = ovpn_cfg.tls_auth_key_path;

            ovpn_server_ = std::make_unique<OpenVpnServer>(ovpn_cfg);

            ovpn_server_->set_on_client_connected([this, id](const OvpnClientInfo& client) {
                std::string client_id = "ovpn-" + client.common_name;
                session_tracker_->register_session({
                    .client_id = client_id,
                    .server_id = id,
                    .tunnel_type = "openvpn",
                    .common_name = client.common_name,
                    .remote_endpoint = client.remote_addr,
                });
                session_tracker_->mark_connected(client_id, client.assigned_ip);

                notify_registry("tunnel.client.connected", {
                    {"client_id", client_id},
                    {"server_id", id},
                    {"tunnel_type", "openvpn"},
                    {"common_name", client.common_name},
                    {"assigned_ip", client.assigned_ip},
                });
            });

            ovpn_server_->set_on_client_disconnected([this, id](const OvpnClientInfo& client) {
                std::string client_id = "ovpn-" + client.common_name;
                session_tracker_->mark_disconnected(client_id);

                notify_registry("tunnel.client.disconnected", {
                    {"client_id", client_id},
                    {"server_id", id},
                    {"tunnel_type", "openvpn"},
                });
            });

            ovpn_server_->set_on_error([this](const std::string& error) {
                notify_registry("tunnel.error", {
                    {"server_id", "ovpn-main"},
                    {"error", error}
                });
            });
        }
    }
}

void TunnelManager::notify_registry(const std::string& method,
                                     const nlohmann::json& params) {
    if (!ipc_client_ || !ipc_client_->is_connected()) {
        return;
    }

    ipc_client_->notify(method, params);

    auto logger = get_logger("tunnel_manager");
    SPDLOG_LOGGER_DEBUG(logger, "Notified registry: {} {}", method, params.dump());
}

// ── IPC handlers ────────────────────────────────────────────────────────────

nlohmann::json TunnelManager::handle_status(const nlohmann::json&) {
    nlohmann::json response;
    response["ok"] = true;
    nlohmann::json server_list = nlohmann::json::array();

    if (wg_server_) {
        nlohmann::json srv;
        srv["id"] = "wg-main";
        srv["type"] = "wireguard";
        srv["running"] = wg_server_->is_running();
        auto peers = wg_server_->list_peers();
        srv["peer_count"] = peers.size();
        server_list.push_back(srv);
    }

    if (ovpn_server_) {
        nlohmann::json srv;
        srv["id"] = "ovpn-main";
        srv["type"] = "openvpn";
        srv["running"] = ovpn_server_->is_running();
        auto clients = ovpn_server_->list_clients();
        srv["peer_count"] = clients.size();
        server_list.push_back(srv);
    }

    response["servers"] = server_list;
    return response;
}

nlohmann::json TunnelManager::handle_peers_list(const nlohmann::json&) {
    nlohmann::json response;
    nlohmann::json peers = nlohmann::json::array();

    if (wg_server_ && wg_server_->is_running()) {
        auto wg_peers = wg_server_->list_peers();
        for (const auto& p : wg_peers) {
            nlohmann::json peer;
            peer["tunnel_type"] = "wireguard";
            peer["public_key"] = p.public_key;
            peer["allowed_ip"] = p.allowed_ip;
            peer["last_handshake_sec"] = p.last_handshake_sec;
            peer["bytes_rx"] = p.bytes_rx;
            peer["bytes_tx"] = p.bytes_tx;
            if (!p.endpoint_addr.empty()) {
                peer["endpoint"] = p.endpoint_addr;
            }
            peers.push_back(peer);
        }
    }

    if (ovpn_server_ && ovpn_server_->is_running()) {
        auto ovpn_clients = ovpn_server_->list_clients();
        for (const auto& c : ovpn_clients) {
            nlohmann::json peer;
            peer["tunnel_type"] = "openvpn";
            peer["common_name"] = c.common_name;
            peer["assigned_ip"] = c.assigned_ip;
            peer["remote_addr"] = c.remote_addr;
            peer["connected_at"] = c.connected_at;
            peer["bytes_rx"] = c.bytes_rx;
            peer["bytes_tx"] = c.bytes_tx;
            peers.push_back(peer);
        }
    }

    response["peers"] = peers;
    response["count"] = peers.size();
    return response;
}

nlohmann::json TunnelManager::handle_peers_add(const nlohmann::json& params) {
    if (!wg_server_) {
        return {{"ok", false}, {"error", "No WireGuard server configured"}};
    }

    std::string public_key = params.value("public_key", "");
    if (public_key.empty()) {
        return {{"ok", false}, {"error", "public_key is required"}};
    }

    std::string ip;
    if (params.contains("allowed_ip")) {
        ip = params["allowed_ip"].get<std::string>();
    } else {
        ip = wg_server_->allocate_ip();
        if (ip.empty()) {
            return {{"ok", false}, {"error", "No available IPs in subnet pool"}};
        }
    }

    if (!wg_server_->add_peer(public_key, ip)) {
        wg_server_->release_ip(ip);
        return {{"ok", false}, {"error", "Failed to add peer"}};
    }

    return {{"ok", true}, {"assigned_ip", ip}};
}

nlohmann::json TunnelManager::handle_peers_remove(const nlohmann::json& params) {
    if (!wg_server_) {
        return {{"ok", false}, {"error", "No WireGuard server configured"}};
    }

    std::string public_key = params.value("public_key", "");
    if (public_key.empty()) {
        return {{"ok", false}, {"error", "public_key is required"}};
    }

    auto info = wg_server_->get_peer_info(public_key);
    if (!wg_server_->remove_peer(public_key)) {
        return {{"ok", false}, {"error", "Failed to remove peer"}};
    }

    if (!info.allowed_ip.empty()) {
        wg_server_->release_ip(info.allowed_ip);
    }

    return {{"ok", true}};
}

nlohmann::json TunnelManager::handle_peers_disconnect(const nlohmann::json& params) {
    if (!ovpn_server_) {
        return {{"ok", false}, {"error", "No OpenVPN server configured"}};
    }
    std::string cn = params.value("common_name", "");
    if (cn.empty()) cn = params.value("cn", "");
    if (cn.empty()) {
        return {{"ok", false}, {"error", "common_name is required"}};
    }
    if (!ovpn_server_->disconnect_client(cn)) {
        return {{"ok", false}, {"error", "Client not found or disconnect failed"}};
    }
    return {{"ok", true}, {"common_name", cn}};
}

nlohmann::json TunnelManager::handle_health(const nlohmann::json&) {
    nlohmann::json response;
    response["ok"] = true;
    nlohmann::json server_list = nlohmann::json::array();

    if (wg_server_) {
        nlohmann::json srv;
        srv["id"] = "wg-main";
        srv["type"] = "wireguard";
        srv["running"] = wg_server_->is_running();
        auto peers = wg_server_->list_peers();
        srv["peers_total"] = peers.size();
        int connected = 0;
        for (const auto& p : peers) {
            if (p.last_handshake_sec > 0) connected++;
        }
        srv["peers_connected"] = connected;
        server_list.push_back(srv);
    }

    if (ovpn_server_) {
        nlohmann::json srv;
        srv["id"] = "ovpn-main";
        srv["type"] = "openvpn";
        srv["running"] = ovpn_server_->is_running();
        auto clients = ovpn_server_->list_clients();
        srv["peers_total"] = clients.size();
        srv["peers_connected"] = clients.size(); // All in status are connected
        server_list.push_back(srv);
    }

    response["servers"] = server_list;
    return response;
}

bool TunnelManager::ensure_wg_private_key(const std::string& key_path,
                                          std::string& out_private_b64,
                                          std::string& out_public_b64) {
    auto kp = CertManager::generate_wg_keypair();
    if (kp.private_key_b64.empty() || kp.public_key_b64.empty()) return false;

    std::filesystem::path parent = std::filesystem::path(key_path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    {
        std::ofstream out(key_path, std::ios::trunc);
        if (!out) return false;
        out << kp.private_key_b64 << '\n';
    }
    std::filesystem::permissions(key_path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);

    std::string pub_path = key_path + ".pub";
    {
        std::ofstream out(pub_path, std::ios::trunc);
        if (!out) return false;
        out << kp.public_key_b64 << '\n';
    }

    out_private_b64 = kp.private_key_b64;
    out_public_b64 = kp.public_key_b64;
    return true;
}

bool TunnelManager::ensure_ovpn_server_material(OvpnServerConfig& cfg) {
    auto logger = get_logger("tunnel_manager");

    if (cfg.ca_cert_path.empty()) {
        cfg.ca_cert_path = get_value<std::string>(config_["ca"], "cert_path")
            .value_or("/etc/odc-root/ca/ca.crt");
    }
    if (cfg.server_cert_path.empty()) {
        cfg.server_cert_path = "/etc/odc-root/ovpn/server.crt";
    }
    if (cfg.server_key_path.empty()) {
        cfg.server_key_path = "/etc/odc-root/ovpn/server.key";
    }

    if (!cert_mgr_.ca_loaded()) {
        SPDLOG_LOGGER_ERROR(logger, "CA not loaded — cannot issue OpenVPN server cert");
        return false;
    }

    const bool need_server =
        !std::filesystem::exists(cfg.server_cert_path) ||
        !std::filesystem::exists(cfg.server_key_path);
    if (need_server) {
        auto leaf = cert_mgr_.issue_server_cert("odc-ovpn-server", 2048, 825);
        if (leaf.cert_pem.empty() ||
            !CertManager::write_cert_files(leaf, cfg.server_cert_path, cfg.server_key_path)) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to issue OpenVPN server certificate");
            return false;
        }
        SPDLOG_LOGGER_INFO(logger, "Issued OpenVPN server cert → {}", cfg.server_cert_path);
    }

    if (!cfg.tls_auth_key_path.empty() &&
        !std::filesystem::exists(cfg.tls_auth_key_path)) {
        if (!CertManager::write_tls_auth_key(cfg.tls_auth_key_path)) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to write tls-auth key {}", cfg.tls_auth_key_path);
            return false;
        }
        SPDLOG_LOGGER_INFO(logger, "Created OpenVPN tls-auth key → {}", cfg.tls_auth_key_path);
    }

    return true;
}

std::string TunnelManager::build_ovpn_profile(const VpnClientCredentials& creds,
                                              const std::string& remote_host,
                                              uint16_t remote_port,
                                              const std::string& protocol) const {
    std::ostringstream oss;
    oss << "client\n"
        << "dev tun\n"
        << "proto " << protocol << "\n"
        << "remote " << remote_host << " " << remote_port << "\n"
        << "resolv-retry infinite\n"
        << "nobind\n"
        << "persist-key\n"
        << "persist-tun\n"
        << "remote-cert-tls server\n"
        << "verb 3\n";

    if (!ovpn_tls_auth_path_.empty() && std::filesystem::exists(ovpn_tls_auth_path_)) {
        std::ifstream ta(ovpn_tls_auth_path_);
        std::string ta_body((std::istreambuf_iterator<char>(ta)),
                             std::istreambuf_iterator<char>());
        oss << "key-direction 1\n"
            << "<tls-auth>\n" << ta_body << "</tls-auth>\n";
    }

    oss << "<ca>\n" << cert_mgr_.ca_cert_pem() << "</ca>\n"
        << "<cert>\n" << creds.cert_pem << "</cert>\n"
        << "<key>\n" << creds.key_pem << "</key>\n";
    return oss.str();
}

std::string TunnelManager::build_wg_profile(const std::string& private_key_b64,
                                            const std::string& server_public_b64,
                                            const std::string& address_cidr,
                                            const std::string& remote_host,
                                            uint16_t remote_port,
                                            const std::string& allowed_ips) const {
    std::ostringstream oss;
    oss << "[Interface]\n"
        << "Address = " << address_cidr << "\n"
        << "PrivateKey = " << private_key_b64 << "\n"
        << "\n"
        << "[Peer]\n"
        << "PublicKey = " << server_public_b64 << "\n"
        << "Endpoint = " << remote_host << ":" << remote_port << "\n"
        << "AllowedIPs = " << allowed_ips << "\n"
        << "PersistentKeepalive = 25\n";
    return oss.str();
}

nlohmann::json TunnelManager::handle_clients_issue(const nlohmann::json& params) {
    if (!cert_generator_) {
        return {{"ok", false}, {"error", "Cert generator not initialized"}};
    }

    const std::string tunnel_type = params.value("tunnel_type", "");
    const std::string common_name = params.value("common_name", "");
    std::string client_id = params.value("client_id", "");
    if (tunnel_type.empty() || common_name.empty()) {
        return {{"ok", false}, {"error", "tunnel_type and common_name are required"}};
    }
    if (tunnel_type != "openvpn" && tunnel_type != "wireguard") {
        return {{"ok", false}, {"error", "tunnel_type must be openvpn or wireguard"}};
    }
    if (client_id.empty()) {
        client_id = tunnel_type + "-" + common_name;
    }

    auto creds = cert_generator_->generate_for_client(client_id, tunnel_type, common_name);
    if (tunnel_type == "openvpn" && creds.cert_pem.empty()) {
        return {{"ok", false}, {"error", "Failed to issue OpenVPN client certificate"}};
    }
    if (tunnel_type == "wireguard" && creds.wg_private_key.empty()) {
        return {{"ok", false}, {"error", "Failed to generate WireGuard keypair"}};
    }

    const std::string remote = params.value("remote", public_endpoint_);
    std::string profile;
    std::string assigned_ip = params.value("assigned_ip", "");

    if (tunnel_type == "openvpn") {
        profile = build_ovpn_profile(creds, remote, ovpn_listen_port_, ovpn_protocol_);
    } else {
        if (wg_server_public_b64_.empty()) {
            return {{"ok", false}, {"error", "WireGuard server public key unavailable"}};
        }
        if (assigned_ip.empty() && wg_server_) {
            assigned_ip = wg_server_->allocate_ip();
        }
        if (assigned_ip.empty()) {
            return {{"ok", false}, {"error", "assigned_ip required or WG IP pool exhausted"}};
        }
        if (wg_server_) {
            // Release pool reservation from allocate_ip then add_peer (which tracks it)
            // add_peer expects the IP; if we allocated, peer add will use it.
            if (!wg_server_->add_peer(creds.wg_public_key, assigned_ip)) {
                wg_server_->release_ip(assigned_ip);
                return {{"ok", false}, {"error", "Failed to add WireGuard peer"}};
            }
        }
        std::string addr = assigned_ip;
        if (addr.find('/') == std::string::npos) addr += "/32";
        profile = build_wg_profile(creds.wg_private_key, wg_server_public_b64_, addr,
                                   remote, wg_listen_port_, wg_subnet_);
    }

    const std::string out_path = params.value("output_path", "");
    if (!out_path.empty()) {
        std::filesystem::path parent = std::filesystem::path(out_path).parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }
        std::ofstream out(out_path, std::ios::trunc);
        if (!out) {
            return {{"ok", false}, {"error", "Failed to write output_path"}};
        }
        out << profile;
        std::filesystem::permissions(out_path,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace);
    }

    notify_registry("tunnel.cert.issued", {
        {"client_id", client_id},
        {"tunnel_type", tunnel_type},
        {"common_name", common_name},
        {"fingerprint", creds.fingerprint},
    });

    nlohmann::json resp{
        {"ok", true},
        {"client_id", client_id},
        {"tunnel_type", tunnel_type},
        {"common_name", common_name},
        {"profile", profile},
    };
    if (!creds.fingerprint.empty()) resp["fingerprint"] = creds.fingerprint;
    if (!creds.wg_public_key.empty()) resp["wg_public_key"] = creds.wg_public_key;
    if (!assigned_ip.empty()) resp["assigned_ip"] = assigned_ip;
    if (!out_path.empty()) resp["output_path"] = out_path;
    return resp;
}

} // namespace odc
