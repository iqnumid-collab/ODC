#include "session_tracker.hpp"
#include "odc/logging.hpp"

#include <ctime>
#include <mutex>

namespace odc {

SessionTracker::SessionTracker(SqliteDb& db)
    : db_(db) {
}

bool SessionTracker::initialize() {
    auto logger = get_logger("session_tracker");

    bool ok = db_.ensure_schema(1, R"(
        CREATE TABLE IF NOT EXISTS vpn_servers (
            id TEXT PRIMARY KEY,
            type TEXT NOT NULL,
            config_json TEXT NOT NULL,
            running INTEGER DEFAULT 0,
            created_at TEXT DEFAULT (datetime('now')),
            updated_at TEXT DEFAULT (datetime('now'))
        );

        CREATE TABLE IF NOT EXISTS vpn_clients (
            id TEXT PRIMARY KEY,
            server_id TEXT NOT NULL,
            tunnel_type TEXT NOT NULL DEFAULT 'wireguard',
            common_name TEXT NOT NULL,
            cert_fingerprint TEXT,
            assigned_ip TEXT,
            public_key TEXT,
            remote_endpoint TEXT,
            connected INTEGER DEFAULT 0,
            connected_at INTEGER DEFAULT 0,
            disconnected_at INTEGER DEFAULT 0,
            bytes_rx INTEGER DEFAULT 0,
            bytes_tx INTEGER DEFAULT 0,
            FOREIGN KEY (server_id) REFERENCES vpn_servers(id)
        );

        CREATE INDEX IF NOT EXISTS idx_vpn_clients_server
            ON vpn_clients(server_id);

        CREATE INDEX IF NOT EXISTS idx_vpn_clients_connected
            ON vpn_clients(connected);
    )");

    if (!ok) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to initialize session tracker schema");
        return false;
    }

    // Load existing cache from DB
    auto stmt = db_.prepare("SELECT id, server_id, tunnel_type, common_name, "
                            "cert_fingerprint, assigned_ip, public_key, "
                            "connected, connected_at, disconnected_at, "
                            "bytes_rx, bytes_tx FROM vpn_clients");
    if (stmt.valid()) {
        while (stmt.step()) {
            ClientSession s;
            s.client_id = stmt.column_text(0);
            s.server_id = stmt.column_text(1);
            s.tunnel_type = stmt.column_text(2);
            s.common_name = stmt.column_text(3);
            s.cert_fingerprint = stmt.column_text(4);
            s.assigned_ip = stmt.column_text(5);
            s.public_key = stmt.column_text(6);
            s.connected = stmt.column_int(7) != 0;
            s.connected_at = stmt.column_int64(8);
            s.disconnected_at = stmt.column_int64(9);
            s.bytes_rx = static_cast<uint64_t>(stmt.column_int64(10));
            s.bytes_tx = static_cast<uint64_t>(stmt.column_int64(11));
            cache_[s.client_id] = s;
        }
    }

    SPDLOG_LOGGER_INFO(logger, "Session tracker initialized ({} cached sessions)", cache_.size());
    return true;
}

// ── Session lifecycle ───────────────────────────────────────────────────────

