#pragma once

#include "event_bus.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

namespace odc::hook {

using EventHandler = std::function<void(const nlohmann::json& event)>;

class ThreadPool {
public:
    ThreadPool(size_t num_threads, EventBus& bus, EventHandler handler,
               std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void start();
    void stop();
    void wait();

    size_t thread_count() const { return num_threads_; }
    size_t active_count() const { return active_count_.load(); }

private:
    void worker_loop();

    size_t num_threads_;
    EventBus& bus_;
    EventHandler handler_;
    std::chrono::milliseconds timeout_;
    std::vector<std::thread> threads_;
    std::atomic<size_t> active_count_{0};
    std::atomic<bool> running_{false};
};

} // namespace odc::hook
