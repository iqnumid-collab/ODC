#pragma once

#include <nlohmann/json.hpp>

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <functional>
#include <memory>
#include <string>

namespace odc::hook {

struct WsClientConfig {
    std::string server_host{"127.0.0.1"};
    uint16_t server_port{9443};
    std::string server_uri{"/ws"};
    std::string client_cert_path;
    std::string client_key_path;
    std::string ca_cert_path;
    int reconnect_interval_sec{5};
    int connect_timeout_sec{10};
    int idle_timeout_sec{300};
    bool verify_server{true};
};

class WsClient {
public:
    explicit WsClient(asio::io_context& io, const WsClientConfig& cfg);
    ~WsClient();

    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;

    bool connect();
    void disconnect();
    bool is_connected() const;

    bool send(const std::string& message);
    bool send_json(const nlohmann::json& data);

    using MessageCallback = std::function<void(std::string_view payload)>;
    using ConnectCallback = std::function<void()>;
    using DisconnectCallback = std::function<void(const std::string& reason)>;

    void set_on_message(MessageCallback cb) { on_message_ = std::move(cb); }
    void set_on_connected(ConnectCallback cb) { on_connected_ = std::move(cb); }
    void set_on_disconnected(DisconnectCallback cb) { on_disconnected_ = std::move(cb); }

    static std::string ws_encode(const std::string& payload, uint8_t opcode);
    static bool ws_parse_frame(const char* data, size_t len,
                               std::string& payload, uint8_t& opcode, bool& fin);

    const WsClientConfig& config() const { return config_; }

private:
    void do_connect();
    void do_ssl_handshake();
    void do_ws_handshake();
    void do_read();
    void do_write();
    void start_reconnect_timer();
    void on_disconnected(const std::string& reason);

    void setup_ssl_context(asio::ssl::context& ctx);
    static std::string generate_ws_key();

    WsClientConfig config_;
    asio::io_context& io_ctx_;
    std::unique_ptr<asio::ssl::context> ssl_ctx_;
    std::unique_ptr<asio::ssl::stream<asio::ip::tcp::socket>> socket_;
    std::unique_ptr<asio::steady_timer> reconnect_timer_;

    bool connected_{false};
    bool connecting_{false};
    std::string read_buffer_;

    MessageCallback on_message_;
    ConnectCallback on_connected_;
    DisconnectCallback on_disconnected_;

    std::string ws_key_;
};

} // namespace odc::hook
