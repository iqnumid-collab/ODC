#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "odc/ipc_client.hpp"
#include "odc/cert_manager.hpp"
#include "odc/logging.hpp"

#include "ws_client.hpp"

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

using namespace std::chrono_literals;

namespace {

void print_json(const nlohmann::json& j, int indent = 2) {
    std::cout << j.dump(indent) << std::endl;
}

// ── Cert helpers ─────────────────────────────────────────────────────────

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}

nlohmann::json cert_generate_ca(const std::string& cert_path, const std::string& key_path,
                                 const std::string& org, const std::string& cn,
                                 const std::string& country, int bits, int days) {
    odc::CaConfig cfg{cert_path, key_path, country, org, cn, bits, days};
    odc::CertManager mgr;
    if (!mgr.create_ca(cfg)) {
        return {{"error", "Failed to create CA certificate"}};
    }
    return {{"status", "ok"}, {"cert_path", cert_path}, {"key_path", key_path}};
}

nlohmann::json cert_issue(const std::string& ca_cert, const std::string& ca_key,
                           const std::string& cn, const std::string& cert_out,
                           const std::string& key_out, int bits, int days) {
    odc::CertManager mgr;
    if (!mgr.load_ca(ca_cert, ca_key)) {
        return {{"error", "Failed to load CA certificate/key"}};
    }
    auto cert = mgr.issue_client_cert(cn, bits, days);
    if (cert.cert_pem.empty()) {
        return {{"error", "Failed to issue client certificate"}};
    }
    std::ofstream(cert_out) << cert.cert_pem;
    std::ofstream(key_out) << cert.key_pem;
    return {
        {"status", "ok"},
        {"common_name", cert.common_name},
        {"fingerprint", cert.fingerprint},
        {"serial", cert.serial},
        {"not_before", cert.not_before},
        {"not_after", cert.not_after},
        {"cert_path", cert_out},
        {"key_path", key_out}
    };
}

nlohmann::json cert_verify(const std::string& ca_cert, const std::string& cert_path) {
    odc::CertManager mgr;
    if (!mgr.load_ca(ca_cert, "")) {
        return {{"error", "Failed to load CA certificate"}};
    }
    std::string pem;
    if (!read_file(cert_path, pem)) {
        return {{"error", "Failed to read certificate file"}};
    }
    bool valid = mgr.verify_cert(pem);
    return {{"valid", valid}};
}

nlohmann::json cert_fingerprint(const std::string& cert_path) {
    std::string pem;
    if (!read_file(cert_path, pem)) {
        return {{"error", "Failed to read certificate file"}};
    }
    odc::CertManager mgr;
    if (!mgr.load_ca("", "")) {} // no-op, we just need the instance
    // Use OpenSSL directly for fingerprint of arbitrary cert
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) return {{"error", "Failed to parse PEM"}};
    X509* x509 = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!x509) return {{"error", "Invalid certificate PEM"}};

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    X509_digest(x509, EVP_sha256(), digest, &digest_len);
    X509_free(x509);

    std::ostringstream oss;
    for (unsigned int i = 0; i < digest_len; ++i) {
        if (i > 0) oss << ':';
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return {{"fingerprint", oss.str()}, {"algorithm", "SHA-256"}};
}

nlohmann::json cert_revoke(const std::string& ca_cert, const std::string& ca_key,
                             const std::string& fingerprint, const std::string& crl_out) {
    odc::CertManager mgr;
    if (!mgr.load_ca(ca_cert, ca_key)) {
        return {{"error", "Failed to load CA certificate/key"}};
    }
    if (!mgr.revoke_cert(fingerprint, crl_out)) {
        return {{"error", "Failed to revoke certificate"}};
    }
    return {{"status", "ok"}, {"crl_path", crl_out}};
}

