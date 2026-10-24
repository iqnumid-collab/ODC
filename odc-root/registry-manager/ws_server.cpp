#include "ws_server.hpp"
#include "odc/logging.hpp"

#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>
#include <asio/read_until.hpp>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/sha.h>
#include <openssl/bn.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace odc {

namespace {

std::string ws_accept_key(const std::string& client_key) {
    static const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = client_key + magic;

    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(combined.data()),
         combined.size(), hash);

    static const char* b64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(28);

    for (int i = 0; i < static_cast<int>(sizeof(hash)); i += 3) {
        uint32_t bits = (static_cast<uint32_t>(hash[i]) << 16);
        if (i + 1 < static_cast<int>(sizeof(hash)))
            bits |= (static_cast<uint32_t>(hash[i + 1]) << 8);
        if (i + 2 < static_cast<int>(sizeof(hash)))
            bits |= static_cast<uint32_t>(hash[i + 2]);

        result += b64[(bits >> 18) & 0x3F];
        result += b64[(bits >> 12) & 0x3F];
        result += (i + 1 < static_cast<int>(sizeof(hash)))
                      ? b64[(bits >> 6) & 0x3F] : '=';
        result += (i + 2 < static_cast<int>(sizeof(hash)))
                      ? b64[bits & 0x3F] : '=';
    }
    return result;
}

// Case-insensitive header lookup in an HTTP request string
std::string http_header(const std::string& request, const std::string& name) {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

    // Search for the header name followed by ":"
    std::string search = "\r\n" + lower_name + ":";
    std::string req_lower = request;
    std::transform(req_lower.begin(), req_lower.end(), req_lower.begin(), ::tolower);

    size_t pos = req_lower.find(search);
    if (pos == std::string::npos) {
        // Try start-of-request (first header line may not have preceding \r\n)
        if (req_lower.find(lower_name + ":") == 0) {
            pos = 0;
        } else {
            return "";
        }
    } else {
        pos += 1; // skip the \r\n we matched
    }

    size_t colon = req_lower.find(':', pos);
    if (colon == std::string::npos) return "";

    size_t val_start = colon + 1;
    while (val_start < request.size() && request[val_start] == ' ') val_start++;

    size_t val_end = request.find('\r', val_start);
    if (val_end == std::string::npos) return "";

    return request.substr(val_start, val_end - val_start);
}

// Parse the first line of an HTTP request to extract method, path
bool parse_http_request_line(const std::string& request,
                              std::string& method, std::string& path) {
    size_t sp1 = request.find(' ');
    if (sp1 == std::string::npos) return false;
    method = request.substr(0, sp1);

    size_t sp2 = request.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return false;
    path = request.substr(sp1 + 1, sp2 - sp1 - 1);
    return true;
}

} // namespace

// ── WsServer ────────────────────────────────────────────────────────────────

WsServer::WsServer(const WsServerConfig& config)
    : config_(config) {
}

WsServer::~WsServer() {
    stop();
}

bool WsServer::start() {
    if (running_) return true;

    auto logger = get_logger("ws_server");

    running_ = true;

    try {
        setup_ssl_context();

        asio::ip::tcp::endpoint ep(
            asio::ip::make_address(config_.listen_address),
            config_.listen_port);

        acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(io_ctx_, ep);
        acceptor_->set_option(asio::ip::tcp::acceptor::reuse_address(true));

        do_accept();

        SPDLOG_LOGGER_INFO(logger, "WebSocket server listening on {}:{} (mTLS: {})",
                           config_.listen_address, config_.listen_port,
                           config_.mtls.enabled ? "enabled" : "disabled");
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to start WebSocket server: {}", e.what());
        running_ = false;
        return false;
    }

    return true;
}

void WsServer::stop() {
    if (!running_) return;

    auto logger = get_logger("ws_server");
    running_ = false;

    try {
        if (acceptor_) {
            acceptor_->close();
        }
        io_ctx_.stop();
    } catch (...) {}

    {
        std::lock_guard lock(conn_mutex_);
        connections_.clear();
        connection_certs_.clear();
        read_states_.clear();
    }
    {
        std::lock_guard lock(subs_mutex_);
        subscribers_.clear();
    }

    SPDLOG_LOGGER_INFO(logger, "WebSocket server stopped");
}

void WsServer::run() {
    if (!running_) return;

    auto logger = get_logger("ws_server");
    SPDLOG_LOGGER_INFO(logger, "WebSocket server running");

    try {
        io_ctx_.run();
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger, "WebSocket server error: {}", e.what());
    }
}

// ── Pub/sub topic management ───────────────────────────────────────────────

