#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace odc::hook {

class EventBus {
public:
    explicit EventBus(size_t queue_capacity = 4096);
    ~EventBus() = default;

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    void publish(nlohmann::json event);
    bool try_publish(nlohmann::json event);

    nlohmann::json consume(std::chrono::milliseconds timeout);
    bool try_consume(nlohmann::json& event);

    void shutdown();
    size_t size() const;
    size_t capacity() const { return capacity_; }
    uint64_t published_count() const { return published_count_.load(); }
    uint64_t consumed_count() const { return consumed_count_.load(); }

private:
    std::deque<nlohmann::json> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_producer_;
    std::condition_variable cv_consumer_;
    size_t capacity_;
    std::atomic<uint64_t> published_count_{0};
    std::atomic<uint64_t> consumed_count_{0};
    std::atomic<bool> shutdown_{false};
};

} // namespace odc::hook
