#include "data_store.hpp"
#include "odc/logging.hpp"

#include <optional>

namespace odc {

DataStore::DataStore(SqliteDb& db) : db_(db) {}

bool DataStore::initialize() {
    auto logger = get_logger("data_store");

    bool ok = db_.ensure_schema(1, R"(
        CREATE TABLE IF NOT EXISTS services (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            type TEXT NOT NULL,
            metadata_json TEXT DEFAULT '{}',
            registered_at TEXT DEFAULT (datetime('now')),
            updated_at TEXT DEFAULT (datetime('now'))
        );

        CREATE TABLE IF NOT EXISTS clients (
            id TEXT PRIMARY KEY,
            name TEXT,
            cert_fingerprint TEXT,
            ws_connected INTEGER DEFAULT 0,
            last_seen TEXT
        );

        CREATE TABLE IF NOT EXISTS webhooks (
            id TEXT PRIMARY KEY,
            url TEXT NOT NULL,
            secret TEXT,
            events_json TEXT DEFAULT '[]',
            enabled INTEGER DEFAULT 1,
            consecutive_failures INTEGER DEFAULT 0,
            last_failure_at TEXT,
            created_at TEXT DEFAULT (datetime('now'))
        );

        CREATE TABLE IF NOT EXISTS events_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            service_id TEXT,
            type TEXT NOT NULL,
            payload_json TEXT,
            created_at TEXT DEFAULT (datetime('now'))
        );

        CREATE INDEX IF NOT EXISTS idx_services_type ON services(type);
        CREATE INDEX IF NOT EXISTS idx_events_type ON events_log(type);
        CREATE INDEX IF NOT EXISTS idx_events_created ON events_log(created_at);
    )");

    if (!ok) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to initialize data store schema");
        return false;
    }

    SPDLOG_LOGGER_INFO(logger, "Data store initialized");
    return true;
}

// ── Services ────────────────────────────────────────────────────────────────

bool DataStore::register_service(const ServiceEntry& service) {
    auto ins = db_.prepare(R"(
        INSERT OR REPLACE INTO services (id, name, type, metadata_json, updated_at)
        VALUES (?, ?, ?, ?, datetime('now'))
    )");
    if (!ins.valid()) return false;
    ins.bind(1, service.id);
    ins.bind(2, service.name);
    ins.bind(3, service.type);
    ins.bind(4, service.metadata.dump());
    return ins.execute();
}

bool DataStore::unregister_service(const std::string& service_id) {
    auto del = db_.prepare("DELETE FROM services WHERE id = ?");
    if (!del.valid()) return false;
    del.bind(1, service_id);
    return del.execute();
}

bool DataStore::update_service(const std::string& service_id,
                                 const nlohmann::json& metadata) {
    auto upd = db_.prepare(
        "UPDATE services SET metadata_json = ?, updated_at = datetime('now') WHERE id = ?");
    if (!upd.valid()) return false;
    upd.bind(1, metadata.dump());
    upd.bind(2, service_id);
    return upd.execute();
}

std::optional<ServiceEntry> DataStore::get_service(const std::string& service_id) {
    auto sel = db_.prepare(
        "SELECT id, name, type, metadata_json, registered_at, updated_at FROM services WHERE id = ?");
    if (!sel.valid()) return std::nullopt;
    sel.bind(1, service_id);
    if (!sel.step()) return std::nullopt;

    ServiceEntry s;
    s.id = sel.column_text(0);
    s.name = sel.column_text(1);
    s.type = sel.column_text(2);
    try { s.metadata = nlohmann::json::parse(sel.column_text(3)); } catch (...) {}
    s.registered_at = sel.column_text(4);
    s.updated_at = sel.column_text(5);
    return s;
}