void WsServer::subscribe(const std::string& conn_id, const std::string& topic) {
    std::lock_guard lock(subs_mutex_);
    subscribers_[topic].insert(conn_id);
}

void WsServer::unsubscribe(const std::string& conn_id, const std::string& topic) {
    std::lock_guard lock(subs_mutex_);
    auto it = subscribers_.find(topic);
    if (it != subscribers_.end()) {
        it->second.erase(conn_id);
        if (it->second.empty())
            subscribers_.erase(it);
    }
}

void WsServer::unsubscribe_all(const std::string& conn_id) {
    std::lock_guard lock(subs_mutex_);
    for (auto it = subscribers_.begin(); it != subscribers_.end(); ) {
        it->second.erase(conn_id);
        if (it->second.empty())
            it = subscribers_.erase(it);
        else
            ++it;
    }
}

// ── Send ────────────────────────────────────────────────────────────────────

bool WsServer::send(const std::string& connection_id, const std::string& message) {
    SocketPtr sock;
    {
        std::lock_guard lock(conn_mutex_);
        auto it = connections_.find(connection_id);
        if (it == connections_.end()) return false;
        sock = it->second;
    }

    if (!sock) return false;

    try {
        std::string frame = ws_frame(message, 0x1);
        asio::write(*sock, asio::buffer(frame));
        return true;
    } catch (const std::exception& e) {
        auto logger = get_logger("ws_server");
        SPDLOG_LOGGER_WARN(logger, "Send to {} failed: {}", connection_id, e.what());
        return false;
    }
}

bool WsServer::send_json(const std::string& connection_id, const nlohmann::json& data) {
    return send(connection_id, data.dump());
}

void WsServer::broadcast(const std::string& message) {
    std::vector<SocketPtr> sockets;
    {
        std::lock_guard lock(conn_mutex_);
        sockets.reserve(connections_.size());
        for (const auto& [id, sock] : connections_) {
            sockets.push_back(sock);
        }
    }

    auto logger = get_logger("ws_server");
    std::string frame = ws_frame(message, 0x1);

    for (const auto& sock : sockets) {
        try {
            asio::write(*sock, asio::buffer(frame));
        } catch (const std::exception& e) {
            SPDLOG_LOGGER_DEBUG(logger, "Broadcast write failed: {}", e.what());
        }
    }
}

void WsServer::broadcast_json(const nlohmann::json& data) {
    broadcast(data.dump());
}

void WsServer::publish(const std::string& topic, const nlohmann::json& data) {
    std::lock_guard lock(subs_mutex_);
    auto it = subscribers_.find(topic);
    if (it == subscribers_.end()) return;

    for (const auto& conn_id : it->second) {
        send_json(conn_id, {
            {"type", "event"},
            {"topic", topic},
            {"data", data}
        });
    }
}

size_t WsServer::connection_count() const {
    std::lock_guard lock(conn_mutex_);
    return connections_.size();
}

std::optional<ClientCertInfo> WsServer::get_client_cert(
    const std::string& connection_id) const {
    std::lock_guard lock(conn_mutex_);
    auto it = connection_certs_.find(connection_id);
    if (it != connection_certs_.end()) {
        return it->second;
    }
    return std::nullopt;
}

// ── SSL context (hardened for mTLS) ─────────────────────────────────────────

