#include "core/app_lifecycle.hpp"
#include "core/privilege_check.hpp"
#include "core/credential_crypto.hpp"
#include "actors/log_actor.hpp"
#include "actors/system_monitor_actor.hpp"
#include "actors/vpn_manager_actor.hpp"
#include "cli/cli_app.hpp"
#include "cli/cli_backend.hpp"
#include "cli/cli_local_backend.hpp"
#include "cli/cli_remote_backend.hpp"
#include "ipc/ipc_client.hpp"

#include <caf/actor_system.hpp>

#include <csignal>
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running.store(false, std::memory_order_release);
}

static void encrypt_creds(const std::string& input_path, const std::string& output_path, const std::string& key_path) {
    if (!odc::CredentialCrypto::init()) {
        std::cerr << "Failed to initialize libsodium" << std::endl;
        exit(1);
    }

    if (odc::CredentialCrypto::encrypt_file(input_path, output_path, key_path)) {
        std::cout << "Credentials encrypted successfully" << std::endl;
        std::cout << "Encrypted file: " << output_path << std::endl;
        std::cout << "Key file: " << key_path << std::endl;
    } else {
        std::cerr << "Failed to encrypt credentials" << std::endl;
        exit(1);
    }
}

static void decrypt_creds(const std::string& input_path, const std::string& key_path) {
    if (!odc::CredentialCrypto::init()) {
        std::cerr << "Failed to initialize libsodium" << std::endl;
        exit(1);
    }

    try {
        std::string decrypted = odc::CredentialCrypto::decrypt_file(input_path, key_path);
        std::cout << decrypted;
    } catch (const std::exception& e) {
        std::cerr << "Failed to decrypt: " << e.what() << std::endl;
        exit(1);
    }
}

