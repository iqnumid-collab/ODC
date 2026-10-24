#pragma once

#include "odc/sqlite_db.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace odc {

struct ServiceEntry {
    std::string id;
    std::string name;
    std::string type;
    nlohmann::json metadata;
    std::string registered_at;
    std::string updated_at;
};

struct ClientEntry {
    std::string id;
    std::string name;
    std::string cert_fingerprint;
    bool ws_connected{false};
    std::string last_seen;
};

struct WebhookEntry {
    std::string id;
    std::string url;
    std::string secret;
    std::vector<std::string> events;
    bool enabled{true};
    int consecutive_failures{0};
};

struct EventEntry {
    int64_t id{0};
    std::string service_id;
    std::string type;
    nlohmann::json payload;
    std::string created_at;
};

class DataStore {
public:
    explicit DataStore(SqliteDb& db);
    ~DataStore() = default;

    bool initialize();

    // Services
    bool register_service(const ServiceEntry& service);
    bool unregister_service(const std::string& service_id);
    bool update_service(const std::string& service_id, const nlohmann::json& metadata);
    std::optional<ServiceEntry> get_service(const std::string& service_id);
    std::vector<ServiceEntry> list_services(const std::string& type_filter = "");
    std::vector<ServiceEntry> search_services(const std::string& query);
    size_t service_count() const;

    // Clients
    bool register_client(const ClientEntry& client);
    bool mark_client_connected(const std::string& client_id);
    bool mark_client_disconnected(const std::string& client_id);
    std::optional<ClientEntry> get_client(const std::string& client_id);
    std::vector<ClientEntry> list_clients();

    // Webhooks
    bool add_webhook(const WebhookEntry& webhook);
    bool remove_webhook(const std::string& webhook_id);
    bool update_webhook(const WebhookEntry& webhook);
    std::vector<WebhookEntry> list_webhooks();
    bool record_webhook_failure(const std::string& webhook_id);
    bool reset_webhook_failures(const std::string& webhook_id);

    // Events log
    bool log_event(const EventEntry& event);
    std::vector<EventEntry> query_events(const std::string& type_filter = "",
                                          int limit = 100, int64_t since_id = 0);

private:
    SqliteDb& db_;
};

} // namespace odc