void WsServer::setup_ssl_context() {
    auto logger = get_logger("ws_server");

    ssl_ctx_ = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_server);

    if (!config_.tls_cert_path.empty() && !config_.tls_key_path.empty()) {
        ssl_ctx_->use_certificate_chain_file(config_.tls_cert_path);
        ssl_ctx_->use_private_key_file(config_.tls_key_path, asio::ssl::context::pem);
    } else {
        SPDLOG_LOGGER_WARN(logger, "No TLS cert/key configured");
    }

    ssl_ctx_->set_options(
        asio::ssl::context::default_workarounds |
        asio::ssl::context::no_sslv2 |
        asio::ssl::context::no_sslv3 |
        asio::ssl::context::no_tlsv1 |
        asio::ssl::context::no_tlsv1_1 |
        asio::ssl::context::single_dh_use);

    SSL_CTX* native = ssl_ctx_->native_handle();
    SSL_CTX_set_min_proto_version(native, TLS1_2_VERSION);
    SSL_CTX_set_options(native, SSL_OP_NO_RENEGOTIATION);
    SSL_CTX_set_options(native, SSL_OP_NO_TICKET);

    if (!config_.mtls.cipher_list.empty()) {
        SSL_CTX_set_cipher_list(native, config_.mtls.cipher_list.c_str());
    } else {
        SSL_CTX_set_cipher_list(native, "ECDHE+AESGCM:!aNULL:!MD5:!RC4:!3DES");
    }

    if (config_.mtls.enabled) {
        if (!config_.mtls.ca_cert_path.empty()) {
            ssl_ctx_->load_verify_file(config_.mtls.ca_cert_path);
            SPDLOG_LOGGER_INFO(logger, "mTLS CA cert loaded: {}", config_.mtls.ca_cert_path);
        }

        int verify_mode = asio::ssl::verify_peer;
        if (config_.mtls.require_client_cert) {
            verify_mode |= asio::ssl::verify_fail_if_no_peer_cert;
        }

        ssl_ctx_->set_verify_mode(verify_mode);
        SSL_CTX_set_verify_depth(native, config_.mtls.verify_depth);

        if (!config_.mtls.crl_path.empty()) {
            crl_ = std::make_unique<CertRevocationList>();
            if (crl_->load_crl(config_.mtls.crl_path)) {
                SPDLOG_LOGGER_INFO(logger, "CRL loaded: {} — {} certificates revoked",
                                   config_.mtls.crl_path, crl_->denied_count());
            } else {
                SPDLOG_LOGGER_WARN(logger, "Failed to load CRL: {}", config_.mtls.crl_path);
                crl_.reset();
            }
        }

        SPDLOG_LOGGER_INFO(logger, "mTLS enabled: verify_depth={}, require_client_cert={}",
                           config_.mtls.verify_depth, config_.mtls.require_client_cert);
    } else {
        if (config_.verify_client_cert && !config_.ca_cert_path.empty()) {
            ssl_ctx_->load_verify_file(config_.ca_cert_path);
            ssl_ctx_->set_verify_mode(
                asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert);
        }
    }
}

// ── Accept loop ─────────────────────────────────────────────────────────────

void WsServer::do_accept() {
    if (!running_) return;

    auto socket = std::make_shared<asio::ssl::stream<asio::ip::tcp::socket>>(
        io_ctx_, *ssl_ctx_);

    acceptor_->async_accept(socket->lowest_layer(),
        [this, socket](const asio::error_code& ec) {
            handle_accept(socket, ec);
        });
}

void WsServer::handle_accept(SocketPtr socket, const asio::error_code& ec) {
    auto logger = get_logger("ws_server");

    if (!ec) {
        socket->async_handshake(asio::ssl::stream_base::server,
            [this, socket](const asio::error_code& hsec) {
                auto logger = get_logger("ws_server");
                if (!hsec) {
                    std::string conn_id;
                    {
                        std::lock_guard lock(id_mutex_);
                        conn_id = "ws-" + std::to_string(next_conn_id_++);
                    }

                    if (config_.mtls.enabled) {
                        SSL* ssl = socket->native_handle();
                        if (!extract_client_cert(ssl, conn_id)) {
                            SPDLOG_LOGGER_WARN(logger,
                                "Client {} failed mTLS cert validation — closing", conn_id);
                            return;
                        }
                    }

                    // Check max connections
                    {
                        std::lock_guard lock(conn_mutex_);
                        if (static_cast<int>(connections_.size()) >= config_.max_connections) {
                            SPDLOG_LOGGER_WARN(logger,
                                "Max connections ({}) exceeded — rejecting {}",
                                config_.max_connections, conn_id);
                            return;
                        }
                        connections_[conn_id] = socket;
                    }

                    // Create read state for this connection
                    auto state = std::make_shared<ConnReadState>();
                    state->socket = socket;
                    {
                        std::lock_guard lock(conn_mutex_);
                        read_states_[conn_id] = state;
                    }

                    SPDLOG_LOGGER_DEBUG(logger, "Client connected via TLS: {}", conn_id);

                    if (on_connect_) on_connect_(conn_id);

                    if (on_client_cert_) {
                        auto cert_info = get_client_cert(conn_id);
                        if (cert_info) {
                            on_client_cert_(conn_id, *cert_info);
                        }
                    }

                    // Start HTTP upgrade handshake
                    start_http_upgrade(conn_id);
                } else {
                    SPDLOG_LOGGER_DEBUG(logger, "TLS handshake failed: {}",
                                        hsec.message());
                }
            });
    }

    do_accept();
}

// ── HTTP upgrade handshake ──────────────────────────────────────────────────

void WsServer::start_http_upgrade(const std::string& conn_id) {
    std::shared_ptr<ConnReadState> state;
    {
        std::lock_guard lock(conn_mutex_);
        auto it = read_states_.find(conn_id);
        if (it == read_states_.end()) return;
        state = it->second;
    }

    auto& sock = state->socket;
    auto buf = asio::buffer(state->read_buf.data(), state->read_buf.size());

    sock->async_read_some(buf,
        [this, conn_id](const asio::error_code& ec, size_t bytes) {
            handle_upgrade_read(conn_id, ec, bytes);
        });
}