std::vector<ServiceEntry> DataStore::list_services(const std::string& type_filter) {
    std::vector<ServiceEntry> results;

    std::optional<SqliteDb::Statement> stmt;
    if (type_filter.empty()) {
        stmt.emplace(db_.prepare(
            "SELECT id, name, type, metadata_json, registered_at, updated_at FROM services ORDER BY name"));
    } else {
        stmt.emplace(db_.prepare(
            "SELECT id, name, type, metadata_json, registered_at, updated_at FROM services WHERE type = ? ORDER BY name"));
        stmt->bind(1, type_filter);
    }

    if (!stmt->valid()) return results;

    while (stmt->step()) {
        ServiceEntry s;
        s.id = stmt->column_text(0);
        s.name = stmt->column_text(1);
        s.type = stmt->column_text(2);
        try { s.metadata = nlohmann::json::parse(stmt->column_text(3)); } catch (...) {}
        s.registered_at = stmt->column_text(4);
        s.updated_at = stmt->column_text(5);
        results.push_back(s);
    }
    return results;
}

std::vector<ServiceEntry> DataStore::search_services(const std::string& query) {
    std::vector<ServiceEntry> results;
    auto stmt = db_.prepare(
        "SELECT id, name, type, metadata_json, registered_at, updated_at FROM services "
        "WHERE name LIKE ? OR type LIKE ? ORDER BY name");
    if (!stmt.valid()) return results;

    std::string pattern = "%" + query + "%";
    stmt.bind(1, pattern);
    stmt.bind(2, pattern);

    while (stmt.step()) {
        ServiceEntry s;
        s.id = stmt.column_text(0);
        s.name = stmt.column_text(1);
        s.type = stmt.column_text(2);
        try { s.metadata = nlohmann::json::parse(stmt.column_text(3)); } catch (...) {}
        s.registered_at = stmt.column_text(4);
        s.updated_at = stmt.column_text(5);
        results.push_back(s);
    }
    return results;
}

size_t DataStore::service_count() const {
    auto stmt = const_cast<SqliteDb&>(db_).prepare("SELECT COUNT(*) FROM services");
    if (stmt.valid() && stmt.step()) {
        return static_cast<size_t>(stmt.column_int(0));
    }
    return 0;
}

// ── Clients ─────────────────────────────────────────────────────────────────

bool DataStore::register_client(const ClientEntry& client) {
    auto ins = db_.prepare(R"(
        INSERT OR REPLACE INTO clients (id, name, cert_fingerprint, ws_connected)
        VALUES (?, ?, ?, ?)
    )");
    if (!ins.valid()) return false;
    ins.bind(1, client.id);
    ins.bind(2, client.name);
    ins.bind(3, client.cert_fingerprint);
    ins.bind(4, client.ws_connected ? 1 : 0);
    return ins.execute();
}

bool DataStore::mark_client_connected(const std::string& client_id) {
    auto upd = db_.prepare(
        "UPDATE clients SET ws_connected = 1, last_seen = datetime('now') WHERE id = ?");
    if (!upd.valid()) return false;
    upd.bind(1, client_id);
    return upd.execute();
}

bool DataStore::mark_client_disconnected(const std::string& client_id) {
    auto upd = db_.prepare(
        "UPDATE clients SET ws_connected = 0, last_seen = datetime('now') WHERE id = ?");
    if (!upd.valid()) return false;
    upd.bind(1, client_id);
    return upd.execute();
}

std::optional<ClientEntry> DataStore::get_client(const std::string& client_id) {
    auto sel = db_.prepare(
        "SELECT id, name, cert_fingerprint, ws_connected, last_seen FROM clients WHERE id = ?");
    if (!sel.valid()) return std::nullopt;
    sel.bind(1, client_id);
    if (!sel.step()) return std::nullopt;

    ClientEntry c;
    c.id = sel.column_text(0);
    c.name = sel.column_text(1);
    c.cert_fingerprint = sel.column_text(2);
    c.ws_connected = sel.column_int(3) != 0;
    c.last_seen = sel.column_text(4);
    return c;
}

std::vector<ClientEntry> DataStore::list_clients() {
    std::vector<ClientEntry> results;
    auto sel = db_.prepare(
        "SELECT id, name, cert_fingerprint, ws_connected, last_seen FROM clients ORDER BY name");
    if (!sel.valid()) return results;

    while (sel.step()) {
        ClientEntry c;
        c.id = sel.column_text(0);
        c.name = sel.column_text(1);
        c.cert_fingerprint = sel.column_text(2);
        c.ws_connected = sel.column_int(3) != 0;
        c.last_seen = sel.column_text(4);
        results.push_back(c);
    }
    return results;
}

