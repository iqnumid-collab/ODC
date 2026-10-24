#pragma once

#include "core/event.hpp"
#include "core/event_bus.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <optional>

namespace odc {

inline spdlog::level::level_enum to_spdlog_level(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:    return spdlog::level::trace;
        case LogLevel::Debug:    return spdlog::level::debug;
        case LogLevel::Info:     return spdlog::level::info;
        case LogLevel::Warn:     return spdlog::level::warn;
        case LogLevel::Error:    return spdlog::level::err;
        case LogLevel::Critical: return spdlog::level::critical;
        case LogLevel::Off:      return spdlog::level::off;
    }
    return spdlog::level::info;
}

inline LogLevel from_spdlog_level(spdlog::level::level_enum level) {
    switch (level) {
        case spdlog::level::trace:    return LogLevel::Trace;
        case spdlog::level::debug:    return LogLevel::Debug;
        case spdlog::level::info:     return LogLevel::Info;
        case spdlog::level::warn:     return LogLevel::Warn;
        case spdlog::level::err:      return LogLevel::Error;
        case spdlog::level::critical: return LogLevel::Critical;
        case spdlog::level::off:      return LogLevel::Off;
    }
    return LogLevel::Info;
}

class LogControl {
public:
    explicit LogControl(EventBus& bus)
        : bus_(bus) {
        default_logger_ = spdlog::stdout_color_mt("odc-onboard");
        default_logger_->set_level(spdlog::level::info);
        spdlog::set_default_logger(default_logger_);
    }

    void init() {
        subscription_id_ = bus_.subscribe([this](const Event& event) {
            handle_event(event);
        });
    }

    void shutdown() {
        if (subscription_id_.has_value()) {
            bus_.unsubscribe(*subscription_id_);
        }
        spdlog::shutdown();
    }

    std::shared_ptr<spdlog::logger> get_logger(std::string_view name = "odc-onboard") {
        std::lock_guard lock(mutex_);
        auto it = loggers_.find(std::string(name));
        if (it != loggers_.end()) {
            return it->second;
        }

        auto logger = spdlog::stdout_color_mt(std::string(name));
        logger->set_level(default_logger_->level());
        loggers_[std::string(name)] = logger;
        return logger;
    }

    void set_level(std::string_view name, LogLevel level) {
        auto logger = get_logger(name);
        logger->set_level(to_spdlog_level(level));
    }

    void set_default_level(LogLevel level) {
        std::lock_guard lock(mutex_);
        auto spdlog_level = to_spdlog_level(level);
        default_logger_->set_level(spdlog_level);
        for (auto& [name, logger] : loggers_) {
            logger->set_level(spdlog_level);
        }
    }

private:
    void handle_event(const Event& event) {
        if (const auto* e = std::get_if<SetLogLevelEvent>(&event)) {
            set_level(e->logger_name, e->level);
        } else if (const auto* e = std::get_if<LogEvent>(&event)) {
            auto logger = get_logger(e->logger_name);
            auto level = to_spdlog_level(e->level);
            logger->log(level, "{}", e->message);
        }
    }

    EventBus& bus_;
    std::shared_ptr<spdlog::logger> default_logger_;
    std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> loggers_;
    mutable std::mutex mutex_;
    std::optional<EventBus::SubscriptionId> subscription_id_;
};

}
