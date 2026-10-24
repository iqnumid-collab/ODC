#include "ws_client.hpp"
#include "odc/logging.hpp"

#include <openssl/rand.h>
#include <openssl/sha.h>

#include <cstring>
#include <random>
#include <sstream>

namespace odc::hook {

namespace {

constexpr size_t kReadBufSize = 65536;

} // namespace

WsClient::WsClient(asio::io_context& io, const WsClientConfig& cfg)
    : config_(cfg), io_ctx_(io) {
}

WsClient::~WsClient() {
    disconnect();
}

bool WsClient::connect() {
    if (connected_ || connecting_) return true;
    do_connect();
    return true;
}

void WsClient::disconnect() {
    auto logger = get_logger("ws_client");
    if (reconnect_timer_) {
        reconnect_timer_->cancel();
    }

    connected_ = false;
    connecting_ = false;

    if (socket_) {
        try {
            socket_->lowest_layer().close();
        } catch (...) {}
        socket_.reset();
    }

    SPDLOG_LOGGER_DEBUG(logger, "Disconnected");
}

bool WsClient::is_connected() const {
    return connected_;
}

bool WsClient::send(const std::string& message) {
    if (!connected_ || !socket_) return false;

    try {
        std::string frame = ws_encode(message, 0x1);
        asio::write(*socket_, asio::buffer(frame));
        return true;
    } catch (const std::exception& e) {
        auto logger = get_logger("ws_client");
        SPDLOG_LOGGER_WARN(logger, "Send failed: {}", e.what());
        return false;
    }
}

bool WsClient::send_json(const nlohmann::json& data) {
    return send(data.dump());
}

// ── Private: connection flow ───────────────────────────────────────────────

void WsClient::do_connect() {
    if (connecting_) return;
    connecting_ = true;

    auto logger = get_logger("ws_client");
    SPDLOG_LOGGER_INFO(logger, "Connecting to {}:{}", config_.server_host, config_.server_port);

    // Cancel any pending reconnect
    if (reconnect_timer_) {
        reconnect_timer_->cancel();
    }

    try {
        ssl_ctx_ = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_client);
        setup_ssl_context(*ssl_ctx_);

        socket_ = std::make_unique<asio::ssl::stream<asio::ip::tcp::socket>>(
            io_ctx_, *ssl_ctx_);

        asio::ip::tcp::resolver resolver(io_ctx_);
        auto endpoints = resolver.resolve(config_.server_host,
                                          std::to_string(config_.server_port));

        asio::async_connect(socket_->lowest_layer(), endpoints,
            [this](const asio::error_code& ec, const asio::ip::tcp::endpoint&) {
                if (!ec) {
                    do_ssl_handshake();
                } else {
                    auto logger = get_logger("ws_client");
                    SPDLOG_LOGGER_ERROR(logger, "TCP connect failed: {}", ec.message());
                    connecting_ = false;
                    on_disconnected(ec.message());
                }
            });

    } catch (const std::exception& e) {
        auto logger = get_logger("ws_client");
        SPDLOG_LOGGER_ERROR(logger, "Connection setup failed: {}", e.what());
        connecting_ = false;
        on_disconnected(e.what());
    }
}

void WsClient::do_ssl_handshake() {
    socket_->async_handshake(asio::ssl::stream_base::client,
        [this](const asio::error_code& ec) {
            if (!ec) {
                auto logger = get_logger("ws_client");
                SPDLOG_LOGGER_DEBUG(logger, "TLS handshake complete");
                do_ws_handshake();
            } else {
                auto logger = get_logger("ws_client");
                SPDLOG_LOGGER_ERROR(logger, "TLS handshake failed: {}", ec.message());
                connecting_ = false;
                disconnect();
                on_disconnected(ec.message());
            }
        });
}

