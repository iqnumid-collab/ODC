#pragma once

#include "ipc/jsonrpc.hpp"

#include <nlohmann/json.hpp>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <shared_mutex>
#include <string>
#include <optional>

namespace odc::ipc {

class MethodRegistry {
public:
    using Handler = std::function<nlohmann::json(const nlohmann::json& params)>;

    void register_method(const std::string& name, Handler handler) {
        std::unique_lock lock(mutex_);
        handlers_[name] = std::move(handler);
    }

    std::optional<Handler> get(const std::string& name) const {
        std::shared_lock lock(mutex_);
        auto it = handlers_.find(name);
        if (it == handlers_.end()) return std::nullopt;
        return it->second;
    }

    bool exists(const std::string& name) const {
        std::shared_lock lock(mutex_);
        return handlers_.count(name) > 0;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Handler> handlers_;
};

}
