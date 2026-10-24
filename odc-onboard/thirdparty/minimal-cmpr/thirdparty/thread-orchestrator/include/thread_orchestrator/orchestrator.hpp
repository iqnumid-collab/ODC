#pragma once

#include "cancellation_token.hpp"
#include "future.hpp"
#include "metrics.hpp"
#include "detail/control_state.hpp"
#include "detail/task_queue.hpp"
#include "detail/unique_function.hpp"
#include "detail/worker_pool.hpp"

#include <functional>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

namespace orch {

struct Config {
    std::size_t initial_threads = 0; // 0 = hardware_concurrency
    std::size_t min_threads     = 1;
    std::size_t max_threads     = 0; // 0 = 2 * hardware_concurrency
    std::function<void(std::exception_ptr)> on_error;
};

class ThreadOrchestrator {
public:
    explicit ThreadOrchestrator(Config cfg = {}) {
        auto hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 2;
        if (cfg.initial_threads == 0) cfg.initial_threads = hw;
        if (cfg.max_threads == 0)     cfg.max_threads = hw * 2;
        if (cfg.min_threads > cfg.max_threads) cfg.min_threads = cfg.max_threads;
        cfg.initial_threads = std::clamp(cfg.initial_threads, cfg.min_threads, cfg.max_threads);

        ctrl_.min_workers = cfg.min_threads;
        ctrl_.max_workers = cfg.max_threads;
        on_error_ = cfg.on_error;

        pool_ = std::make_unique<detail::WorkerPool>(
            queue_, ctrl_, metrics_, cfg.initial_threads);
    }

    ~ThreadOrchestrator() { shutdown(); }

    ThreadOrchestrator(const ThreadOrchestrator&) = delete;
    ThreadOrchestrator& operator=(const ThreadOrchestrator&) = delete;

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) {
        using R = std::invoke_result_t<std::decay_t<F>, CancellationToken, std::decay_t<Args>...>;
        return submit_impl<R>(std::forward<F>(f), std::forward<Args>(args)...);
    }

    template <typename F>
    auto submit_simple(F&& f) {
        using R = std::invoke_result_t<std::decay_t<F>>;
        auto state = std::make_shared<detail::FutureState<R>>();
        state->pool_submit = pool_submit_fn();
        auto err_cb = on_error_; // copy for capture

        auto task = detail::UniqueFunction(
            [state, func = std::forward<F>(f), err_cb]() mutable {
                if (state->csource.is_cancelled()) {
                    state->set_exception(std::make_exception_ptr(CancelledException{}));
                    return;
                }
                try {
                    if constexpr (std::is_void_v<R>) {
                        func();
                        state->set_value();
                    } else {
                        state->set_value(func());
                    }
                } catch (...) {
                    auto ep = std::current_exception();
                    state->set_exception(ep);
                    if (err_cb) { try { err_cb(ep); } catch (...) {} }
                }
            });

        metrics_.pending_tasks.fetch_add(1, std::memory_order_relaxed);
        queue_.push(std::move(task));

        OrchestratorFuture<R> future;
        future.state_ = state;
        return future;
    }

    void pause()  { ctrl_.pause(); }
    void resume() { ctrl_.resume(); queue_.notify_all(); }

    void resize(std::size_t n) {
        if (pool_) pool_->resize(n);
    }

    [[nodiscard]] MetricsSnapshot get_metrics() const { return metrics_.snapshot(); }

    void shutdown() {
        if (pool_) {
            pool_->shutdown();
            auto remaining = queue_.drain();
            for (auto& t : remaining) {
                metrics_.pending_tasks.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    }

private:
    detail::TaskQueue queue_;
    detail::ControlState ctrl_;
    Metrics metrics_;
    std::unique_ptr<detail::WorkerPool> pool_;
    std::function<void(std::exception_ptr)> on_error_;

    detail::PoolSubmitFn pool_submit_fn() {
        return [this](detail::UniqueFunction task) {
            metrics_.pending_tasks.fetch_add(1, std::memory_order_relaxed);
            queue_.push(std::move(task));
        };
    }

    template <typename R, typename F, typename... Args>
    auto submit_impl(F&& f, Args&&... args) {
        auto state = std::make_shared<detail::FutureState<R>>();
        state->pool_submit = pool_submit_fn();
        auto err_cb = on_error_;

        auto task = detail::UniqueFunction(
            [state, func = std::forward<F>(f), err_cb, ...captured_args = std::forward<Args>(args)]() mutable {
                if (state->csource.is_cancelled()) {
                    state->set_exception(std::make_exception_ptr(CancelledException{}));
                    return;
                }
                try {
                    auto token = state->csource.token();
                    if constexpr (std::is_void_v<R>) {
                        func(token, std::move(captured_args)...);
                        state->set_value();
                    } else {
                        state->set_value(func(token, std::move(captured_args)...));
                    }
                } catch (...) {
                    auto ep = std::current_exception();
                    state->set_exception(ep);
                    if (err_cb) { try { err_cb(ep); } catch (...) {} }
                }
            });

        metrics_.pending_tasks.fetch_add(1, std::memory_order_relaxed);
        queue_.push(std::move(task));

        OrchestratorFuture<R> future;
        future.state_ = state;
        return future;
    }
};

} // namespace orch
