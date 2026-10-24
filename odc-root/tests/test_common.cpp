#include <catch2/catch_test_macros.hpp>

#include "odc/sqlite_db.hpp"
#include "odc/cert_manager.hpp"
#include "odc/yaml_config.hpp"
#include "odc/ipc_server.hpp"
#include "odc/ipc_client.hpp"
#include "odc/logging.hpp"

#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace odc;

// ── SqliteDb tests ───────────────────────────────────────────────────────────

TEST_CASE("SqliteDb open/close", "[sqlite]") {
    SqliteDb db;
    REQUIRE(db.open(":memory:"));
    REQUIRE(db.is_open());
    db.close();
    REQUIRE_FALSE(db.is_open());
}

TEST_CASE("SqliteDb create table and insert", "[sqlite]") {
    SqliteDb db;
    REQUIRE(db.open(":memory:"));
    REQUIRE(db.execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)"));
    REQUIRE(db.execute("INSERT INTO test (name) VALUES ('hello')"));
    REQUIRE(db.execute("INSERT INTO test (name) VALUES ('world')"));

    auto stmt = db.prepare("SELECT COUNT(*) FROM test");
    REQUIRE(stmt.valid());
    REQUIRE(stmt.step());
    REQUIRE(stmt.column_int(0) == 2);
}

TEST_CASE("SqliteDb prepared statement", "[sqlite]") {
    SqliteDb db;
    REQUIRE(db.open(":memory:"));
    REQUIRE(db.execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)"));

    auto ins = db.prepare("INSERT INTO users (name, age) VALUES (?, ?)");
    REQUIRE(ins.valid());
    ins.bind(1, std::string("Alice"));
    ins.bind(2, 30);
    REQUIRE(ins.execute());

    ins.reset();
    ins.clear_bindings();
    ins.bind(1, std::string("Bob"));
    ins.bind(2, 25);
    REQUIRE(ins.execute());

    auto sel = db.prepare("SELECT name, age FROM users WHERE id = ?");
    REQUIRE(sel.valid());
    sel.bind(1, 1);
    REQUIRE(sel.step());
    REQUIRE(sel.column_text(0) == "Alice");
    REQUIRE(sel.column_int(1) == 30);

    sel.reset();
    sel.clear_bindings();
    sel.bind(1, 2);
    REQUIRE(sel.step());
    REQUIRE(sel.column_text(0) == "Bob");
}

TEST_CASE("SqliteDb transactions", "[sqlite]") {
    SqliteDb db;
    REQUIRE(db.open(":memory:"));
    REQUIRE(db.execute("CREATE TABLE t (val INTEGER)"));
    REQUIRE(db.begin_transaction());
    REQUIRE(db.execute("INSERT INTO t (val) VALUES (42)"));
    REQUIRE(db.rollback());

    auto stmt = db.prepare("SELECT COUNT(*) FROM t");
    REQUIRE(stmt.valid());
    REQUIRE(stmt.step());
    REQUIRE(stmt.column_int(0) == 0);

    REQUIRE(db.begin_transaction());
    REQUIRE(db.execute("INSERT INTO t (val) VALUES (99)"));
    REQUIRE(db.commit());

    stmt = db.prepare("SELECT COUNT(*) FROM t");
    REQUIRE(stmt.valid());
    REQUIRE(stmt.step());
    REQUIRE(stmt.column_int(0) == 1);
}

TEST_CASE("SqliteDb schema migrations", "[sqlite]") {
    SqliteDb db;
    REQUIRE(db.open(":memory:"));
    REQUIRE(db.schema_version() == 0);

    REQUIRE(db.ensure_schema(1, "CREATE TABLE v1_test (id INTEGER)"));
    REQUIRE(db.schema_version() == 1);

    REQUIRE(db.ensure_schema(3, "CREATE TABLE v3_test (name TEXT)"));
    REQUIRE(db.schema_version() == 3);
}

// ── CertManager tests ────────────────────────────────────────────────────────

TEST_CASE("CertManager create CA", "[cert]") {
    CertManager mgr;
    CaConfig cfg;
    cfg.ca_cert_path = "/tmp/test-odc-ca.crt";
    cfg.ca_key_path = "/tmp/test-odc-ca.key";
    cfg.country = "FR";
    cfg.organization = "odc-test";
    cfg.key_bits = 2048;
    cfg.validity_days = 365;

    REQUIRE(mgr.create_ca(cfg));
    REQUIRE(mgr.ca_loaded());

    std::filesystem::remove(cfg.ca_cert_path);
    std::filesystem::remove(cfg.ca_key_path);
}

TEST_CASE("CertManager load CA from files", "[cert]") {
    CertManager create_mgr;
    CaConfig cfg;
    cfg.ca_cert_path = "/tmp/test-odc-load-ca.crt";
    cfg.ca_key_path = "/tmp/test-odc-load-ca.key";
    REQUIRE(create_mgr.create_ca(cfg));

    CertManager load_mgr;
    REQUIRE(load_mgr.load_ca(cfg.ca_cert_path, cfg.ca_key_path));
    REQUIRE(load_mgr.ca_loaded());

    std::filesystem::remove(cfg.ca_cert_path);
    std::filesystem::remove(cfg.ca_key_path);
}