// ── Webhooks ────────────────────────────────────────────────────────────────

bool DataStore::add_webhook(const WebhookEntry& webhook) {
    nlohmann::json events_json = webhook.events;
    auto ins = db_.prepare(R"(
        INSERT OR REPLACE INTO webhooks (id, url, secret, events_json, enabled)
        VALUES (?, ?, ?, ?, ?)
    )");
    if (!ins.valid()) return false;
    ins.bind(1, webhook.id);
    ins.bind(2, webhook.url);
    ins.bind(3, webhook.secret);
    ins.bind(4, events_json.dump());
    ins.bind(5, webhook.enabled ? 1 : 0);
    return ins.execute();
}

bool DataStore::remove_webhook(const std::string& webhook_id) {
    auto del = db_.prepare("DELETE FROM webhooks WHERE id = ?");
    if (!del.valid()) return false;
    del.bind(1, webhook_id);
    return del.execute();
}

bool DataStore::update_webhook(const WebhookEntry& webhook) {
    return add_webhook(webhook);
}

std::vector<WebhookEntry> DataStore::list_webhooks() {
    std::vector<WebhookEntry> results;
    auto sel = db_.prepare(
        "SELECT id, url, secret, events_json, enabled, consecutive_failures FROM webhooks");
    if (!sel.valid()) return results;

    while (sel.step()) {
        WebhookEntry w;
        w.id = sel.column_text(0);
        w.url = sel.column_text(1);
        w.secret = sel.column_text(2);
        try {
            nlohmann::json events = nlohmann::json::parse(sel.column_text(3));
            for (const auto& e : events) w.events.push_back(e.get<std::string>());
        } catch (...) {}
        w.enabled = sel.column_int(4) != 0;
        w.consecutive_failures = sel.column_int(5);
        results.push_back(w);
    }
    return results;
}

bool DataStore::record_webhook_failure(const std::string& webhook_id) {
    auto upd = db_.prepare(
        "UPDATE webhooks SET consecutive_failures = consecutive_failures + 1, "
        "last_failure_at = datetime('now') WHERE id = ?");
    if (!upd.valid()) return false;
    upd.bind(1, webhook_id);
    return upd.execute();
}

bool DataStore::reset_webhook_failures(const std::string& webhook_id) {
    auto upd = db_.prepare(
        "UPDATE webhooks SET consecutive_failures = 0, last_failure_at = NULL, enabled = 1 WHERE id = ?");
    if (!upd.valid()) return false;
    upd.bind(1, webhook_id);
    return upd.execute();
}

// ── Events log ──────────────────────────────────────────────────────────────

bool DataStore::log_event(const EventEntry& event) {
    auto ins = db_.prepare(R"(
        INSERT INTO events_log (service_id, type, payload_json)
        VALUES (?, ?, ?)
    )");
    if (!ins.valid()) return false;
    ins.bind(1, event.service_id);
    ins.bind(2, event.type);
    ins.bind(3, event.payload.dump());
    return ins.execute();
}

std::vector<EventEntry> DataStore::query_events(const std::string& type_filter,
                                                  int limit, int64_t since_id) {
    std::vector<EventEntry> results;
    std::string sql;
    bool has_filter = false;

    sql = "SELECT id, service_id, type, payload_json, created_at FROM events_log";
    if (!type_filter.empty() || since_id > 0) {
        sql += " WHERE ";
        if (since_id > 0) {
            sql += "id > " + std::to_string(since_id);
            has_filter = true;
        }
        if (!type_filter.empty()) {
            if (has_filter) sql += " AND ";
            sql += "type = '" + type_filter + "'";
        }
    }
    sql += " ORDER BY id DESC LIMIT " + std::to_string(limit);

    auto stmt = db_.prepare(sql);
    if (!stmt.valid()) return results;

    while (stmt.step()) {
        EventEntry e;
        e.id = stmt.column_int64(0);
        e.service_id = stmt.column_text(1);
        e.type = stmt.column_text(2);
        try { e.payload = nlohmann::json::parse(stmt.column_text(3)); } catch (...) {}
        e.created_at = stmt.column_text(4);
        results.push_back(e);
    }
    return results;
}

} // namespace odc
