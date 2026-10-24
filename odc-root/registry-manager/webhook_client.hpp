#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace odc {

struct WebhookConfig {
    std::string id;
    std::string url;
    std::string secret;
    std::vector<std::string> events;
    bool enabled{true};
    int max_retries{5};
    int retry_base_sec{1};
    int timeout_sec{10};
};

class WebhookClient {
public:
    WebhookClient() = default;
    ~WebhookClient();

    WebhookClient(const WebhookClient&) = delete;
    WebhookClient& operator=(const WebhookClient&) = delete;

    void add_webhook(const WebhookConfig& config);
    void remove_webhook(const std::string& id);
    void update_webhook(const WebhookConfig& config);
    std::vector<WebhookConfig> list_webhooks() const;
    bool is_enabled(const std::string& id) const;

    void deliver(const std::string& event_type, const nlohmann::json& payload);

    using DeliveryCallback = std::function<void(const std::string& webhook_id,
                                                 bool success,
                                                 int http_status,
                                                 const std::string& error)>;
    void set_on_delivery(DeliveryCallback cb) { delivery_callback_ = std::move(cb); }

private:
    void do_deliver(const WebhookConfig& webhook, const nlohmann::json& payload, int attempt);
    std::string compute_hmac(const std::string& secret, const std::string& body);
    static std::string base64_encode(const unsigned char* data, size_t len);
    bool http_post(const std::string& url, const std::string& body,
                   const std::string& signature, int timeout_sec);

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, WebhookConfig> webhooks_;
    std::unordered_map<std::string, int> consecutive_failures_;
    DeliveryCallback delivery_callback_;
};

} // namespace odc