void WsClient::do_ws_handshake() {
    ws_key_ = generate_ws_key();

    std::ostringstream req;
    req << "GET " << config_.server_uri << " HTTP/1.1\r\n";
    req << "Host: " << config_.server_host << ":" << config_.server_port << "\r\n";
    req << "Upgrade: websocket\r\n";
    req << "Connection: Upgrade\r\n";
    req << "Sec-WebSocket-Key: " << ws_key_ << "\r\n";
    req << "Sec-WebSocket-Version: 13\r\n";
    req << "\r\n";

    std::string req_str = req.str();

    try {
        asio::write(*socket_, asio::buffer(req_str));
    } catch (const std::exception& e) {
        auto logger = get_logger("ws_client");
        SPDLOG_LOGGER_ERROR(logger, "WS handshake send failed: {}", e.what());
        connecting_ = false;
        disconnect();
        on_disconnected(e.what());
        return;
    }

    // Read response
    auto buf = std::make_shared<std::vector<char>>(kReadBufSize);
    socket_->async_read_some(asio::buffer(*buf),
        [this, buf](const asio::error_code& ec, size_t bytes_transferred) {
            if (ec) {
                auto logger = get_logger("ws_client");
                SPDLOG_LOGGER_ERROR(logger, "WS handshake read failed: {}", ec.message());
                connecting_ = false;
                disconnect();
                on_disconnected(ec.message());
                return;
            }

            std::string response(buf->data(), bytes_transferred);

            if (response.find("101") == std::string::npos) {
                auto logger = get_logger("ws_client");
                SPDLOG_LOGGER_ERROR(logger, "WS handshake rejected: {}",
                                   response.substr(0, 100));
                connecting_ = false;
                disconnect();
                on_disconnected("Handshake rejected");
                return;
            }

            auto logger = get_logger("ws_client");
            SPDLOG_LOGGER_INFO(logger, "WebSocket connected to {}:{}{}",
                              config_.server_host, config_.server_port,
                              config_.server_uri);

            connected_ = true;
            connecting_ = false;

            if (on_connected_) on_connected_();

            do_read();
        });
}

void WsClient::do_read() {
    if (!connected_ || !socket_) return;

    auto buf = std::make_shared<std::vector<char>>(kReadBufSize);
    socket_->async_read_some(asio::buffer(*buf),
        [this, buf](const asio::error_code& ec, size_t bytes_transferred) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    auto logger = get_logger("ws_client");
                    SPDLOG_LOGGER_DEBUG(logger, "Read error: {}", ec.message());
                    on_disconnected(ec.message());
                }
                return;
            }

            read_buffer_.append(buf->data(), bytes_transferred);

            // Parse complete frames from buffer
            while (true) {
                if (read_buffer_.size() < 2) break;

                const auto* data = reinterpret_cast<const uint8_t*>(read_buffer_.data());
                size_t offset = 2;  // Skip first 2 bytes

                // Mask bit (server→client should not be masked per RFC 6455)
                bool masked = (data[1] & 0x80) != 0;
                uint64_t payload_len = data[1] & 0x7F;

                if (payload_len == 126) {
                    if (read_buffer_.size() < 4) break;
                    payload_len = (static_cast<uint16_t>(data[2]) << 8) | data[3];
                    offset = 4;
                } else if (payload_len == 127) {
                    if (read_buffer_.size() < 10) break;
                    payload_len = 0;
                    for (int i = 0; i < 8; ++i) {
                        payload_len = (payload_len << 8) | data[2 + i];
                    }
                    offset = 10;
                }

                size_t mask_offset = offset;
                if (masked) {
                    offset += 4;
                }

                if (read_buffer_.size() < offset + payload_len) break;

                // Extract payload
                std::string payload(read_buffer_.data() + offset, payload_len);

                // Unmask if needed
                if (masked) {
                    for (size_t i = 0; i < payload_len; ++i) {
                        payload[i] ^= read_buffer_[mask_offset + (i % 4)];
                    }
                }

                uint8_t opcode = data[0] & 0x0F;
                bool fin = (data[0] & 0x80) != 0;

                // Remove processed frame from buffer
                read_buffer_.erase(0, offset + payload_len);

                switch (opcode) {
                case 0x1:  // Text
                case 0x2:  // Binary
                    if (fin && on_message_) {
                        on_message_(payload);
                    }
                    break;
                case 0x8:  // Close
                    {
                        auto logger = get_logger("ws_client");
                        SPDLOG_LOGGER_DEBUG(logger, "Server sent close frame");
                        on_disconnected("Server closed connection");
                        return;
                    }
                case 0x9:  // Ping
                    {
                        std::string pong = ws_encode(payload, 0xA);
                        try {
                            asio::write(*socket_, asio::buffer(pong));
                        } catch (...) {}
                    }
                    break;
                case 0xA:  // Pong
                    break;
                }
            }

            do_read();
        });
}