void WsServer::handle_upgrade_read(const std::string& conn_id,
                                    const asio::error_code& ec, size_t bytes) {
    auto logger = get_logger("ws_server");

    if (ec) {
        if (ec != asio::error::operation_aborted) {
            SPDLOG_LOGGER_DEBUG(logger, "HTTP upgrade read error for {}: {}",
                                conn_id, ec.message());
        }
        cleanup_connection(conn_id);
        return;
    }

    std::shared_ptr<ConnReadState> state;
    {
        std::lock_guard lock(conn_mutex_);
        auto it = read_states_.find(conn_id);
        if (it == read_states_.end()) return;
        state = it->second;
    }

    // Append received data to parse_buf
    state->parse_buf.insert(state->parse_buf.end(),
                            state->read_buf.begin(),
                            state->read_buf.begin() + bytes);

    // Try to parse HTTP upgrade
    if (try_http_upgrade(conn_id)) {
        return; // Upgrade completed, read loop started
    }

    // Need more data — keep reading
    // Cap total buffered data for the upgrade header at 16KB
    if (state->parse_buf.size() > 16384) {
        SPDLOG_LOGGER_WARN(logger, "HTTP upgrade header too large for {} ({})",
                           conn_id, state->parse_buf.size());
        send_http_error(conn_id, 431, "Request Header Fields Too Large");
        cleanup_connection(conn_id);
        return;
    }

    start_http_upgrade(conn_id);
}

bool WsServer::try_http_upgrade(const std::string& conn_id) {
    auto logger = get_logger("ws_server");

    std::shared_ptr<ConnReadState> state;
    {
        std::lock_guard lock(conn_mutex_);
        auto it = read_states_.find(conn_id);
        if (it == read_states_.end()) return false;
        state = it->second;
    }

    // Check if we have the full HTTP header (ends with \r\n\r\n)
    std::string data(state->parse_buf.begin(), state->parse_buf.end());
    size_t header_end = data.find("\r\n\r\n");
    if (header_end == std::string::npos) return false;

    std::string header(data.begin(), data.begin() + header_end + 4);

    // Parse request line
    std::string method, path;
    if (!parse_http_request_line(header, method, path)) {
        SPDLOG_LOGGER_WARN(logger, "Malformed HTTP request from {}", conn_id);
        send_http_error(conn_id, 400, "Bad Request");
        cleanup_connection(conn_id);
        return true;
    }

    if (method != "GET") {
        send_http_error(conn_id, 405, "Method Not Allowed");
        cleanup_connection(conn_id);
        return true;
    }

    // Validate WebSocket upgrade headers
    std::string upgrade = http_header(header, "Upgrade");
    std::string connection = http_header(header, "Connection");
    std::string ws_key = http_header(header, "Sec-WebSocket-Key");
    std::string ws_version = http_header(header, "Sec-WebSocket-Version");

    // Case-insensitive checks
    std::string upgrade_lower = upgrade;
    std::transform(upgrade_lower.begin(), upgrade_lower.end(),
                   upgrade_lower.begin(), ::tolower);
    std::string conn_lower = connection;
    std::transform(conn_lower.begin(), conn_lower.end(),
                   conn_lower.begin(), ::tolower);

    if (upgrade_lower != "websocket") {
        SPDLOG_LOGGER_WARN(logger, "Client {}: invalid Upgrade header: '{}'",
                           conn_id, upgrade);
        send_http_error(conn_id, 400, "Invalid Upgrade header");
        cleanup_connection(conn_id);
        return true;
    }

    if (conn_lower.find("upgrade") == std::string::npos) {
        SPDLOG_LOGGER_WARN(logger, "Client {}: missing 'Upgrade' in Connection header",
                           conn_id);
        send_http_error(conn_id, 400, "Invalid Connection header");
        cleanup_connection(conn_id);
        return true;
    }

    if (ws_version != "13") {
        SPDLOG_LOGGER_WARN(logger, "Client {}: unsupported WS version: {}",
                           conn_id, ws_version);
        std::string resp = "HTTP/1.1 426 Upgrade Required\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Version: 13\r\n"
                           "\r\n";
        send(conn_id, resp);
        // send() won't work for raw HTTP — write directly
        try {
            asio::write(*state->socket, asio::buffer(resp));
        } catch (...) {}
        cleanup_connection(conn_id);
        return true;
    }

    if (ws_key.empty()) {
        SPDLOG_LOGGER_WARN(logger, "Client {}: missing Sec-WebSocket-Key", conn_id);
        send_http_error(conn_id, 400, "Missing Sec-WebSocket-Key");
        cleanup_connection(conn_id);
        return true;
    }

    // Validate URL path
    if (!config_.url_path.empty() && path != config_.url_path) {
        SPDLOG_LOGGER_DEBUG(logger, "Client {}: path mismatch (got '{}', expected '{}')",
                            conn_id, path, config_.url_path);
        send_http_error(conn_id, 404, "Not Found");
        cleanup_connection(conn_id);
        return true;
    }

    // Send HTTP 101 Switching Protocols
    std::string accept_key = ws_accept_key(ws_key);
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept_key + "\r\n"
        "\r\n";

    try {
        asio::write(*state->socket, asio::buffer(response));
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_DEBUG(logger, "Upgrade response write failed for {}: {}",
                            conn_id, e.what());
        cleanup_connection(conn_id);
        return true;
    }

    SPDLOG_LOGGER_INFO(logger, "WebSocket upgrade completed for {}", conn_id);

    // Any leftover bytes after the HTTP header are part of the first
    // WebSocket frame — preserve them in parse_buf
    if (header_end + 4 < data.size()) {
        state->parse_buf = std::vector<uint8_t>(
            data.begin() + header_end + 4, data.end());
    } else {
        state->parse_buf.clear();
    }

    state->upgrade_done = true;

    // Start WebSocket read loop
    start_read_loop(conn_id);
    return true;
}

