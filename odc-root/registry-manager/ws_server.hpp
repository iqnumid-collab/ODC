#pragma once

#include "cert_revocation.hpp"

#include <nlohmann/json.hpp>

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <atomic>
#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace odc {

struct MtlsConfig {
    bool enabled{true};
    std::string ca_cert_path;
    int verify_depth{2};
    std::string crl_path;
    std::string cipher_list{"ECDHE+AESGCM:!aNULL:!MD5:!RC4"};
    bool require_client_cert{true};
};

struct WsServerConfig {
    uint16_t listen_port{9443};
    std::string listen_address{"0.0.0.0"};
    std::string tls_cert_path;
    std::string tls_key_path;
    std::string url_path{"/ws"};
    int max_connections{1000};
    int idle_timeout_sec{300};
    bool verify_client_cert{false};
    std::string ca_cert_path;
    MtlsConfig mtls;
};

struct ClientCertInfo {
    std::string fingerprint;
    std::string common_name;
    int64_t not_before{0};
    int64_t not_after{0};
    std::string serial;
};

class WsServer {
public:
    explicit WsServer(const WsServerConfig& config);
    ~WsServer();

    WsServer(const WsServer&) = delete;
    WsServer& operator=(const WsServer&) = delete;

    bool start();
    void stop();
    void run();
    bool is_running() const { return running_; }

    bool send(const std::string& connection_id, const std::string& message);
    bool send_json(const std::string& connection_id, const nlohmann::json& data);

    void broadcast(const std::string& message);
    void broadcast_json(const nlohmann::json& data);

    void publish(const std::string& topic, const nlohmann::json& data);

    size_t connection_count() const;

    std::optional<ClientCertInfo> get_client_cert(const std::string& connection_id) const;

    // Pub/sub topic management — called internally from the read loop
    // when a client sends {"type":"subscribe"/"unsubscribe",...} messages
    void subscribe(const std::string& connection_id, const std::string& topic);
    void unsubscribe(const std::string& connection_id, const std::string& topic);
    void unsubscribe_all(const std::string& connection_id);

    using MessageCallback = std::function<void(const std::string& connection_id,
                                                const nlohmann::json& data)>;
    using ConnectCallback = std::function<void(const std::string& connection_id)>;
    using DisconnectCallback = std::function<void(const std::string& connection_id)>;
    using CertCallback = std::function<void(const std::string& connection_id,
                                             const ClientCertInfo& cert)>;

    void set_on_message(MessageCallback cb) { on_message_ = std::move(cb); }
    void set_on_connect(ConnectCallback cb) { on_connect_ = std::move(cb); }
    void set_on_disconnect(DisconnectCallback cb) { on_disconnect_ = std::move(cb); }
    void set_on_client_cert(CertCallback cb) { on_client_cert_ = std::move(cb); }

private:
    // ── Connection state tracking ──────────────────────────────────────────

    static constexpr size_t kReadBufSize = 65536;
    using SocketPtr = std::shared_ptr<asio::ssl::stream<asio::ip::tcp::socket>>;

    // Per-connection WebSocket frame parsing state
    struct ConnReadState {
        SocketPtr socket;
        std::array<uint8_t, kReadBufSize> read_buf{};
        std::vector<uint8_t> parse_buf;     // accumulated unparsed bytes
        std::vector<uint8_t> frame_payload; // accumulated payload for fragmented frames
        uint8_t frag_opcode{0};             // opcode of first fragment (0 = no fragment in progress)
        bool upgrade_done{false};           // HTTP upgrade completed
        bool disconnected{false};           // connection already cleaned up
    };

    // ── Accept + TLS ───────────────────────────────────────────────────────

    void do_accept();
    void handle_accept(SocketPtr socket, const asio::error_code& ec);
    void setup_ssl_context();
    bool extract_client_cert(SSL* ssl, const std::string& conn_id);

    // ── HTTP upgrade handshake ─────────────────────────────────────────────

    void start_http_upgrade(const std::string& conn_id);
    void handle_upgrade_read(const std::string& conn_id,
                             const asio::error_code& ec, size_t bytes);
    bool try_http_upgrade(const std::string& conn_id);
    void send_upgrade_response(const std::string& conn_id);
    void send_http_error(const std::string& conn_id, int status,
                         const std::string& message);

    // ── WebSocket read loop ────────────────────────────────────────────────

    void start_read_loop(const std::string& conn_id);
    void handle_read(const std::string& conn_id,
                     const asio::error_code& ec, size_t bytes);
    void parse_websocket_frames(const std::string& conn_id);
    void dispatch_message(const std::string& conn_id, const std::string& payload);
    void handle_ping(const std::string& conn_id,
                     const std::vector<uint8_t>& payload);
    void handle_close(const std::string& conn_id,
                      const std::vector<uint8_t>& payload);

    // ── Connection cleanup ─────────────────────────────────────────────────

    void cleanup_connection(const std::string& conn_id);

    // ── Helpers ────────────────────────────────────────────────────────────

    void check_crl(const std::string& fingerprint, const std::string& conn_id);
    static std::string format_fingerprint(const unsigned char* data, size_t len);
    static std::string ws_frame(const std::string& payload, uint8_t opcode);

    WsServerConfig config_;
    asio::io_context io_ctx_;
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;
    std::unique_ptr<asio::ssl::context> ssl_ctx_;

    std::atomic<bool> running_{false};
    uint64_t next_conn_id_{1};
    std::mutex id_mutex_;

    MessageCallback on_message_;
    ConnectCallback on_connect_;
    DisconnectCallback on_disconnect_;
    CertCallback on_client_cert_;

    std::unique_ptr<CertRevocationList> crl_;

    mutable std::mutex conn_mutex_;
    std::unordered_map<std::string, SocketPtr> connections_;
    std::unordered_map<std::string, ClientCertInfo> connection_certs_;
    std::unordered_map<std::string, std::shared_ptr<ConnReadState>> read_states_;

    mutable std::mutex subs_mutex_;
    std::unordered_map<std::string, std::unordered_set<std::string>> subscribers_;
};

} // namespace odc
