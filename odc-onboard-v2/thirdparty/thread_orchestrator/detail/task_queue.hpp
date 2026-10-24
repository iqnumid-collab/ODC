#pragma once

#include "unique_function.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

namespace orch::detail {

class TaskQueue {
public:
    void push(UniqueFunction task) {
        {
            std::lock_guard lk(mtx_);
            tasks_.push_back(std::move(task));
        }
        cv_.notify_one();
    }

    // Blocking pop respecting global stop, pause, and per-worker local stop.
    bool pop_or_stop(UniqueFunction& out,
                     const std::atomic<bool>& stop,
                     const std::atomic<bool>& paused,
                     const std::atomic<bool>& local_stop) {
        std::unique_lock lk(mtx_);
        // Use timed wait to periodically re-check local_stop (which the CV doesn't know about).
        while (true) {
            if (stop.load(std::memory_order_acquire) || local_stop.load(std::memory_order_acquire))
                return false;
            if (!paused.load(std::memory_order_acquire) && !tasks_.empty())
                break;
            cv_.wait_for(lk, std::chrono::milliseconds(50));
        }
        out = std::move(tasks_.front());
        tasks_.pop_front();
        return true;
    }

    // Drain all tasks. Returns them so callers can cancel their futures.
    std::vector<UniqueFunction> drain() {
        std::lock_guard lk(mtx_);
        std::vector<UniqueFunction> out;
        out.reserve(tasks_.size());
        while (!tasks_.empty()) {
            out.push_back(std::move(tasks_.front()));
            tasks_.pop_front();
        }
        return out;
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lk(mtx_);
        return tasks_.size();
    }

    void notify_all() { cv_.notify_all(); }

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<UniqueFunction> tasks_;
};

} // namespace orch::detail