void WsServer::send_http_error(const std::string& conn_id, int status,
                                const std::string& message) {
    std::shared_ptr<ConnReadState> state;
    {
        std::lock_guard lock(conn_mutex_);
        auto it = read_states_.find(conn_id);
        if (it == read_states_.end()) return;
        state = it->second;
    }

    std::string status_text;
    switch (status) {
        case 400: status_text = "Bad Request"; break;
        case 404: status_text = "Not Found"; break;
        case 405: status_text = "Method Not Allowed"; break;
        case 426: status_text = "Upgrade Required"; break;
        case 431: status_text = "Request Header Fields Too Large"; break;
        default:  status_text = "Error"; break;
    }

    std::string response =
        "HTTP/1.1 " + std::to_string(status) + " " + status_text + "\r\n"
        "Content-Type: text/plain\r\n"
        "Connection: close\r\n"
        "\r\n" + message + "\n";

    try {
        asio::write(*state->socket, asio::buffer(response));
    } catch (...) {}
}

// ── WebSocket read loop ────────────────────────────────────────────────────

void WsServer::start_read_loop(const std::string& conn_id) {
    std::shared_ptr<ConnReadState> state;
    {
        std::lock_guard lock(conn_mutex_);
        auto it = read_states_.find(conn_id);
        if (it == read_states_.end()) return;
        state = it->second;
    }

    if (state->disconnected) return;

    auto& sock = state->socket;
    auto buf = asio::buffer(state->read_buf.data(), state->read_buf.size());

    sock->async_read_some(buf,
        [this, conn_id](const asio::error_code& ec, size_t bytes) {
            handle_read(conn_id, ec, bytes);
        });
}

void WsServer::handle_read(const std::string& conn_id,
                            const asio::error_code& ec, size_t bytes) {
    if (ec) {
        if (ec != asio::error::operation_aborted) {
            auto logger = get_logger("ws_server");
            SPDLOG_LOGGER_DEBUG(logger, "WebSocket read error for {}: {}",
                                conn_id, ec.message());
        }
        cleanup_connection(conn_id);
        return;
    }

    std::shared_ptr<ConnReadState> state;
    {
        std::lock_guard lock(conn_mutex_);
        auto it = read_states_.find(conn_id);
        if (it == read_states_.end()) return;
        state = it->second;
    }

    if (state->disconnected) return;

    // Append received data to parse buffer
    state->parse_buf.insert(state->parse_buf.end(),
                            state->read_buf.begin(),
                            state->read_buf.begin() + bytes);

    // Parse available frames
    parse_websocket_frames(conn_id);

    // Continue reading if still connected
    if (!state->disconnected) {
        start_read_loop(conn_id);
    }
}