void WsClient::do_write() {
    // No-op: writes happen synchronously in send()
}

void WsClient::start_reconnect_timer() {
    if (!reconnect_timer_) {
        reconnect_timer_ = std::make_unique<asio::steady_timer>(io_ctx_);
    }

    reconnect_timer_->expires_after(std::chrono::seconds(config_.reconnect_interval_sec));
    reconnect_timer_->async_wait([this](const asio::error_code& ec) {
        if (ec == asio::error::operation_aborted) return;
        auto logger = get_logger("ws_client");
        SPDLOG_LOGGER_INFO(logger, "Reconnecting...");
        do_connect();
    });
}

void WsClient::on_disconnected(const std::string& reason) {
    connected_ = false;
    connecting_ = false;
    read_buffer_.clear();

    if (on_disconnected_) on_disconnected_(reason);

    start_reconnect_timer();
}

// ── SSL context ─────────────────────────────────────────────────────────────

void WsClient::setup_ssl_context(asio::ssl::context& ctx) {
    auto logger = get_logger("ws_client");

    // Load client certificate (mTLS — we present this to the server)
    if (!config_.client_cert_path.empty() && !config_.client_key_path.empty()) {
        ctx.use_certificate_chain_file(config_.client_cert_path);
        ctx.use_private_key_file(config_.client_key_path, asio::ssl::context::pem);
        SPDLOG_LOGGER_DEBUG(logger, "Client cert loaded for mTLS");
    }

    // Verify server certificate
    if (config_.verify_server && !config_.ca_cert_path.empty()) {
        ctx.load_verify_file(config_.ca_cert_path);
        ctx.set_verify_mode(asio::ssl::verify_peer);
        SPDLOG_LOGGER_DEBUG(logger, "Server certificate verification enabled");
    } else if (!config_.verify_server) {
        ctx.set_verify_mode(asio::ssl::verify_none);
    }

    ctx.set_options(
        asio::ssl::context::default_workarounds |
        asio::ssl::context::no_sslv2 |
        asio::ssl::context::no_sslv3 |
        asio::ssl::context::no_tlsv1 |
        asio::ssl::context::no_tlsv1_1);
}

// ── WebSocket helpers ───────────────────────────────────────────────────────

std::string WsClient::generate_ws_key() {
    unsigned char key[16];
    RAND_bytes(key, sizeof(key));
    static const char* b64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(24);
    for (int i = 0; i < 16; i += 3) {
        uint32_t bits = (static_cast<uint32_t>(key[i]) << 16);
        if (i + 1 < 16) bits |= (static_cast<uint32_t>(key[i + 1]) << 8);
        if (i + 2 < 16) bits |= static_cast<uint32_t>(key[i + 2]);
        result += b64[(bits >> 18) & 0x3F];
        result += b64[(bits >> 12) & 0x3F];
        result += b64[(bits >> 6) & 0x3F];
        result += b64[bits & 0x3F];
    }
    return result;
}

std::string WsClient::ws_encode(const std::string& payload, uint8_t opcode) {
    std::string frame;
    frame.push_back(static_cast<char>(0x80 | (opcode & 0x0F)));

    // Client frames MUST be masked per RFC 6455
    unsigned char mask_key[4];
    RAND_bytes(mask_key, sizeof(mask_key));

    if (payload.size() <= 125) {
        frame.push_back(static_cast<char>(0x80 | payload.size()));
    } else if (payload.size() <= 65535) {
        frame.push_back(static_cast<char>(0x80 | 126));
        frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFF));
        frame.push_back(static_cast<char>(payload.size() & 0xFF));
    } else {
        frame.push_back(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((payload.size() >> (i * 8)) & 0xFF));
        }
    }

    frame.append(reinterpret_cast<const char*>(mask_key), 4);

    for (size_t i = 0; i < payload.size(); ++i) {
        frame.push_back(payload[i] ^ mask_key[i % 4]);
    }

    return frame;
}

bool WsClient::ws_parse_frame(const char* data, size_t len,
                               std::string& payload, uint8_t& opcode, bool& fin) {
    (void)data;
    (void)len;
    (void)payload;
    (void)opcode;
    (void)fin;
    return false;
}

} // namespace odc::hook
