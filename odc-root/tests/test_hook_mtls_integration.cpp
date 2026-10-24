#include <catch2/catch_test_macros.hpp>

#include "ws_server.hpp"
#include "ws_client.hpp"
#include "odc/cert_manager.hpp"
#include "odc/logging.hpp"

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace odc;
using namespace odc::hook;
using namespace std::chrono_literals;

namespace {

struct TestCerts {
    std::string ca_cert, ca_key;
    std::string server_cert, server_key;
    std::string client_cert, client_key;
    std::string client_fingerprint;
    std::string temp_dir;

    void cleanup() {
        if (!temp_dir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(temp_dir, ec);
        }
    }
};

TestCerts generate_test_certs() {
    init_logging(spdlog::level::info, true);

    TestCerts tc;
    tc.temp_dir = std::filesystem::temp_directory_path() /
        ("odc-e2e-" + std::to_string(std::rand()));
    std::filesystem::create_directories(tc.temp_dir);

    tc.ca_cert  = tc.temp_dir + "/ca.crt";
    tc.ca_key   = tc.temp_dir + "/ca.key";
    tc.server_cert = tc.temp_dir + "/server.crt";
    tc.server_key  = tc.temp_dir + "/server.key";
    tc.client_cert = tc.temp_dir + "/client.crt";
    tc.client_key  = tc.temp_dir + "/client.key";

    CertManager ca;
    CaConfig ca_cfg{tc.ca_cert, tc.ca_key, "XX", "ODC-Test-CA", "odc-root CA", 2048, 30};
    REQUIRE(ca.create_ca(ca_cfg));

    {
        CertManager mgr;
        REQUIRE(mgr.load_ca(tc.ca_cert, tc.ca_key));
        auto cert = mgr.issue_client_cert("odc-registry-test");
        REQUIRE_FALSE(cert.cert_pem.empty());
        std::ofstream(tc.server_cert) << cert.cert_pem;
        std::ofstream(tc.server_key) << cert.key_pem;
    }

    {
        CertManager mgr;
        REQUIRE(mgr.load_ca(tc.ca_cert, tc.ca_key));
        auto cert = mgr.issue_client_cert("odc-hook-test");
        REQUIRE_FALSE(cert.cert_pem.empty());
        std::ofstream(tc.client_cert) << cert.cert_pem;
        std::ofstream(tc.client_key) << cert.key_pem;
        tc.client_fingerprint = cert.fingerprint;
    }

    return tc;
}

struct ServerFixture {
    WsServer server;
    std::thread th;
    ServerFixture(WsServerConfig cfg) : server(cfg) {}
    ~ServerFixture() {
        server.stop();
        if (th.joinable()) th.join();
    }
    void start_server() {
        REQUIRE(server.start());
        th = std::thread([this]() { server.run(); });
        std::this_thread::sleep_for(200ms);
    }
};

// Use raw asio SSL client for handshake-only tests (no WS needed)
bool try_ssl_connect(uint16_t port,
                     const std::string& client_cert_path,
                     const std::string& client_key_path,
                     const std::string& ca_cert_path,
                     bool verify_server) {
    asio::io_context io;
    asio::ssl::context ssl_ctx(asio::ssl::context::tls_client);

    if (!client_cert_path.empty() && !client_key_path.empty()) {
        ssl_ctx.use_certificate_chain_file(client_cert_path);
        ssl_ctx.use_private_key_file(client_key_path, asio::ssl::context::pem);
    }

    if (verify_server && !ca_cert_path.empty()) {
        ssl_ctx.load_verify_file(ca_cert_path);
        ssl_ctx.set_verify_mode(asio::ssl::verify_peer);
    } else {
        ssl_ctx.set_verify_mode(asio::ssl::verify_none);
    }

    ssl_ctx.set_options(
        asio::ssl::context::default_workarounds |
        asio::ssl::context::no_sslv2 |
        asio::ssl::context::no_sslv3 |
        asio::ssl::context::no_tlsv1 |
        asio::ssl::context::no_tlsv1_1);

    asio::ssl::stream<asio::ip::tcp::socket> sock(io, ssl_ctx);
    asio::error_code ec;
    asio::ip::tcp::resolver resolver(io);
    auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port), ec);
    if (ec) return false;

    asio::connect(sock.lowest_layer(), endpoints, ec);
    if (ec) return false;

    sock.handshake(asio::ssl::stream_base::client, ec);
    return !ec;
}

