#include "webhook_client.hpp"
#include "odc/logging.hpp"
#include "odc/http_client.hpp"

#include <openssl/hmac.h>
#include <openssl/evp.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <thread>

namespace odc {

WebhookClient::~WebhookClient() {
}

void WebhookClient::add_webhook(const WebhookConfig& config) {
    std::unique_lock lock(mutex_);
    webhooks_[config.id] = config;
    consecutive_failures_[config.id] = 0;
}

void WebhookClient::remove_webhook(const std::string& id) {
    std::unique_lock lock(mutex_);
    webhooks_.erase(id);
    consecutive_failures_.erase(id);
}

void WebhookClient::update_webhook(const WebhookConfig& config) {
    add_webhook(config);
}

std::vector<WebhookConfig> WebhookClient::list_webhooks() const {
    std::shared_lock lock(mutex_);
    std::vector<WebhookConfig> result;
    result.reserve(webhooks_.size());
    for (const auto& [id, cfg] : webhooks_) {
        result.push_back(cfg);
    }
    return result;
}

bool WebhookClient::is_enabled(const std::string& id) const {
    std::shared_lock lock(mutex_);
    auto it = webhooks_.find(id);
    return it != webhooks_.end() && it->second.enabled;
}

void WebhookClient::deliver(const std::string& event_type,
                             const nlohmann::json& payload) {
    std::shared_lock lock(mutex_);
    std::vector<WebhookConfig> targets;
    for (const auto& [id, cfg] : webhooks_) {
        if (!cfg.enabled) continue;
        if (cfg.events.empty()) continue;
        bool matches = false;
        for (const auto& pattern : cfg.events) {
            if (pattern == "*" || pattern == event_type) {
                matches = true;
                break;
            }
        }
        if (matches) {
            targets.push_back(cfg);
        }
    }

    for (const auto& cfg : targets) {
        std::thread([this, cfg, event_type, payload]() {
            nlohmann::json body = {
                {"event", event_type},
                {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()},
                {"payload", payload}
            };
            do_deliver(cfg, body, 0);
        }).detach();
    }
}

// ── Private ─────────────────────────────────────────────────────────────────

void WebhookClient::do_deliver(const WebhookConfig& webhook,
                                const nlohmann::json& payload, int attempt) {
    auto logger = get_logger("webhook");

    std::string body = payload.dump();
    std::string signature;

    if (!webhook.secret.empty()) {
        signature = compute_hmac(webhook.secret, body);
    }

    HttpClientConfig http_cfg;
    http_cfg.connect_timeout = std::chrono::seconds(webhook.timeout_sec);
    http_cfg.request_timeout = std::chrono::seconds(webhook.timeout_sec);
    // Note: CA cert and client cert for mTLS can be added to WebhookConfig in future

    HttpClient client(http_cfg);

    std::unordered_map<std::string, std::string> headers;
    if (!signature.empty()) {
        headers["X-ODC-Signature"] = signature;
    }

    HttpResponse response = client.post(webhook.url, body, headers);

    bool success = (response.status_code >= 200 && response.status_code < 300);

    if (success) {
        std::unique_lock lock(mutex_);
        consecutive_failures_[webhook.id] = 0;
        SPDLOG_LOGGER_DEBUG(logger, "Webhook '{}' delivered (attempt {}, status {})",
                           webhook.id, attempt + 1, response.status_code);
        if (delivery_callback_) {
            delivery_callback_(webhook.id, true, response.status_code, "");
        }
        return;
    }

    SPDLOG_LOGGER_WARN(logger, "Webhook '{}' delivery failed (attempt {}/{}, status {})",
                       webhook.id, attempt + 1, webhook.max_retries,
                       response.status_code);

    {
        std::unique_lock lock(mutex_);
        consecutive_failures_[webhook.id]++;
        if (consecutive_failures_[webhook.id] >= webhook.max_retries) {
            auto it = webhooks_.find(webhook.id);
            if (it != webhooks_.end()) {
                it->second.enabled = false;
                SPDLOG_LOGGER_ERROR(logger, "Webhook '{}' disabled after {} consecutive failures",
                                    webhook.id, webhook.max_retries);
            }
        }
    }

    if (attempt < webhook.max_retries - 1) {
        int delay_sec = webhook.retry_base_sec *
                        static_cast<int>(std::pow(2, attempt));
        if (delay_sec > 60) delay_sec = 60;

        std::this_thread::sleep_for(std::chrono::seconds(delay_sec));
        do_deliver(webhook, payload, attempt + 1);
    } else {
        if (delivery_callback_) {
            delivery_callback_(webhook.id, false, response.status_code,
                               "Max retries exceeded");
        }
    }
}

std::string WebhookClient::compute_hmac(const std::string& secret,
                                         const std::string& body) {
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int result_len = 0;

    HMAC(EVP_sha256(),
         secret.data(), static_cast<int>(secret.size()),
         reinterpret_cast<const unsigned char*>(body.data()), body.size(),
         result, &result_len);

    return base64_encode(result, result_len);
}

std::string WebhookClient::base64_encode(const unsigned char* data, size_t len) {
    static const char* chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t bits = (static_cast<uint32_t>(data[i]) << 16);
        if (i + 1 < len) bits |= (static_cast<uint32_t>(data[i + 1]) << 8);
        if (i + 2 < len) bits |= static_cast<uint32_t>(data[i + 2]);

        result += chars[(bits >> 18) & 0x3F];
        result += chars[(bits >> 12) & 0x3F];
        result += (i + 1 < len) ? chars[(bits >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? chars[bits & 0x3F] : '=';
    }
    return result;
}

} // namespace odc
