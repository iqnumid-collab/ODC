#pragma once

#include "core/event.hpp"
#include "thread_orchestrator/orchestrator.hpp"

#include <functional>
#include <memory>
#include <shared_mutex>
#include <vector>
#include <atomic>

namespace odc {

class EventBus {
public:
    using Handler = std::function<void(const Event&)>;
    using SubscriptionId = uint64_t;

    explicit EventBus(orch::ThreadOrchestrator& orchestrator)
        : orchestrator_(orchestrator) {}

    SubscriptionId subscribe(Handler handler) {
        auto id = next_id_.fetch_add(1, std::memory_order_relaxed);
        auto entry = std::make_shared<Entry>(id, std::move(handler));

        std::unique_lock lock(mutex_);
        handlers_.push_back(entry);
        return id;
    }

    void unsubscribe(SubscriptionId id) {
        std::unique_lock lock(mutex_);
        handlers_.erase(
            std::remove_if(handlers_.begin(), handlers_.end(),
                [id](const auto& h) { return h->id == id; }),
            handlers_.end()
        );
    }

    void publish(Event event) {
        std::shared_lock lock(mutex_);
        auto snapshot = handlers_;
        lock.unlock();

        for (const auto& handler : snapshot) {
            if (handler && handler->fn) {
                // Copy event per subscriber — moving once would leave later handlers empty.
                orchestrator_.submit_simple([handler, event]() mutable {
                    handler->fn(event);
                });
            }
        }
    }

    void publish_sync(const Event& event) {
        std::shared_lock lock(mutex_);
        auto snapshot = handlers_;
        lock.unlock();

        for (const auto& handler : snapshot) {
            if (handler && handler->fn) {
                handler->fn(event);
            }
        }
    }

    std::size_t subscriber_count() const {
        std::shared_lock lock(mutex_);
        return handlers_.size();
    }

private:
    struct Entry {
        SubscriptionId id;
        Handler fn;
        Entry(SubscriptionId i, Handler f) : id(i), fn(std::move(f)) {}
    };

    orch::ThreadOrchestrator& orchestrator_;
    mutable std::shared_mutex mutex_;
    std::vector<std::shared_ptr<Entry>> handlers_;
    std::atomic<SubscriptionId> next_id_{0};
};

}
