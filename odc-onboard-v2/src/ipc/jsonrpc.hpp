#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <atomic>

namespace odc::ipc {

struct JsonRpcRequest {
    std::string jsonrpc = "2.0";
    std::string method;
    nlohmann::json params;
    std::optional<int64_t> id;
};

struct JsonRpcResponse {
    std::string jsonrpc = "2.0";
    std::optional<nlohmann::json> result;
    struct Error {
        int code;
        std::string message;
    };
    std::optional<Error> error;
    std::optional<int64_t> id;
};

struct JsonRpcNotification {
    std::string jsonrpc = "2.0";
    std::string method;
    nlohmann::json params;
};

inline std::string serialize_request(const JsonRpcRequest& req) {
    nlohmann::json j;
    j["jsonrpc"] = req.jsonrpc;
    j["method"] = req.method;
    if (!req.params.empty()) {
        j["params"] = req.params;
    }
    if (req.id.has_value()) {
        j["id"] = req.id.value();
    }
    return j.dump() + "\n";
}

inline std::string serialize_response(const JsonRpcResponse& resp) {
    nlohmann::json j;
    j["jsonrpc"] = resp.jsonrpc;
    if (resp.result.has_value()) {
        j["result"] = resp.result.value();
    }
    if (resp.error.has_value()) {
        j["error"] = nlohmann::json{
            {"code", resp.error->code},
            {"message", resp.error->message}
        };
    }
    if (resp.id.has_value()) {
        j["id"] = resp.id.value();
    }
    return j.dump() + "\n";
}

inline std::string serialize_notification(const JsonRpcNotification& notif) {
    nlohmann::json j;
    j["jsonrpc"] = notif.jsonrpc;
    j["method"] = notif.method;
    if (!notif.params.empty()) {
        j["params"] = notif.params;
    }
    return j.dump() + "\n";
}

inline std::optional<JsonRpcRequest> parse_request(const std::string& line) {
    try {
        auto j = nlohmann::json::parse(line);
        if (!j.contains("jsonrpc") || j["jsonrpc"] != "2.0") return std::nullopt;
        if (!j.contains("method")) return std::nullopt;

        JsonRpcRequest req;
        req.method = j["method"].get<std::string>();
        if (j.contains("params")) req.params = j["params"];
        if (j.contains("id")) req.id = j["id"].get<int64_t>();
        return req;
    } catch (...) {
        return std::nullopt;
    }
}

inline std::optional<JsonRpcResponse> parse_response(const std::string& line) {
    try {
        auto j = nlohmann::json::parse(line);
        if (!j.contains("jsonrpc") || j["jsonrpc"] != "2.0") return std::nullopt;
        if (!j.contains("id")) return std::nullopt;
        if (j.contains("method")) return std::nullopt;

        JsonRpcResponse resp;
        if (j.contains("result")) resp.result = j["result"];
        if (j.contains("error")) {
            resp.error = JsonRpcResponse::Error{
                j["error"]["code"].get<int>(),
                j["error"]["message"].get<std::string>()
            };
        }
        resp.id = j["id"].get<int64_t>();
        return resp;
    } catch (...) {
        return std::nullopt;
    }
}

inline std::optional<JsonRpcNotification> parse_notification(const std::string& line) {
    try {
        auto j = nlohmann::json::parse(line);
        if (!j.contains("jsonrpc") || j["jsonrpc"] != "2.0") return std::nullopt;
        if (!j.contains("method")) return std::nullopt;
        if (j.contains("id")) return std::nullopt;

        JsonRpcNotification notif;
        notif.method = j["method"].get<std::string>();
        if (j.contains("params")) notif.params = j["params"];
        return notif;
    } catch (...) {
        return std::nullopt;
    }
}

inline JsonRpcResponse make_error_response(int64_t id, int code, const std::string& msg) {
    JsonRpcResponse resp;
    resp.id = id;
    resp.error = JsonRpcResponse::Error{code, msg};
    return resp;
}

inline JsonRpcResponse make_success_response(int64_t id, const nlohmann::json& result) {
    JsonRpcResponse resp;
    resp.id = id;
    resp.result = result;
    return resp;
}

class IdGenerator {
public:
    int64_t next() { return next_id_.fetch_add(1, std::memory_order_relaxed); }
private:
    std::atomic<int64_t> next_id_{1};
};

}
