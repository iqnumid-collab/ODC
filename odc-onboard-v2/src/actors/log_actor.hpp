#pragma once

#include "core/event.hpp"
#include "core/event_bus.hpp"
#include "core/log_control.hpp"

#include <caf/event_based_actor.hpp>
#include <caf/behavior.hpp>

#include <spdlog/spdlog.h>

namespace odc {

class log_actor_impl : public caf::event_based_actor {
public:
    explicit log_actor_impl(caf::actor_config& cfg, EventBus& bus, LogControl& log_control)
        : event_based_actor(cfg), bus_(bus), log_control_(log_control) {
        bus_.subscribe([this](const Event& event) {
            handle_event(event);
        });
    }

    caf::behavior make_behavior() override {
        return {
            [this](mon_tick) {
                flush_logs();
            }
        };
    }

private:
    void handle_event(const Event& event) {
        if (const auto* e = std::get_if<LogEvent>(&event)) {
            auto logger = log_control_.get_logger(e->logger_name);
            auto level = to_spdlog_level(e->level);
            logger->log(level, "[actor] {}", e->message);
        } else if (const auto* e = std::get_if<SetLogLevelEvent>(&event)) {
            log_control_.set_level(e->logger_name, e->level);
        }
    }

    void flush_logs() {
        auto metrics = log_control_.get_logger("log_actor");
        SPDLOG_LOGGER_DEBUG(metrics, "Log actor tick");
    }

    EventBus& bus_;
    LogControl& log_control_;
};

inline auto make_log_actor(caf::actor_system& sys, EventBus& bus, LogControl& log_control) {
    auto actor = sys.spawn<log_actor_impl>(std::ref(bus), std::ref(log_control));
    caf::anon_send(actor, mon_tick{});
    return actor;
}

}
