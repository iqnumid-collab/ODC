#include <catch2/catch_test_macros.hpp>

#include "odc/ipc_server.hpp"
#include "odc/ipc_client.hpp"
#include "odc/sqlite_db.hpp"

#include <thread>
#include <chrono>
#include <filesystem>

using namespace odc;

// ── Integration: IPC notification round-trip ────────────────────────────────

TEST_CASE("IPC integration: registry registers methods, tunnel sends notifications", "[integration]") {
    const std::string sock = "/tmp/test-odc-integration.sock";
    std::filesystem::remove(sock);

    // Track received notifications
    std::atomic<int> connected_count{0};
    std::atomic<int> disconnected_count{0};
    nlohmann::json last_connected_params;
    nlohmann::json last_disconnected_params;

    // Start IPC server (registry-manager side)
    IpcServer server(sock);

    server.register_method("tunnel.client.connected",
        [&](const nlohmann::json& params) {
            connected_count++;
            last_connected_params = params;
            return nlohmann::json{{"ok", true}};
        });

    server.register_method("tunnel.client.disconnected",
        [&](const nlohmann::json& params) {
            disconnected_count++;
            last_disconnected_params = params;
            return nlohmann::json{{"ok", true}};
        });

    server.register_method("tunnel.server.started",
        [](const nlohmann::json&) {
            return nlohmann::json{{"ok", true}};
        });

    server.register_method("ping",
        [](const nlohmann::json&) {
            return nlohmann::json{{"pong", true}};
        });

    REQUIRE(server.start());

    std::thread server_thread([&]() { server.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Start IPC client (tunnel-manager side)
    IpcClient client(sock);
    REQUIRE(client.connect());
    REQUIRE(client.is_connected());

    // Send notifications
    client.notify("tunnel.server.started", {
        {"server_id", "wg-main"},
        {"type", "wireguard"}
    });

    client.notify("tunnel.client.connected", {
        {"client_id", "test-client-001"},
        {"server_id", "wg-main"},
        {"tunnel_type", "wireguard"},
        {"assigned_ip", "10.0.0.2"},
    });

    client.notify("tunnel.client.disconnected", {
        {"client_id", "test-client-001"},
        {"server_id", "wg-main"},
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Verify notifications received
    REQUIRE(connected_count == 1);
    REQUIRE(disconnected_count == 1);
    REQUIRE(last_connected_params["client_id"] == "test-client-001");
    REQUIRE(last_connected_params["assigned_ip"] == "10.0.0.2");

    // Ping
    auto ping_resp = client.call("ping");
    REQUIRE(ping_resp.contains("pong"));
    REQUIRE(ping_resp["pong"] == true);

    client.disconnect();
    server.stop();
    if (server_thread.joinable()) server_thread.join();
}

// ── Integration: Service registration via IPC ───────────────────────────────

TEST_CASE("IPC integration: service register/unregister round-trip", "[integration]") {
    const std::string sock = "/tmp/test-odc-svc-integration.sock";
    std::filesystem::remove(sock);

    std::vector<nlohmann::json> registered_services;
    std::vector<std::string> unregistered_ids;

    IpcServer server(sock);

    server.register_method("services.list",
        [&](const nlohmann::json&) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& s : registered_services) {
                arr.push_back(s);
            }
            return nlohmann::json{{"services", arr}, {"total", arr.size()}};
        });

    server.register_method("services.register",
        [&](const nlohmann::json& params) {
            registered_services.push_back(params);
            return nlohmann::json{{"ok", true}};
        });

    server.register_method("services.unregister",
        [&](const nlohmann::json& params) {
            unregistered_ids.push_back(params["id"].get<std::string>());
            return nlohmann::json{{"ok", true}};
        });

    REQUIRE(server.start());
    std::thread server_thread([&]() { server.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    IpcClient client(sock);
    REQUIRE(client.connect());

    // Register two services
    auto r1 = client.call("services.register", {
        {"id", "svc-1"},
        {"name", "Service One"},
        {"type", "test"},
    });
    REQUIRE(r1["ok"] == true);

    auto r2 = client.call("services.register", {
        {"id", "svc-2"},
        {"name", "Service Two"},
        {"type", "test"},
    });
    REQUIRE(r2["ok"] == true);

    // List
    auto list = client.call("services.list");
    REQUIRE(list["total"] == 2);

    // Unregister
    auto unreg = client.call("services.unregister", {{"id", "svc-1"}});
    REQUIRE(unreg["ok"] == true);
    REQUIRE(unregistered_ids.size() == 1);
    REQUIRE(unregistered_ids[0] == "svc-1");

    client.disconnect();
    server.stop();
    if (server_thread.joinable()) server_thread.join();
}
