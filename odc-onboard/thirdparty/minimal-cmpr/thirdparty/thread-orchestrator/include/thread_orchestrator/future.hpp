#pragma once

#include "cancellation_token.hpp"
#include "detail/unique_function.hpp"

#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace orch {

struct CancelledException : std::runtime_error {
    CancelledException() : std::runtime_error("task was cancelled") {}
};

enum class ExecutionPolicy { Pool, Inline };

namespace detail {
using PoolSubmitFn = std::function<void(UniqueFunction)>;

// Shared state for OrchestratorFuture<T>. Holds the result or exception,
// a cancellation source, an optional continuation, and a pool-submit hook.
template <typename T>
struct FutureState {
    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;

    std::optional<T> value;
    std::exception_ptr exception;

    CancellationSource csource;
    PoolSubmitFn pool_submit; // set by orchestrator for Pool continuations

    // Continuation: called once when result is set.
    std::function<void()> continuation;

    void set_value(T v) {
        std::function<void()> cont;
        {
            std::lock_guard lk(mtx);
            value.emplace(std::move(v));
            ready = true;
            cont = std::move(continuation);
        }
        cv.notify_all();
        if (cont) cont();
    }

    void set_exception(std::exception_ptr ep) {
        std::function<void()> cont;
        {
            std::lock_guard lk(mtx);
            exception = ep;
            ready = true;
            cont = std::move(continuation);
        }
        cv.notify_all();
        if (cont) cont();
    }

    T get() {
        std::unique_lock lk(mtx);
        cv.wait(lk, [&] { return ready; });
        if (exception) std::rethrow_exception(exception);
        return std::move(*value);
    }
};

// Specialization for void
template <>
struct FutureState<void> {
    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;
    bool has_value = false;
    std::exception_ptr exception;

    CancellationSource csource;
    PoolSubmitFn pool_submit;
    std::function<void()> continuation;

    void set_value() {
        std::function<void()> cont;
        {
            std::lock_guard lk(mtx);
            has_value = true;
            ready = true;
            cont = std::move(continuation);
        }
        cv.notify_all();
        if (cont) cont();
    }

    void set_exception(std::exception_ptr ep) {
        std::function<void()> cont;
        {
            std::lock_guard lk(mtx);
            exception = ep;
            ready = true;
            cont = std::move(continuation);
        }
        cv.notify_all();
        if (cont) cont();
    }

    void get() {
        std::unique_lock lk(mtx);
        cv.wait(lk, [&] { return ready; });
        if (exception) std::rethrow_exception(exception);
    }
};

} // namespace detail

class ThreadOrchestrator; // forward

namespace detail {
// Deduce the return type of a continuation F applied to T
template <typename T, typename F>
struct then_result {
    using type = std::invoke_result_t<F, T>;
};
template <typename F>
struct then_result<void, F> {
    using type = std::invoke_result_t<F>;
};
} // namespace detail

template <typename T>
class OrchestratorFuture {
public:
    OrchestratorFuture() = default;

    // Block until result is ready, then return it.
    T get() { return state_->get(); }

    void cancel() { state_->csource.cancel(); }

    [[nodiscard]] bool is_cancelled() const { return state_->csource.is_cancelled(); }

    [[nodiscard]] bool is_ready() const {
        std::lock_guard lk(state_->mtx);
        return state_->ready;
    }

    // Register a continuation. Returns a future for the continuation's result.
    template <typename F>
    auto then(F&& f, ExecutionPolicy policy = ExecutionPolicy::Pool) {
        using R = typename detail::then_result<T, std::decay_t<F>>::type;
        auto child = std::make_shared<detail::FutureState<R>>();
        child->csource = state_->csource; // share cancellation chain

        auto parent = state_;
        auto wrapper = [parent, child, func = std::forward<F>(f)]() mutable {
            try {
                if (parent->exception) {
                    child->set_exception(parent->exception);
                    return;
                }
                if constexpr (std::is_void_v<T>) {
                    if constexpr (std::is_void_v<R>) {
                        func();
                        child->set_value();
                    } else {
                        child->set_value(func());
                    }
                } else {
                    if constexpr (std::is_void_v<R>) {
                        func(std::move(*parent->value));
                        child->set_value();
                    } else {
                        child->set_value(func(std::move(*parent->value)));
                    }
                }
            } catch (...) {
                child->set_exception(std::current_exception());
            }
        };

        auto dispatch = [policy, pool = state_->pool_submit](detail::UniqueFunction task) {
            if (policy == ExecutionPolicy::Inline || !pool) {
                task();
            } else {
                pool(std::move(task));
            }
        };

        // Wire the continuation under lock
        {
            std::lock_guard lk(state_->mtx);
            child->pool_submit = state_->pool_submit;
            if (state_->ready) {
                // Already done — fire now
                dispatch(detail::UniqueFunction(std::move(wrapper)));
            } else {
                state_->continuation = [dispatch, w = std::move(wrapper)]() mutable {
                    dispatch(detail::UniqueFunction(std::move(w)));
                };
            }
        }

        OrchestratorFuture<R> result;
        result.state_ = child;
        return result;
    }

private:
    template <typename U> friend class OrchestratorFuture;
    friend class ThreadOrchestrator;

    std::shared_ptr<detail::FutureState<T>> state_;
};

} // namespace orch
