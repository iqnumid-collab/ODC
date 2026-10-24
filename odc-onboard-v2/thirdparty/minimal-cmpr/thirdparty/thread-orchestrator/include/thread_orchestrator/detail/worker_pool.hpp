#pragma once

#include "control_state.hpp"
#include "task_queue.hpp"
#include "unique_function.hpp"
#include "../metrics.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace orch::detail {

class WorkerPool {
public:
    using ErrorHandler = std::function<void(std::exception_ptr)>;

    WorkerPool(TaskQueue& queue, ControlState& ctrl, Metrics& metrics, std::size_t count, ErrorHandler on_error = {})
        : queue_(queue), ctrl_(ctrl), metrics_(metrics), on_error_(std::move(on_error)) {
        spawn(count);
    }

    ~WorkerPool() { shutdown(); }

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void resize(std::size_t desired) {
        desired = ctrl_.clamp(desired);
        std::lock_guard lk(pool_mtx_);
        if (desired > workers_.size()) {
            spawn_locked(desired - workers_.size());
        } else if (desired < workers_.size()) {
            shrink_locked(workers_.size() - desired);
        }
    }

    void shutdown() {
        ctrl_.stop();
        queue_.notify_all();
        std::lock_guard lk(pool_mtx_);
        for (auto& w : workers_) {
            if (w.thread.joinable()) w.thread.join();
        }
        workers_.clear();
        metrics_.worker_count.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lk(pool_mtx_);
        return workers_.size();
    }

private:
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> local_stop; // shared_ptr so it's movable

        Worker() : local_stop(std::make_shared<std::atomic<bool>>(false)) {}
    };

    TaskQueue& queue_;
    ControlState& ctrl_;
    Metrics& metrics_;
    ErrorHandler on_error_;
    mutable std::mutex pool_mtx_;
    std::vector<Worker> workers_;

    void spawn(std::size_t n) {
        std::lock_guard lk(pool_mtx_);
        spawn_locked(n);
    }

    void spawn_locked(std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            workers_.emplace_back();
            auto& w = workers_.back();
            auto stop_flag = w.local_stop;
            w.thread = std::thread([this, stop_flag] { worker_loop(*stop_flag); });
        }
        metrics_.worker_count.store(workers_.size(), std::memory_order_relaxed);
    }

    void shrink_locked(std::size_t n) {
        std::size_t count = workers_.size();
        for (std::size_t i = 0; i < n && i < count; ++i) {
            workers_[count - 1 - i].local_stop->store(true, std::memory_order_release);
        }
        queue_.notify_all();

        std::vector<std::thread> to_join;
        auto it = workers_.begin();
        while (it != workers_.end()) {
            if (it->local_stop->load(std::memory_order_acquire)) {
                to_join.push_back(std::move(it->thread));
                it = workers_.erase(it);
            } else {
                ++it;
            }
        }
        metrics_.worker_count.store(workers_.size(), std::memory_order_relaxed);

        for (auto& t : to_join) {
            if (t.joinable()) t.join();
        }
    }

    void worker_loop(std::atomic<bool>& local_stop) {
        while (!ctrl_.is_stopped() && !local_stop.load(std::memory_order_acquire)) {
            UniqueFunction task;
            if (!queue_.pop_or_stop(task, ctrl_.stopped, ctrl_.paused, local_stop))
                break;
            if (!task) continue;

            metrics_.active_tasks.fetch_add(1, std::memory_order_relaxed);
            metrics_.pending_tasks.fetch_sub(1, std::memory_order_relaxed);
            try {
                task();
            } catch (...) {
                if (on_error_) {
                    try { on_error_(std::current_exception()); } catch (...) {}
                }
            }
            metrics_.active_tasks.fetch_sub(1, std::memory_order_relaxed);
            metrics_.completed_tasks.fetch_add(1, std::memory_order_relaxed);
        }
    }
};

} // namespace orch::detail