void WsServer::parse_websocket_frames(const std::string& conn_id) {
    auto logger = get_logger("ws_server");

    std::shared_ptr<ConnReadState> state;
    {
        std::lock_guard lock(conn_mutex_);
        auto it = read_states_.find(conn_id);
        if (it == read_states_.end()) return;
        state = it->second;
    }

    while (!state->disconnected) {
        // Need at least 2 bytes for the frame header
        if (state->parse_buf.size() < 2) return;

        const auto& buf = state->parse_buf;
        size_t offset = 0;

        uint8_t byte0 = buf[offset++];
        uint8_t byte1 = buf[offset++];

        bool fin = (byte0 & 0x80) != 0;
        // bool rsv1 = (byte0 & 0x40) != 0;
        // bool rsv2 = (byte0 & 0x20) != 0;
        // bool rsv3 = (byte0 & 0x10) != 0;
        uint8_t opcode = byte0 & 0x0F;

        bool masked = (byte1 & 0x80) != 0;
        uint64_t payload_len = byte1 & 0x7F;

        // Client frames MUST be masked (RFC 6455 §5.1)
        if (!masked) {
            SPDLOG_LOGGER_WARN(logger, "Client {} sent unmasked frame — closing", conn_id);
            std::vector<uint8_t> close_payload = {0x03, 0xEA}; // 1002 Protocol Error
            handle_close(conn_id, close_payload);
            return;
        }

        // Extended payload length
        if (payload_len == 126) {
            if (buf.size() < offset + 2) return;
            payload_len = (static_cast<uint64_t>(buf[offset]) << 8) | buf[offset + 1];
            offset += 2;
        } else if (payload_len == 127) {
            if (buf.size() < offset + 8) return;
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | buf[offset + i];
            }
            offset += 8;
        }

        // Mask key (4 bytes)
        if (buf.size() < offset + 4) return;
        uint8_t mask_key[4];
        std::memcpy(mask_key, buf.data() + offset, 4);
        offset += 4;

        // Need full payload
        if (buf.size() < offset + payload_len) return;

        // Extract and unmask payload
        std::vector<uint8_t> payload;
        payload.reserve(payload_len);
        for (size_t i = 0; i < payload_len; ++i) {
            payload.push_back(buf[offset + i] ^ mask_key[i % 4]);
        }
        offset += payload_len;

        // Remove consumed bytes from parse_buf
        state->parse_buf.erase(state->parse_buf.begin(),
                               state->parse_buf.begin() + offset);

        // ── Control frames ─────────────────────────────────────────────

        bool is_control = (opcode & 0x08) != 0; // opcodes 8-F are control

        if (is_control) {
            // Control frames must not be fragmented and payload <= 125
            if (!fin || payload_len > 125) {
                SPDLOG_LOGGER_WARN(logger, "Invalid control frame from {} — closing", conn_id);
                std::vector<uint8_t> close_payload = {0x03, 0xEA};
                handle_close(conn_id, close_payload);
                return;
            }

            switch (opcode) {
                case 0x8: // Close
                    handle_close(conn_id, payload);
                    return;
                case 0x9: // Ping
                    handle_ping(conn_id, payload);
                    break;
                case 0xA: // Pong
                    // Client responded to our ping — acknowledge silently
                    SPDLOG_LOGGER_DEBUG(logger, "Pong received from {}", conn_id);
                    break;
                default:
                    SPDLOG_LOGGER_WARN(logger, "Unknown control opcode {} from {}",
                                       static_cast<int>(opcode), conn_id);
                    break;
            }
            continue; // Process next frame
        }

        // ── Data frames ────────────────────────────────────────────────

        if (opcode == 0x0) {
            // Continuation frame
            if (state->frag_opcode == 0) {
                SPDLOG_LOGGER_WARN(logger, "Unexpected continuation from {} — closing", conn_id);
                std::vector<uint8_t> close_payload = {0x03, 0xEA};
                handle_close(conn_id, close_payload);
                return;
            }
            state->frame_payload.insert(state->frame_payload.end(),
                                        payload.begin(), payload.end());
            if (fin) {
                std::string msg(state->frame_payload.begin(),
                                state->frame_payload.end());
                state->frame_payload.clear();
                uint8_t saved_opcode = state->frag_opcode;
                state->frag_opcode = 0;

                if (saved_opcode == 0x1) {
                    dispatch_message(conn_id, msg);
                } else {
                    SPDLOG_LOGGER_DEBUG(logger, "Binary frame completed for {} ({} bytes) — ignoring",
                                        conn_id, msg.size());
                }
            }
        } else {
            // New data frame
            if (state->frag_opcode != 0) {
                SPDLOG_LOGGER_WARN(logger, "New data frame while fragmentation in progress from {} — closing", conn_id);
                std::vector<uint8_t> close_payload = {0x03, 0xEA};
                handle_close(conn_id, close_payload);
                return;
            }

            if (fin) {
                // Complete message — dispatch immediately
                std::string msg(payload.begin(), payload.end());
                if (opcode == 0x1) {
                    dispatch_message(conn_id, msg);
                } else if (opcode == 0x2) {
                    SPDLOG_LOGGER_DEBUG(logger, "Binary frame from {} ({} bytes) — ignoring",
                                        conn_id, payload.size());
                }
            } else {
                // Start fragmentation
                state->frag_opcode = opcode;
                state->frame_payload = std::move(payload);
            }
        }
    }
}