static void run_cli_local(const std::filesystem::path& config_path, bool json_mode) {
    auto priv_result = odc::check_privileges();
    if (!priv_result.has_net_admin && !priv_result.is_root) {
        std::cerr << "ERROR: Missing CAP_NET_ADMIN privilege. VPN operations require network administration rights.\n";
        std::cerr << "\nFix options:\n";
        std::cerr << "  1. Run with: sudo ./odc-onboard\n";
        std::cerr << "  2. Set capability: sudo setcap cap_net_admin+ep ./odc-onboard\n";
        std::cerr << "  3. Use --check-privileges to verify before starting\n";
        std::cerr << "\nExiting.\n";
        exit(1);
    }

    odc::AppLifecycle app;
    app.set_drain_timeout(10);

    if (!app.startup(config_path)) {
        std::cerr << "Failed to start application" << std::endl;
        exit(1);
    }

    auto& sys = app.actor_system();
    auto& bus = app.event_bus();
    auto& log = app.log_control();
    auto& orch = app.orchestrator();
    auto& vpn_mgr = app.vpn_manager();

    auto logger = log.get_logger("main");
    SPDLOG_LOGGER_INFO(logger, "Spawning actors...");

    auto log_actor_hdl = odc::make_log_actor(sys, bus, log);
    auto monitor_hdl = odc::make_system_monitor_actor(sys, orch, bus);
    auto vpn_actor_hdl = odc::make_vpn_manager_actor(sys, vpn_mgr);

    SPDLOG_LOGGER_INFO(logger, "Actors spawned.");

    auto connections = vpn_mgr.list_connections();
    if (!connections.empty()) {
        SPDLOG_LOGGER_INFO(logger, "Loaded {} VPN connections", connections.size());
    }

    odc::CliLocalBackend backend(vpn_mgr, log, orch, app.mav_router(), app.mav_extender_manager(), app.rtsp_compressor());
    odc::CliApp cli(backend);
    cli.set_json_mode(json_mode);
    cli.run();

    bus.publish(odc::ShutdownEvent{
        .exit_code = 0,
        .reason = "Normal shutdown"
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    app.shutdown();
    std::_Exit(0);
}

static void run_cli_remote(bool json_mode) {
    odc::ipc::IpcClient client;
    if (!client.connect("/run/odc-onboard/odc-onboard.sock")) {
        std::cerr << "ERROR: odc-onboard service is not running." << std::endl;
        std::cerr << "Start it with: sudo systemctl start odc-onboard" << std::endl;
        exit(1);
    }

    try {
        client.send_request("subscribe", {}, 3000);
    } catch (...) {
    }

    client.event_callback_ = [](const std::string& json_str) {
        try {
            auto j = nlohmann::json::parse(json_str);
            std::string method = j.value("method", "");
            if (method == "vpn.state_change") {
                auto p = j["params"];
                std::cout << "\n[event] " << p.value("connection_id", "")
                          << ": " << p.value("old_state", "")
                          << " -> " << p.value("new_state", "") << "\n";
                std::cout << "odc-onboard> " << std::flush;
            } else if (method == "vpn.stats") {
                auto p = j["params"];
                std::cout << "\n[event] " << p.value("connection_id", "")
                          << " stats: " << p.value("bytes_in", 0) << "B in, "
                          << p.value("bytes_out", 0) << "B out\n";
                std::cout << "odc-onboard> " << std::flush;
            } else if (method == "vpn.health_check") {
                auto p = j["params"];
                std::cout << "\n[event] " << p.value("connection_id", "")
                          << " health: " << (p.value("healthy", false) ? "OK" : "FAIL")
                          << " latency=" << p.value("latency_ms", 0) << "ms\n";
                std::cout << "odc-onboard> " << std::flush;
            } else if (method == "log") {
                auto p = j["params"];
                std::cout << "\n[log:" << p.value("level", "") << "] "
                          << p.value("message", "") << "\n";
                std::cout << "odc-onboard> " << std::flush;
            }
        } catch (...) {
        }
    };

    odc::CliRemoteBackend backend(client);
    odc::CliApp cli(backend);
    cli.set_json_mode(json_mode);
    cli.run();

    try {
        client.send_request("unsubscribe", {}, 1000);
    } catch (...) {
    }
}

static void run_daemon(const std::filesystem::path& config_path, const std::string& socket_path) {
    auto priv_result = odc::check_privileges();
    if (!priv_result.has_net_admin && !priv_result.is_root) {
        std::cerr << "ERROR: Missing CAP_NET_ADMIN privilege. VPN operations require network administration rights.\n";
        std::cerr << "\nFix options:\n";
        std::cerr << "  1. Run with: sudo ./odc-onboard\n";
        std::cerr << "  2. Set capability: sudo setcap cap_net_admin+ep ./odc-onboard\n";
        std::cerr << "  3. Use --check-privileges to verify before starting\n";
        std::cerr << "\nExiting.\n";
        exit(1);
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    odc::AppLifecycle app;

    if (!app.startup(config_path)) {
        std::cerr << "Failed to start application" << std::endl;
        exit(1);
    }

    app.start_ipc_server(socket_path);

    auto& sys = app.actor_system();
    auto& bus = app.event_bus();
    auto& log = app.log_control();
    auto& orch = app.orchestrator();
    auto& vpn_mgr = app.vpn_manager();

    auto logger = log.get_logger("main");

    SPDLOG_LOGGER_INFO(logger, "Spawning actors...");

    auto log_actor_hdl = odc::make_log_actor(sys, bus, log);
    auto monitor_hdl = odc::make_system_monitor_actor(sys, orch, bus);
    auto vpn_actor_hdl = odc::make_vpn_manager_actor(sys, vpn_mgr);

    SPDLOG_LOGGER_INFO(logger, "Actors spawned.");

    auto connections = vpn_mgr.list_connections();
    if (!connections.empty()) {
        SPDLOG_LOGGER_INFO(logger, "Loaded {} VPN connections", connections.size());
        for (const auto& conn : connections) {
            SPDLOG_LOGGER_INFO(logger, "  {} ({}) - {}", conn.id, odc::to_string_view(conn.type), odc::to_string_view(conn.state));
        }
    }

    SPDLOG_LOGGER_INFO(logger, "Running as daemon. Use --cli to attach.");
    SPDLOG_LOGGER_INFO(logger, "Waiting for signal...");

    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    SPDLOG_LOGGER_INFO(logger, "Signal received, shutting down");

    bus.publish(odc::ShutdownEvent{
        .exit_code = 0,
        .reason = "Normal shutdown"
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    app.shutdown();
    std::_Exit(0);
}

int main(int argc, char* argv[]) {
    std::filesystem::path config_path = "/etc/odc-onboard/config.json";
    bool cli_mode = false;
    bool daemon_mode = false;
    bool check_only = false;
    bool json_mode = false;
    std::string encrypt_input, encrypt_output, encrypt_key;
    std::string decrypt_input, decrypt_key;
    std::string socket_path = "/run/odc-onboard/odc-onboard.sock";
    uint32_t drain_timeout = 10;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--cli") {
            cli_mode = true;
        } else if (arg == "--daemon") {
            daemon_mode = true;
        } else if (arg == "--check-privileges") {
            check_only = true;
        } else if (arg == "--json") {
            json_mode = true;
        } else if (arg == "--socket" && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (arg == "--drain-timeout" && i + 1 < argc) {
            drain_timeout = std::stoul(argv[++i]);
        } else if (arg == "--encrypt-creds" && i + 3 < argc) {
            encrypt_input = argv[++i];
            encrypt_output = argv[++i];
            encrypt_key = argv[++i];
        } else if (arg == "--decrypt-creds" && i + 2 < argc) {
            decrypt_input = argv[++i];
            decrypt_key = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: odc-onboard [OPTIONS]\n";
            std::cout << "\nOptions:\n";
            std::cout << "  --config <path>               Config file path (default: /etc/odc-onboard/config.json)\n";
            std::cout << "  --daemon                      Run as daemon (IPC server mode)\n";
            std::cout << "  --cli                         Attach to daemon or start standalone CLI\n";
            std::cout << "  --socket <path>               IPC socket path (default: /run/odc-onboard/odc-onboard.sock)\n";
            std::cout << "  --check-privileges            Check network privileges and exit\n";
            std::cout << "  --json                        Enable JSON output mode\n";
            std::cout << "  --drain-timeout <seconds>     Graceful shutdown drain timeout (default: 10)\n";
            std::cout << "  --encrypt-creds <in> <out> <key>  Encrypt credentials file\n";
            std::cout << "  --decrypt-creds <in> <key>    Decrypt and display credentials\n";
            std::cout << "  --help, -h                    Show this help\n";
            return 0;
        }
    }

    if (!encrypt_input.empty()) {
        encrypt_creds(encrypt_input, encrypt_output, encrypt_key);
        return 0;
    }

    if (!decrypt_input.empty()) {
        decrypt_creds(decrypt_input, decrypt_key);
        return 0;
    }

    if (check_only) {
        auto result = odc::check_privileges();
        if (json_mode) {
            nlohmann::json j;
            j["is_root"] = result.is_root;
            j["has_net_admin"] = result.has_net_admin;
            j["missing_capabilities"] = result.missing_capabilities;
            if (!result.error_message.empty()) {
                j["error"] = result.error_message;
            }
            std::cout << j.dump(2) << std::endl;
        } else {
            std::cout << odc::format_privilege_result(result);
        }
        return result.has_net_admin || result.is_root ? 0 : 1;
    }

    if (daemon_mode) {
        run_daemon(config_path, socket_path);
        return 0;
    }

    if (odc::ipc::socket_exists("/run/odc-onboard/odc-onboard.sock")) {
        run_cli_remote(json_mode);
        return 0;
    }

    if (cli_mode) {
        run_cli_local(config_path, json_mode);
        return 0;
    }

    auto priv_result = odc::check_privileges();
    if (!priv_result.has_net_admin && !priv_result.is_root) {
        std::cerr << "ERROR: Missing CAP_NET_ADMIN privilege. VPN operations require network administration rights.\n";
        std::cerr << "\nFix options:\n";
        std::cerr << "  1. Run with: sudo ./odc-onboard\n";
        std::cerr << "  2. Set capability: sudo setcap cap_net_admin+ep ./odc-onboard\n";
        std::cerr << "  3. Use --check-privileges to verify before starting\n";
        std::cerr << "\nExiting.\n";
        return 1;
    }

    run_daemon(config_path, socket_path);

    return 0;
}