nlohmann::json cert_keypair_wg(const std::string& key_out) {
    auto kp = odc::CertManager::generate_wg_keypair();
    if (kp.private_key_b64.empty() || kp.public_key_b64.empty()) {
        return {{"error", "Failed to generate WireGuard keypair"}};
    }
    if (!key_out.empty()) {
        std::ofstream(key_out) << kp.private_key_b64 << '\n';
        std::ofstream(key_out + ".pub") << kp.public_key_b64 << '\n';
    }
    return {{"status", "ok"},
            {"private_key", kp.private_key_b64},
            {"public_key", kp.public_key_b64}};
}

// ── IPC helper ───────────────────────────────────────────────────────────

nlohmann::json ipc_call(const std::string& socket_path, const std::string& method,
                         const nlohmann::json& params = {}) {
    odc::IpcClient client(socket_path);
    if (!client.connect()) {
        return {{"error", "Cannot connect to " + socket_path}};
    }
    return client.call(method, params);
}

// ── Events watch ─────────────────────────────────────────────────────────

std::atomic<bool> events_running{true};

void signal_handler(int) {
    events_running = false;
}

int events_watch(const std::string& url, const std::string& ca_cert_path,
                 const std::string& client_cert_path, const std::string& client_key_path,
                 const std::vector<std::string>& topics, int timeout_sec) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    asio::io_context io;

    odc::hook::WsClientConfig cfg;
    cfg.ca_cert_path = ca_cert_path;
    cfg.client_cert_path = client_cert_path;
    cfg.client_key_path = client_key_path;
    cfg.verify_server = !ca_cert_path.empty();

    // Parse URL: wss://host:port/path
    std::string url_copy = url;
    if (url_copy.find("wss://") == 0) url_copy = url_copy.substr(6);
    else if (url_copy.find("ws://") == 0) url_copy = url_copy.substr(5);

    size_t colon = url_copy.find(':');
    size_t slash = url_copy.find('/');
    if (colon != std::string::npos) {
        cfg.server_host = url_copy.substr(0, colon);
        std::string port_str = url_copy.substr(colon + 1, slash == std::string::npos ? std::string::npos : slash - colon - 1);
        cfg.server_port = static_cast<uint16_t>(std::stoi(port_str));
    } else {
        cfg.server_host = url_copy.substr(0, slash);
        cfg.server_port = 9443;
    }
    if (slash != std::string::npos) {
        cfg.server_uri = url_copy.substr(slash);
    } else {
        cfg.server_uri = "/ws";
    }
    cfg.reconnect_interval_sec = 60;

    std::mutex print_mutex;
    std::atomic<bool> connected{false};

    odc::hook::WsClient client(io, cfg);
    client.set_on_connected([&]() {
        connected = true;
        for (const auto& topic : topics) {
            nlohmann::json sub = {{"type", "subscribe"}, {"topic", topic}};
            client.send(sub.dump());
        }
    });

    client.set_on_message([&](std::string_view payload) {
        std::lock_guard lock(print_mutex);
        try {
            auto json = nlohmann::json::parse(payload);
            std::cout << json.dump() << std::endl;
        } catch (...) {
            std::cerr << "# Invalid JSON: " << payload << std::endl;
        }
    });

    client.set_on_disconnected([&](const std::string& reason) {
        std::lock_guard lock(print_mutex);
        std::cerr << "# Disconnected: " << reason << std::endl;
        connected = false;
    });

    client.connect();

    std::thread io_thread([&io]() {
        try { io.run(); } catch (const std::exception& e) {
            std::cerr << "# IO error: " << e.what() << std::endl;
        }
    });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    while (events_running.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(100ms);
    }

    client.disconnect();
    io.stop();
    if (io_thread.joinable()) io_thread.join();

    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    CLI::App app{"odc-cli — ODC Root command-line interface"};

    std::string socket_path = "/run/odc-root/odc-ipc.sock";
    app.add_option("-s,--socket", socket_path,
                   "Unix socket path to registry-manager");

    // ── Status ───────────────────────────────────────────────────────────

    auto* status_cmd = app.add_subcommand("status", "Check registry-manager health");
    status_cmd->callback([&]() {
        print_json(ipc_call(socket_path, "ping"));
    });

    // ── Services ─────────────────────────────────────────────────────────

    auto* svc = app.add_subcommand("services", "Service registry commands");
    svc->require_subcommand(1);

    std::string svc_type;
    auto* svc_list = svc->add_subcommand("list", "List registered services");
    svc_list->add_option("-t,--type", svc_type, "Filter by type");
    svc_list->callback([&]() {
        nlohmann::json params;
        if (!svc_type.empty()) params["type"] = svc_type;
        print_json(ipc_call(socket_path, "services.list", params));
    });

    std::string reg_id, reg_name, reg_type;
    auto* svc_reg = svc->add_subcommand("register", "Register a new service");
    svc_reg->add_option("--id", reg_id, "Service ID")->required();
    svc_reg->add_option("--name", reg_name, "Service name")->required();
    svc_reg->add_option("--type", reg_type, "Service type")->required();
    svc_reg->callback([&]() {
        print_json(ipc_call(socket_path, "services.register",
            {{"id", reg_id}, {"name", reg_name}, {"type", reg_type}}));
    });

    std::string unreg_id;
    auto* svc_unreg = svc->add_subcommand("unregister", "Unregister a service");
    svc_unreg->add_option("--id", unreg_id, "Service ID")->required();
    svc_unreg->callback([&]() {
        print_json(ipc_call(socket_path, "services.unregister", {{"id", unreg_id}}));
    });

    // ── Cert ─────────────────────────────────────────────────────────────

    auto* cert = app.add_subcommand("cert", "Certificate management commands");
    cert->require_subcommand(1);

    std::string ca_cert, ca_key, cert_path, cert_out, key_out, org, cn, country;
    std::string fingerprint, crl_out, keypair_out;
    int bits = 2048, days = 365;

    auto* cert_genca = cert->add_subcommand("generate-ca", "Generate a new CA certificate");
    cert_genca->add_option("--cert", ca_cert, "CA certificate output path")->required();
    cert_genca->add_option("--key",  ca_key,  "CA private key output path")->required();
    cert_genca->add_option("--org", org, "Organization")->default_val("odc-root");
    cert_genca->add_option("--cn", cn, "Common Name")->default_val("odc-root CA");
    cert_genca->add_option("--country", country, "Country code")->default_val("XX");
    cert_genca->add_option("--bits", bits, "RSA key bits")->default_val(2048);
    cert_genca->add_option("--days", days, "Validity in days")->default_val(365);
    cert_genca->callback([&]() {
        print_json(cert_generate_ca(ca_cert, ca_key, org, cn, country, bits, days));
    });

    auto* cert_issue_cmd = cert->add_subcommand("issue", "Issue a client certificate signed by CA");
    cert_issue_cmd->add_option("--ca-cert", ca_cert, "CA certificate path")->required();
    cert_issue_cmd->add_option("--ca-key", ca_key, "CA private key path")->required();
    cert_issue_cmd->add_option("--cn", cn, "Common Name for new cert")->required();
    cert_issue_cmd->add_option("--cert-out", cert_out, "Certificate output path")->required();
    cert_issue_cmd->add_option("--key-out", key_out, "Private key output path")->required();
    cert_issue_cmd->add_option("--bits", bits, "RSA key bits")->default_val(2048);
    cert_issue_cmd->add_option("--days", days, "Validity in days")->default_val(365);
    cert_issue_cmd->callback([&]() {
        print_json(cert_issue(ca_cert, ca_key, cn, cert_out, key_out, bits, days));
    });

    auto* cert_vfy = cert->add_subcommand("verify", "Verify certificate against CA");
    cert_vfy->add_option("--ca-cert", ca_cert, "CA certificate path")->required();
    cert_vfy->add_option("--cert", cert_path, "Certificate to verify")->required();
    cert_vfy->callback([&]() {
        auto r = cert_verify(ca_cert, cert_path);
        print_json(r);
        if (!r.value("valid", false)) {
            std::exit(1);
        }
    });

    auto* cert_fp = cert->add_subcommand("fingerprint", "Compute SHA-256 fingerprint of certificate");
    cert_fp->add_option("--cert", cert_path, "Certificate path")->required();
    cert_fp->callback([&]() {
        print_json(cert_fingerprint(cert_path));
    });

    auto* cert_revoke_cmd = cert->add_subcommand("revoke", "Revoke a certificate and update CRL");
    cert_revoke_cmd->add_option("--ca-cert", ca_cert, "CA certificate path")->required();
    cert_revoke_cmd->add_option("--ca-key", ca_key, "CA private key path")->required();
    cert_revoke_cmd->add_option("--fingerprint", fingerprint, "SHA-256 fingerprint to revoke")->required();
    cert_revoke_cmd->add_option("--crl-out", crl_out, "CRL output path")->required();
    cert_revoke_cmd->callback([&]() {
        print_json(cert_revoke(ca_cert, ca_key, fingerprint, crl_out));
    });

    auto* cert_wg = cert->add_subcommand("keypair-wg", "Generate WireGuard Curve25519 keypair");
    cert_wg->add_option("--key-out", keypair_out, "Private key output path");
    cert_wg->callback([&]() {
        print_json(cert_keypair_wg(keypair_out));
    });

    // ── Tunnel ───────────────────────────────────────────────────────────

    std::string tunnel_socket = "/run/odc-root/tunnel-ipc.sock";
    auto* tunnel = app.add_subcommand("tunnel", "Tunnel manager commands");
    tunnel->require_subcommand(1);

    auto* tun_status = tunnel->add_subcommand("status", "Get tunnel manager status");
    tun_status->add_option("-s,--socket", tunnel_socket, "Tunnel IPC socket");
    tun_status->callback([&]() {
        print_json(ipc_call(tunnel_socket, "tunnel.status"));
    });

    auto* tun_peers = tunnel->add_subcommand("peers", "Peer management commands");
    tun_peers->require_subcommand(1);

    auto* tun_peers_list = tun_peers->add_subcommand("list", "List all peers");
    std::string server_filter;
    tun_peers_list->add_option("-s,--socket", tunnel_socket, "Tunnel IPC socket");
    tun_peers_list->add_option("--server", server_filter, "Filter by server ID");
    tun_peers_list->callback([&]() {
        nlohmann::json params;
        if (!server_filter.empty()) params["server_id"] = server_filter;
        print_json(ipc_call(tunnel_socket, "tunnel.peers.list", params));
    });

    std::string peer_key, peer_ip;
    auto* tun_peers_add = tun_peers->add_subcommand("add", "Add a peer");
    tun_peers_add->add_option("-s,--socket", tunnel_socket, "Tunnel IPC socket");
    tun_peers_add->add_option("--public-key", peer_key, "WireGuard public key (base64)")->required();
    tun_peers_add->add_option("--ip", peer_ip, "Assigned IP (auto-allocated if omitted)");
    tun_peers_add->callback([&]() {
        nlohmann::json params{{"public_key", peer_key}};
        if (!peer_ip.empty()) params["allowed_ip"] = peer_ip;
        print_json(ipc_call(tunnel_socket, "tunnel.peers.add", params));
    });

    auto* tun_peers_remove = tun_peers->add_subcommand("remove", "Remove a WireGuard peer");
    tun_peers_remove->add_option("-s,--socket", tunnel_socket, "Tunnel IPC socket");
    tun_peers_remove->add_option("--public-key", peer_key, "WireGuard public key (base64)")->required();
    tun_peers_remove->callback([&]() {
        print_json(ipc_call(tunnel_socket, "tunnel.peers.remove", {{"public_key", peer_key}}));
    });

    std::string peer_cn;
    auto* tun_peers_disconnect = tun_peers->add_subcommand(
        "disconnect", "Disconnect an OpenVPN client by common name");
    tun_peers_disconnect->add_option("-s,--socket", tunnel_socket, "Tunnel IPC socket");
    tun_peers_disconnect->add_option("--cn", peer_cn, "OpenVPN client common name")->required();
    tun_peers_disconnect->callback([&]() {
        print_json(ipc_call(tunnel_socket, "tunnel.peers.disconnect", {{"common_name", peer_cn}}));
    });

    auto* tun_health = tunnel->add_subcommand("health", "Check tunnel manager health");
    tun_health->add_option("-s,--socket", tunnel_socket, "Tunnel IPC socket");
    tun_health->callback([&]() {
        print_json(ipc_call(tunnel_socket, "tunnel.health"));
    });

    auto* tun_clients = tunnel->add_subcommand("clients", "Client credential / profile commands");
    tun_clients->require_subcommand(1);

    std::string issue_type, issue_cn, issue_id, issue_ip, issue_remote, issue_out;
    auto* tun_clients_issue = tun_clients->add_subcommand(
        "issue", "Issue VPN client credentials and profile via tunnel-manager");
    tun_clients_issue->add_option("-s,--socket", tunnel_socket, "Tunnel IPC socket");
    tun_clients_issue->add_option("--type", issue_type, "openvpn|wireguard")->required();
    tun_clients_issue->add_option("--cn", issue_cn, "Common name / client label")->required();
    tun_clients_issue->add_option("--client-id", issue_id, "Optional client id");
    tun_clients_issue->add_option("--ip", issue_ip, "WireGuard assigned IP (optional)");
    tun_clients_issue->add_option("--remote", issue_remote, "Endpoint/remote host override");
    tun_clients_issue->add_option("--output", issue_out, "Write profile to path");
    tun_clients_issue->callback([&]() {
        nlohmann::json params{
            {"tunnel_type", issue_type},
            {"common_name", issue_cn},
        };
        if (!issue_id.empty()) params["client_id"] = issue_id;
        if (!issue_ip.empty()) params["assigned_ip"] = issue_ip;
        if (!issue_remote.empty()) params["remote"] = issue_remote;
        if (!issue_out.empty()) params["output_path"] = issue_out;
        print_json(ipc_call(tunnel_socket, "tunnel.clients.issue", params));
    });

    // ── Events ───────────────────────────────────────────────────────────

    std::string ws_url = "wss://127.0.0.1:9443/ws";
    std::string ws_ca_cert, ws_client_cert, ws_client_key;
    std::vector<std::string> event_topics;
    int events_timeout = 30;

    auto* events = app.add_subcommand("events", "Event monitoring commands");
    events->require_subcommand(1);

    auto* events_watch_cmd = events->add_subcommand("watch", "Watch real-time events via WebSocket");
    events_watch_cmd->add_option("--url", ws_url, "WebSocket URL")->default_val("wss://127.0.0.1:9443/ws");
    events_watch_cmd->add_option("--ca-cert", ws_ca_cert, "CA cert for server verification");
    events_watch_cmd->add_option("--client-cert", ws_client_cert, "Client cert for mTLS");
    events_watch_cmd->add_option("--client-key", ws_client_key, "Client key for mTLS");
    events_watch_cmd->add_option("-t,--topic", event_topics, "Topics to subscribe (repeatable, default all)");
    events_watch_cmd->add_option("--timeout", events_timeout, "Exit after N seconds (0=forever)")->default_val(30);
    events_watch_cmd->callback([&]() {
        if (event_topics.empty()) event_topics.push_back("*");
        int tout = events_timeout == 0 ? 86400 : events_timeout;
        int rc = events_watch(ws_url, ws_ca_cert, ws_client_cert, ws_client_key, event_topics, tout);
        std::exit(rc);
    });

    // ── Hook ─────────────────────────────────────────────────────────────

    std::string hook_socket = "/run/odc-root/hook.sock";
    auto* hook = app.add_subcommand("hook", "Hook client commands");
    hook->require_subcommand(1);

    auto* hook_status = hook->add_subcommand("status", "Check hook client status");
    hook_status->add_option("-s,--socket", hook_socket, "Hook IPC socket");
    hook_status->callback([&]() {
        print_json(ipc_call(hook_socket, "hook.status"));
    });

    // ── Parse ───────────────────────────────────────────────────────────────

    CLI11_PARSE(app, argc, argv);

    if (argc == 1) {
        std::cout << app.help() << std::endl;
    }

    return 0;
}