TEST_CASE("CertManager issue client cert", "[cert]") {
    CertManager mgr;
    CaConfig cfg;
    cfg.ca_cert_path = "/tmp/test-odc-issue-ca.crt";
    cfg.ca_key_path = "/tmp/test-odc-issue-ca.key";
    REQUIRE(mgr.create_ca(cfg));

    auto cert = mgr.issue_client_cert("test-client", 2048, 365);
    REQUIRE_FALSE(cert.cert_pem.empty());
    REQUIRE_FALSE(cert.key_pem.empty());
    REQUIRE_FALSE(cert.fingerprint.empty());
    REQUIRE(cert.common_name == "test-client");

    REQUIRE(mgr.verify_cert(cert.cert_pem));

    std::filesystem::remove(cfg.ca_cert_path);
    std::filesystem::remove(cfg.ca_key_path);
}

TEST_CASE("CertManager curve25519 keypair", "[cert]") {
    auto priv = CertManager::generate_curve25519_private_key();
    REQUIRE_FALSE(priv.empty());
    REQUIRE(priv.find("PRIVATE KEY") != std::string::npos);

    auto pub = CertManager::generate_curve25519_public_key(priv);
    REQUIRE_FALSE(pub.empty());
    REQUIRE(pub.find("PUBLIC KEY") != std::string::npos);
}

// ── YamlConfig tests ─────────────────────────────────────────────────────────

TEST_CASE("YamlConfig load and save", "[yaml]") {
    const std::string path = "/tmp/test-odc-yaml.yaml";
    std::filesystem::remove(path);

    YAML::Node root;
    root["version"] = 1;
    root["name"] = "test";
    YAML::Node items;
    items.push_back("a");
    items.push_back("b");
    root["items"] = items;

    save_yaml(root, path);
    REQUIRE(std::filesystem::exists(path));

    auto loaded = load_yaml(path);
    REQUIRE(loaded["version"].as<int>() == 1);
    REQUIRE(loaded["name"].as<std::string>() == "test");
    REQUIRE(has_key(loaded, "items"));

    std::filesystem::remove(path);
}

TEST_CASE("YamlConfig atomic write produces bak", "[yaml]") {
    const std::string path = "/tmp/test-odc-atom.yaml";
    const std::string bak_path = path + ".bak";
    std::filesystem::remove(path);
    std::filesystem::remove(bak_path);

    YAML::Node root;
    root["val"] = 1;
    save_yaml(root, path);

    YAML::Node root2;
    root2["val"] = 2;
    save_yaml(root2, path);

    auto loaded = load_yaml(path);
    REQUIRE(loaded["val"].as<int>() == 2);

    // After successful write, .bak should be cleaned up
    REQUIRE_FALSE(std::filesystem::exists(bak_path));
    std::filesystem::remove(path);
}

// ── IPC tests ────────────────────────────────────────────────────────────────

TEST_CASE("IpcServer start and stop", "[ipc]") {
    const std::string sock = "/tmp/test-odc-ipc-start.sock";
    std::filesystem::remove(sock);

    IpcServer server(sock);
    REQUIRE(server.start());
    REQUIRE(server.is_running());
    server.stop();
    REQUIRE_FALSE(server.is_running());
    REQUIRE_FALSE(std::filesystem::exists(sock));
}

TEST_CASE("IpcServer request/response", "[ipc]") {
    const std::string sock = "/tmp/test-odc-ipc-req.sock";
    std::filesystem::remove(sock);

    IpcServer server(sock);
    server.register_method("echo", [](const nlohmann::json& params) {
        return params;
    });
    server.register_method("add", [](const nlohmann::json& params) {
        int a = params.value("a", 0);
        int b = params.value("b", 0);
        return nlohmann::json{{"result", a + b}};
    });

    REQUIRE(server.start());

    std::thread server_thread([&]() { server.run(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    IpcClient client(sock);
    REQUIRE(client.connect());
    REQUIRE(client.is_connected());

    auto resp = client.call("echo", {{"msg", "hello"}});
    REQUIRE(resp.contains("msg"));
    REQUIRE(resp["msg"] == "hello");

    auto add_resp = client.call("add", {{"a", 3}, {"b", 4}});
    REQUIRE(add_resp.contains("result"));
    REQUIRE(add_resp["result"] == 7);

    client.disconnect();
    server.stop();
    if (server_thread.joinable()) server_thread.join();
}

TEST_CASE("IpcServer notification", "[ipc]") {
    const std::string sock = "/tmp/test-odc-ipc-notif.sock";
    std::filesystem::remove(sock);

    std::atomic<int> notif_count{0};

    IpcServer server(sock);
    server.register_method("notify_test", [&](const nlohmann::json&) {
        notif_count++;
        return nlohmann::json{{"ok", true}};
    });

    REQUIRE(server.start());
    std::thread server_thread([&]() { server.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    IpcClient client(sock);
    REQUIRE(client.connect());
    client.notify("notify_test", {{"data", 1}});
    client.notify("notify_test", {{"data", 2}});

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    REQUIRE(notif_count == 2);

    client.disconnect();
    server.stop();
    if (server_thread.joinable()) server_thread.join();
}

TEST_CASE("IpcServer unknown method returns error", "[ipc]") {
    const std::string sock = "/tmp/test-odc-ipc-unknown.sock";
    std::filesystem::remove(sock);

    IpcServer server(sock);
    REQUIRE(server.start());
    std::thread server_thread([&]() { server.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    IpcClient client(sock);
    REQUIRE(client.connect());

    auto resp = client.call("nonexistent_method", {});
    REQUIRE(resp.contains("error"));

    client.disconnect();
    server.stop();
    if (server_thread.joinable()) server_thread.join();
}
