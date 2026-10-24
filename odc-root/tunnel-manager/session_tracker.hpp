#pragma once

#include "odc/sqlite_db.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace odc {

struct ClientSession {
    std::string client_id;
    std::string server_id;
    std::string tunnel_type;        // "wireguard" or "openvpn"
    std::string common_name;
    std::string cert_fingerprint;
    std::string assigned_ip;
    std::string public_key;
    std::string remote_endpoint;
    bool connected{false};
    int64_t connected_at{0};
    int64_t disconnected_at{0};
    uint64_t bytes_rx{0};
    uint64_t bytes_tx{0};
};

class SessionTracker {
public:
    explicit SessionTracker(SqliteDb& db);
    ~SessionTracker() = default;

    bool initialize();

    bool register_session(const ClientSession& session);
    bool mark_connected(const std::string& client_id, const std::string& assigned_ip);
    bool mark_disconnected(const std::string& client_id);
    bool update_stats(const std::string& client_id, uint64_t rx, uint64_t tx);

    std::optional<ClientSession> get_session(const std::string& client_id);
    std::vector<ClientSession> list_active_sessions();
    std::vector<ClientSession> list_sessions_by_server(const std::string& server_id);
    std::vector<ClientSession> list_all_sessions();

    bool remove_session(const std::string& client_id);

private:
    void sync_to_cache(const std::string& client_id);

    SqliteDb& db_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ClientSession> cache_;
};

} // namespace odc