void WsServer::dispatch_message(const std::string& conn_id, const std::string& payload) {
    auto logger = get_logger("ws_server");

    try {
        nlohmann::json data = nlohmann::json::parse(payload);

        std::string type = data.value("type", "");

        // Intercept subscribe/unsubscribe at the WsServer layer
        if (type == "subscribe") {
            std::string topic = data.value("topic", "");
            if (!topic.empty()) {
                subscribe(conn_id, topic);
                send_json(conn_id, {
                    {"type", "subscribed"},
                    {"topic", topic}
                });
                SPDLOG_LOGGER_DEBUG(logger, "{} subscribed to topic '{}'", conn_id, topic);
            }
            return;
        }

        if (type == "unsubscribe") {
            std::string topic = data.value("topic", "");
            if (!topic.empty()) {
                unsubscribe(conn_id, topic);
                send_json(conn_id, {
                    {"type", "unsubscribed"},
                    {"topic", topic}
                });
                SPDLOG_LOGGER_DEBUG(logger, "{} unsubscribed from topic '{}'", conn_id, topic);
            }
            return;
        }

        // All other message types → forward to application callback
        if (on_message_) {
            on_message_(conn_id, data);
        }
    } catch (const nlohmann::json::parse_error& e) {
        SPDLOG_LOGGER_WARN(logger, "Invalid JSON from {}: {} — closing with 1003",
                           conn_id, e.what());
        std::vector<uint8_t> close_payload = {0x03, 0xF3}; // 1007 Invalid data
        handle_close(conn_id, close_payload);
    }
}

void WsServer::handle_ping(const std::string& conn_id,
                            const std::vector<uint8_t>& payload) {
    std::string pong_frame;
    pong_frame.push_back(static_cast<char>(0x8A)); // FIN + Pong opcode
    pong_frame.push_back(static_cast<char>(payload.size())); // No mask (server→client)
    pong_frame.append(reinterpret_cast<const char*>(payload.data()), payload.size());

    std::shared_ptr<ConnReadState> state;
    {
        std::lock_guard lock(conn_mutex_);
        auto it = read_states_.find(conn_id);
        if (it == read_states_.end()) return;
        state = it->second;
    }

    try {
        asio::write(*state->socket, asio::buffer(pong_frame));
    } catch (const std::exception& e) {
        auto logger = get_logger("ws_server");
        SPDLOG_LOGGER_DEBUG(logger, "Failed to send pong to {}: {}", conn_id, e.what());
    }
}

