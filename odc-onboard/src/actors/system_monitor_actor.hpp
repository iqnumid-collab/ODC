#pragma once

#include "core/event.hpp"
#include "core/event_bus.hpp"
#include "thread_orchestrator/orchestrator.hpp"

#include <caf/event_based_actor.hpp>
#include <caf/behavior.hpp>

namespace odc {

class system_monitor_actor_impl : public caf::event_based_actor {
public:
    explicit system_monitor_actor_impl(
        caf::actor_config& cfg,
        orch::ThreadOrchestrator& orchestrator,
        EventBus& bus
    )
        : event_based_actor(cfg),
          orchestrator_(orchestrator),
          bus_(bus),
          running_(false) {}

    caf::behavior make_behavior() override {
        running_ = true;
        delayed_send(this, std::chrono::seconds(5), mon_tick{});

        return {
            [this](mon_tick) {
                if (!running_) return;

                auto metrics = orchestrator_.get_metrics();
                MetricsSnapshotEvent evt{
                    .active_tasks = metrics.active_tasks,
                    .pending_tasks = metrics.pending_tasks,
                    .completed_tasks = metrics.completed_tasks,
                    .worker_count = metrics.worker_count
                };
                bus_.publish(std::move(evt));

                delayed_send(this, std::chrono::seconds(5), mon_tick{});
            },
            [this](mon_start) {
                running_ = true;
                delayed_send(this, std::chrono::seconds(5), mon_tick{});
            },
            [this](mon_stop) {
                running_ = false;
            }
        };
    }

private:
    orch::ThreadOrchestrator& orchestrator_;
    EventBus& bus_;
    bool running_;
};

inline auto make_system_monitor_actor(
    caf::actor_system& sys,
    orch::ThreadOrchestrator& orchestrator,
    EventBus& bus
) {
    auto actor = sys.spawn<system_monitor_actor_impl>(
        std::ref(orchestrator), std::ref(bus)
    );
    caf::anon_send(actor, mon_start{});
    return actor;
}

}