bool SessionTracker::register_session(const ClientSession& session) {
    std::lock_guard lock(mutex_);

    auto ins = db_.prepare(R"(
        INSERT OR REPLACE INTO vpn_clients
            (id, server_id, tunnel_type, common_name, cert_fingerprint,
             assigned_ip, public_key, remote_endpoint,
             connected, connected_at, disconnected_at, bytes_rx, bytes_tx)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");

    if (!ins.valid()) return false;

    ins.bind(1, session.client_id);
    ins.bind(2, session.server_id);
    ins.bind(3, session.tunnel_type);
    ins.bind(4, session.common_name);
    ins.bind(5, session.cert_fingerprint);
    ins.bind(6, session.assigned_ip);
    ins.bind(7, session.public_key);
    ins.bind(8, session.remote_endpoint);
    ins.bind(9, session.connected ? 1 : 0);
    ins.bind(10, static_cast<int64_t>(session.connected_at));
    ins.bind(11, static_cast<int64_t>(session.disconnected_at));
    ins.bind(12, static_cast<int64_t>(session.bytes_rx));
    ins.bind(13, static_cast<int64_t>(session.bytes_tx));

    if (!ins.execute()) return false;

    cache_[session.client_id] = session;
    return true;
}

bool SessionTracker::mark_connected(const std::string& client_id, const std::string& assigned_ip) {
    std::lock_guard lock(mutex_);

    auto it = cache_.find(client_id);
    if (it == cache_.end()) return false;

    int64_t now = static_cast<int64_t>(std::time(nullptr));

    auto upd = db_.prepare(R"(
        UPDATE vpn_clients
        SET connected = 1, connected_at = ?, assigned_ip = ?, disconnected_at = 0
        WHERE id = ?
    )");
    if (!upd.valid()) return false;

    upd.bind(1, now);
    upd.bind(2, assigned_ip);
    upd.bind(3, client_id);
    if (!upd.execute()) return false;

    it->second.connected = true;
    it->second.connected_at = now;
    it->second.assigned_ip = assigned_ip;
    it->second.disconnected_at = 0;

    auto logger = get_logger("session_tracker");
    SPDLOG_LOGGER_INFO(logger, "Client '{}' connected (IP: {})", client_id, assigned_ip);
    return true;
}

bool SessionTracker::mark_disconnected(const std::string& client_id) {
    std::lock_guard lock(mutex_);

    auto it = cache_.find(client_id);
    if (it == cache_.end()) return false;

    int64_t now = static_cast<int64_t>(std::time(nullptr));

    auto upd = db_.prepare(R"(
        UPDATE vpn_clients
        SET connected = 0, disconnected_at = ?
        WHERE id = ?
    )");
    if (!upd.valid()) return false;

    upd.bind(1, now);
    upd.bind(2, client_id);
    if (!upd.execute()) return false;

    it->second.connected = false;
    it->second.disconnected_at = now;

    auto logger = get_logger("session_tracker");
    SPDLOG_LOGGER_INFO(logger, "Client '{}' disconnected", client_id);
    return true;
}

bool SessionTracker::update_stats(const std::string& client_id, uint64_t rx, uint64_t tx) {
    std::lock_guard lock(mutex_);

    auto upd = db_.prepare(R"(
        UPDATE vpn_clients SET bytes_rx = ?, bytes_tx = ? WHERE id = ?
    )");
    if (!upd.valid()) return false;

    upd.bind(1, static_cast<int64_t>(rx));
    upd.bind(2, static_cast<int64_t>(tx));
    upd.bind(3, client_id);
    if (!upd.execute()) return false;

    auto it = cache_.find(client_id);
    if (it != cache_.end()) {
        it->second.bytes_rx = rx;
        it->second.bytes_tx = tx;
    }
    return true;
}

// ── Queries ─────────────────────────────────────────────────────────────────

std::optional<ClientSession> SessionTracker::get_session(const std::string& client_id) {
    std::lock_guard lock(mutex_);
    auto it = cache_.find(client_id);
    if (it != cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<ClientSession> SessionTracker::list_active_sessions() {
    std::lock_guard lock(mutex_);
    std::vector<ClientSession> result;
    for (const auto& [id, session] : cache_) {
        if (session.connected) {
            result.push_back(session);
        }
    }
    return result;
}

std::vector<ClientSession> SessionTracker::list_sessions_by_server(const std::string& server_id) {
    std::lock_guard lock(mutex_);
    std::vector<ClientSession> result;
    for (const auto& [id, session] : cache_) {
        if (session.server_id == server_id) {
            result.push_back(session);
        }
    }
    return result;
}

std::vector<ClientSession> SessionTracker::list_all_sessions() {
    std::lock_guard lock(mutex_);
    std::vector<ClientSession> result;
    result.reserve(cache_.size());
    for (const auto& [id, session] : cache_) {
        result.push_back(session);
    }
    return result;
}

bool SessionTracker::remove_session(const std::string& client_id) {
    std::lock_guard lock(mutex_);

    auto del = db_.prepare("DELETE FROM vpn_clients WHERE id = ?");
    if (!del.valid()) return false;
    del.bind(1, client_id);
    if (!del.execute()) return false;

    cache_.erase(client_id);
    return true;
}

void SessionTracker::sync_to_cache(const std::string& client_id) {
    // Internal: re-read from DB to cache (used after external modifications)
    auto stmt = db_.prepare("SELECT id, server_id, tunnel_type, common_name, "
                            "cert_fingerprint, assigned_ip, public_key, "
                            "connected, connected_at, disconnected_at, "
                            "bytes_rx, bytes_tx FROM vpn_clients WHERE id = ?");
    if (!stmt.valid()) return;
    stmt.bind(1, client_id);
    if (!stmt.step()) return;

    ClientSession s;
    s.client_id = stmt.column_text(0);
    s.server_id = stmt.column_text(1);
    s.tunnel_type = stmt.column_text(2);
    s.common_name = stmt.column_text(3);
    s.cert_fingerprint = stmt.column_text(4);
    s.assigned_ip = stmt.column_text(5);
    s.public_key = stmt.column_text(6);
    s.connected = stmt.column_int(8) != 0;
    s.connected_at = stmt.column_int64(9);
    s.disconnected_at = stmt.column_int64(10);
    s.bytes_rx = static_cast<uint64_t>(stmt.column_int64(11));
    s.bytes_tx = static_cast<uint64_t>(stmt.column_int64(12));
    cache_[s.client_id] = s;
}

} // namespace odc