void WsServer::handle_close(const std::string& conn_id,
                             const std::vector<uint8_t>& payload) {
    auto logger = get_logger("ws_server");

    uint16_t close_code = 1005; // No status received
    std::string reason;
    if (payload.size() >= 2) {
        close_code = (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
    }
    if (payload.size() > 2) {
        reason = std::string(reinterpret_cast<const char*>(payload.data() + 2),
                             payload.size() - 2);
    }

    SPDLOG_LOGGER_INFO(logger, "Close frame from {}: code={} reason='{}'",
                       conn_id, close_code, reason);

    // Echo close frame back
    std::shared_ptr<ConnReadState> state;
    {
        std::lock_guard lock(conn_mutex_);
        auto it = read_states_.find(conn_id);
        if (it == read_states_.end()) return;
        state = it->second;
    }

    if (!state->disconnected) {
        std::string close_frame;
        close_frame.push_back(static_cast<char>(0x88)); // FIN + Close
        size_t pl = std::min(payload.size(), size_t(125));
        close_frame.push_back(static_cast<char>(pl));
        if (pl > 0) {
            close_frame.append(reinterpret_cast<const char*>(payload.data()), pl);
        }
        try {
            asio::write(*state->socket, asio::buffer(close_frame));
        } catch (...) {}
    }

    cleanup_connection(conn_id);
}

// ── Connection cleanup ─────────────────────────────────────────────────────

void WsServer::cleanup_connection(const std::string& conn_id) {
    auto logger = get_logger("ws_server");

    // Mark as disconnected to prevent re-entry
    {
        std::lock_guard lock(conn_mutex_);
        auto it = read_states_.find(conn_id);
        if (it != read_states_.end() && it->second->disconnected) {
            return; // Already cleaned up
        }
    }

    // Close the socket
    {
        std::lock_guard lock(conn_mutex_);
        auto it = connections_.find(conn_id);
        if (it != connections_.end()) {
            SocketPtr sock = it->second;
            try {
                sock->lowest_layer().close();
            } catch (...) {}
        }
    }

    // Unsubscribe from all topics
    unsubscribe_all(conn_id);

    // Fire disconnect callback
    DisconnectCallback cb;
    {
        std::lock_guard lock(conn_mutex_);
        cb = on_disconnect_;
    }
    if (cb) {
        try {
            cb(conn_id);
        } catch (const std::exception& e) {
            SPDLOG_LOGGER_ERROR(logger, "on_disconnect_ callback threw: {}", e.what());
        }
    }

    // Remove from all tracking maps
    {
        std::lock_guard lock(conn_mutex_);
        auto rit = read_states_.find(conn_id);
        if (rit != read_states_.end()) {
            rit->second->disconnected = true;
            read_states_.erase(rit);
        }
        connections_.erase(conn_id);
        connection_certs_.erase(conn_id);
    }

    SPDLOG_LOGGER_DEBUG(logger, "Connection {} cleaned up", conn_id);
}

// ── Client certificate extraction ───────────────────────────────────────────

bool WsServer::extract_client_cert(SSL* ssl, const std::string& conn_id) {
    auto logger = get_logger("ws_server");

    X509* client_cert = SSL_get_peer_certificate(ssl);
    if (!client_cert) {
        if (config_.mtls.require_client_cert) {
            SPDLOG_LOGGER_WARN(logger, "Client {} did not present a certificate", conn_id);
            return false;
        }
        return true;
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (!X509_digest(client_cert, EVP_sha256(), digest, &digest_len)) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to compute client cert fingerprint");
        X509_free(client_cert);
        return false;
    }

    ClientCertInfo info;
    info.fingerprint = format_fingerprint(digest, digest_len);

    if (crl_ && crl_->is_denied(info.fingerprint)) {
        SPDLOG_LOGGER_WARN(logger, "Client {} certificate revoked — closing", conn_id);
        X509_free(client_cert);
        return false;
    }

    X509_NAME* subject = X509_get_subject_name(client_cert);
    char cn_buf[256] = {};
    X509_NAME_get_text_by_NID(subject, NID_commonName, cn_buf, sizeof(cn_buf));
    info.common_name = std::string(cn_buf);

    const ASN1_TIME* not_before = X509_get_notBefore(client_cert);
    const ASN1_TIME* not_after = X509_get_notAfter(client_cert);

    int day, sec;
    if (not_before && ASN1_TIME_diff(&day, &sec, nullptr, not_before)) {
        info.not_before = static_cast<int64_t>(std::time(nullptr)) + sec + day * 86400L;
    }
    if (not_after && ASN1_TIME_diff(&day, &sec, nullptr, not_after)) {
        info.not_after = static_cast<int64_t>(std::time(nullptr)) + sec + day * 86400L;
    }

    const ASN1_INTEGER* serial = X509_get_serialNumber(client_cert);
    if (serial) {
        BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
        if (bn) {
            char* hex = BN_bn2hex(bn);
            if (hex) {
                info.serial = std::string(hex);
                OPENSSL_free(hex);
            }
            BN_free(bn);
        }
    }

    X509_free(client_cert);

    {
        std::lock_guard lock(conn_mutex_);
        connection_certs_[conn_id] = info;
    }

    SPDLOG_LOGGER_INFO(logger, "Client {} authenticated: CN={} fingerprint={}",
                       conn_id, info.common_name, info.fingerprint.substr(0, 23) + "...");
    return true;
}

// ── Static helpers ──────────────────────────────────────────────────────────

std::string WsServer::format_fingerprint(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        if (i > 0) oss << ':';
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(data[i]);
    }
    return oss.str();
}

std::string WsServer::ws_frame(const std::string& payload, uint8_t opcode) {
    std::string frame;
    frame.push_back(static_cast<char>(0x80 | (opcode & 0x0F)));

    if (payload.size() <= 125) {
        frame.push_back(static_cast<char>(payload.size()));
    } else if (payload.size() <= 65535) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFF));
        frame.push_back(static_cast<char>(payload.size() & 0xFF));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((payload.size() >> (i * 8)) & 0xFF));
        }
    }

    frame.append(payload);
    return frame;
}

void WsServer::check_crl(const std::string& fingerprint, const std::string& conn_id) {
    auto logger = get_logger("ws_server");
    if (crl_ && crl_->is_denied(fingerprint)) {
        SPDLOG_LOGGER_WARN(logger, "Active connection {} has a revoked certificate", conn_id);
    }
}

} // namespace odc