void run_ws_client_until(asio::io_context& io, WsClient& client,
                         std::atomic<bool>& connected,
                         std::chrono::seconds timeout = 5s) {
    std::thread io_thread([&io]() {
        try { io.run(); }
        catch (const std::exception& e) {
            UNSCOPED_INFO("io.run exception: " << e.what());
        }
    });

    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!connected.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(50ms);
    }

    client.disconnect();
    io.stop();
    if (io_thread.joinable()) io_thread.join();
}

} // namespace

// ── Test 1: Plain TLS handshake (no mTLS) ────────────────────────────────────

TEST_CASE("E2E: plain TLS handshake succeeds", "[e2e]") {
    auto certs = generate_test_certs();
    struct Cleanup { TestCerts& c; ~Cleanup() { c.cleanup(); } } _c{certs};

    WsServerConfig cfg;
    cfg.listen_port = 19543;
    cfg.listen_address = "127.0.0.1";
    cfg.tls_cert_path = certs.server_cert;
    cfg.tls_key_path = certs.server_key;
    cfg.mtls.enabled = false;

    ServerFixture fix(cfg);
    fix.start_server();

    asio::io_context io;
    asio::ssl::context ssl_ctx(asio::ssl::context::tls_client);
    ssl_ctx.set_verify_mode(asio::ssl::verify_none);

    asio::ssl::stream<asio::ip::tcp::socket> sock(io, ssl_ctx);
    asio::error_code ec;
    asio::ip::tcp::resolver resolver(io);
    auto endpoints = resolver.resolve("127.0.0.1", "19543", ec);
    REQUIRE_FALSE(ec);
    asio::connect(sock.lowest_layer(), endpoints, ec);
    REQUIRE_FALSE(ec);
    sock.handshake(asio::ssl::stream_base::client, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(sock.lowest_layer().is_open());
}

// ── Test 2: Client verifies server cert against CA ───────────────────────────

TEST_CASE("E2E: client verifies server certificate against CA", "[e2e]") {
    auto certs = generate_test_certs();
    struct Cleanup { TestCerts& c; ~Cleanup() { c.cleanup(); } } _c{certs};

    WsServerConfig cfg;
    cfg.listen_port = 19540;
    cfg.listen_address = "127.0.0.1";
    cfg.tls_cert_path = certs.server_cert;
    cfg.tls_key_path = certs.server_key;
    cfg.mtls.enabled = false;  // No mTLS — only client verifies server

    ServerFixture fix(cfg);
    fix.start_server();

    asio::io_context io;
    asio::ssl::context ssl_ctx(asio::ssl::context::tls_client);
    ssl_ctx.load_verify_file(certs.ca_cert);
    ssl_ctx.set_verify_mode(asio::ssl::verify_peer);
    ssl_ctx.set_options(
        asio::ssl::context::default_workarounds |
        asio::ssl::context::no_sslv2 |
        asio::ssl::context::no_sslv3 |
        asio::ssl::context::no_tlsv1 |
        asio::ssl::context::no_tlsv1_1);

    asio::ssl::stream<asio::ip::tcp::socket> sock(io, ssl_ctx);
    asio::error_code ec;
    asio::ip::tcp::resolver resolver(io);
    auto endpoints = resolver.resolve("127.0.0.1", "19540", ec);
    REQUIRE_FALSE(ec);
    asio::connect(sock.lowest_layer(), endpoints, ec);
    REQUIRE_FALSE(ec);
    sock.handshake(asio::ssl::stream_base::client, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(sock.lowest_layer().is_open());
}

// ── Test 3: mTLS mutual verification ─────────────────────────────────────────

TEST_CASE("E2E: full mTLS mutual verification succeeds", "[e2e]") {
    auto certs = generate_test_certs();
    struct Cleanup { TestCerts& c; ~Cleanup() { c.cleanup(); } } _c{certs};

    WsServerConfig cfg;
    cfg.listen_port = 19541;
    cfg.listen_address = "127.0.0.1";
    cfg.tls_cert_path = certs.server_cert;
    cfg.tls_key_path = certs.server_key;
    cfg.mtls.enabled = true;
    cfg.mtls.ca_cert_path = certs.ca_cert;
    cfg.mtls.require_client_cert = true;

    ServerFixture fix(cfg);
    fix.start_server();

    asio::io_context io;
    asio::ssl::context ssl_ctx(asio::ssl::context::tls_client);

    ssl_ctx.load_verify_file(certs.ca_cert);
    ssl_ctx.set_verify_mode(asio::ssl::verify_peer);
    ssl_ctx.use_certificate_chain_file(certs.client_cert);
    ssl_ctx.use_private_key_file(certs.client_key, asio::ssl::context::pem);
    ssl_ctx.set_options(
        asio::ssl::context::default_workarounds |
        asio::ssl::context::no_sslv2 |
        asio::ssl::context::no_sslv3 |
        asio::ssl::context::no_tlsv1 |
        asio::ssl::context::no_tlsv1_1);

    asio::ssl::stream<asio::ip::tcp::socket> sock(io, ssl_ctx);
    asio::error_code ec;
    asio::ip::tcp::resolver resolver(io);
    auto endpoints = resolver.resolve("127.0.0.1", "19541", ec);
    REQUIRE_FALSE(ec);
    asio::connect(sock.lowest_layer(), endpoints, ec);
    REQUIRE_FALSE(ec);
    sock.handshake(asio::ssl::stream_base::client, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(sock.lowest_layer().is_open());
}

// ── Test 4: Server extracts client cert identity ──────────────────────────────

TEST_CASE("E2E: server extracts client cert identity on mTLS connect", "[e2e]") {
    auto certs = generate_test_certs();
    struct Cleanup { TestCerts& c; ~Cleanup() { c.cleanup(); } } _c{certs};

    std::atomic<bool> cert_received{false};
    ClientCertInfo received;

    WsServerConfig cfg;
    cfg.listen_port = 19542;
    cfg.listen_address = "127.0.0.1";
    cfg.tls_cert_path = certs.server_cert;
    cfg.tls_key_path = certs.server_key;
    cfg.mtls.enabled = true;
    cfg.mtls.ca_cert_path = certs.ca_cert;
    cfg.mtls.require_client_cert = true;

    ServerFixture fix(cfg);
    fix.server.set_on_client_cert([&](const std::string&, const ClientCertInfo& ci) {
        received = ci;
        cert_received = true;
    });
    fix.start_server();

    asio::io_context io;
    asio::ssl::context ssl_ctx(asio::ssl::context::tls_client);
    ssl_ctx.load_verify_file(certs.ca_cert);
    ssl_ctx.set_verify_mode(asio::ssl::verify_peer);
    ssl_ctx.use_certificate_chain_file(certs.client_cert);
    ssl_ctx.use_private_key_file(certs.client_key, asio::ssl::context::pem);
    ssl_ctx.set_options(
        asio::ssl::context::default_workarounds |
        asio::ssl::context::no_sslv2 |
        asio::ssl::context::no_sslv3 |
        asio::ssl::context::no_tlsv1 |
        asio::ssl::context::no_tlsv1_1);

    asio::ssl::stream<asio::ip::tcp::socket> sock(io, ssl_ctx);
    asio::error_code ec;
    asio::ip::tcp::resolver resolver(io);
    auto endpoints = resolver.resolve("127.0.0.1", "19542", ec);
    REQUIRE_FALSE(ec);
    asio::connect(sock.lowest_layer(), endpoints, ec);
    REQUIRE_FALSE(ec);
    sock.handshake(asio::ssl::stream_base::client, ec);
    REQUIRE_FALSE(ec);

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!cert_received.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(50ms);
    }

    REQUIRE(cert_received.load());
    REQUIRE(received.common_name == "odc-hook-test");
    REQUIRE_FALSE(received.fingerprint.empty());
}
