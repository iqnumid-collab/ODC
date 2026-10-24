#include "event_bus.hpp"

namespace odc::hook {

EventBus::EventBus(size_t queue_capacity)
    : capacity_(queue_capacity) {
}

void EventBus::publish(nlohmann::json event) {
    if (shutdown_.load(std::memory_order_acquire)) return;

    std::unique_lock lock(mutex_);
    cv_producer_.wait(lock, [this]() {
        return queue_.size() < capacity_ || shutdown_.load(std::memory_order_acquire);
    });

    if (shutdown_.load(std::memory_order_acquire)) return;

    queue_.push_back(std::move(event));
    published_count_.fetch_add(1, std::memory_order_relaxed);
    cv_consumer_.notify_one();
}

bool EventBus::try_publish(nlohmann::json event) {
    if (shutdown_.load(std::memory_order_acquire)) return false;

    std::lock_guard lock(mutex_);
    if (queue_.size() >= capacity_) return false;

    queue_.push_back(std::move(event));
    published_count_.fetch_add(1, std::memory_order_relaxed);
    cv_consumer_.notify_one();
    return true;
}

nlohmann::json EventBus::consume(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    cv_consumer_.wait_for(lock, timeout, [this]() {
        return !queue_.empty() || shutdown_.load(std::memory_order_acquire);
    });

    if (queue_.empty()) {
        return nlohmann::json();
    }

    nlohmann::json event = std::move(queue_.front());
    queue_.pop_front();
    consumed_count_.fetch_add(1, std::memory_order_relaxed);

    cv_producer_.notify_one();
    return event;
}

bool EventBus::try_consume(nlohmann::json& event) {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) return false;

    event = std::move(queue_.front());
    queue_.pop_front();
    consumed_count_.fetch_add(1, std::memory_order_relaxed);

    cv_producer_.notify_one();
    return true;
}

void EventBus::shutdown() {
    shutdown_.store(true, std::memory_order_release);
    cv_consumer_.notify_all();
    cv_producer_.notify_all();
}

size_t EventBus::size() const {
    std::lock_guard lock(mutex_);
    return queue_.size();
}

} // namespace odc::hook
