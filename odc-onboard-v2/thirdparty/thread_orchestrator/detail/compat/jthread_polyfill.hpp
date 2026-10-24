#pragma once

// Detect native jthread/stop_token support
#include <version>
#include <thread>
#include <atomic>
#include <memory>
#include <type_traits>

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
#define ORCH_HAS_JTHREAD 1
#else
#define ORCH_HAS_JTHREAD 0
#endif

namespace orch::compat {

#if ORCH_HAS_JTHREAD

using stop_token  = std::stop_token;
using stop_source = std::stop_source;
using jthread     = std::jthread;

template <typename Callback>
using stop_callback = std::stop_callback<Callback>;

#else

// Minimal polyfill for platforms without jthread (e.g. libc++)

class stop_source;
class stop_token;

namespace detail {
struct stop_state {
    std::atomic<bool> stopped{false};
};
} // namespace detail

class stop_token {
public:
    stop_token() noexcept = default;

    [[nodiscard]] bool stop_requested() const noexcept {
        return state_ && state_->stopped.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool stop_possible() const noexcept {
        return state_ != nullptr;
    }

    friend bool operator==(const stop_token& a, const stop_token& b) noexcept {
        return a.state_ == b.state_;
    }

private:
    friend class stop_source;
    explicit stop_token(std::shared_ptr<detail::stop_state> s) : state_(std::move(s)) {}
    std::shared_ptr<detail::stop_state> state_;
};

class stop_source {
public:
    stop_source() : state_(std::make_shared<detail::stop_state>()) {}

    [[nodiscard]] stop_token get_token() const noexcept {
        return stop_token{state_};
    }

    bool request_stop() noexcept {
        if (state_) {
            bool expected = false;
            return state_->stopped.compare_exchange_strong(expected, true, std::memory_order_release);
        }
        return false;
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return state_ && state_->stopped.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool stop_possible() const noexcept {
        return state_ != nullptr;
    }

private:
    std::shared_ptr<detail::stop_state> state_;
};

// Minimal stop_callback: registers a callback invoked on request_stop.
// For simplicity, this only supports calling the callback if stop is already
// requested at construction time, or polling. Full async callback registration
// would require a more complex stop_state. For our use case (condition_variable
// wakeup), we use a simpler approach in the task queue.
template <typename Callback>
class stop_callback {
public:
    template <typename C>
    stop_callback(const stop_token& token, C&& cb)
        : cb_(std::forward<C>(cb)) {
        if (token.stop_requested()) {
            cb_();
        }
    }
    ~stop_callback() = default;
    stop_callback(const stop_callback&) = delete;
    stop_callback& operator=(const stop_callback&) = delete;

private:
    Callback cb_;
};

class jthread {
public:
    jthread() noexcept = default;

    template <typename F, typename... Args>
        requires std::is_invocable_v<std::decay_t<F>, stop_token, std::decay_t<Args>...>
    explicit jthread(F&& f, Args&&... args)
        : ssource_{}
        , thread_(std::forward<F>(f), ssource_.get_token(), std::forward<Args>(args)...) {}

    ~jthread() {
        if (joinable()) {
            request_stop();
            join();
        }
    }

    jthread(jthread&&) noexcept = default;
    jthread& operator=(jthread&& other) noexcept {
        if (this != &other) {
            if (joinable()) {
                request_stop();
                join();
            }
            ssource_ = std::move(other.ssource_);
            thread_  = std::move(other.thread_);
        }
        return *this;
    }

    jthread(const jthread&) = delete;
    jthread& operator=(const jthread&) = delete;

    [[nodiscard]] bool joinable() const noexcept { return thread_.joinable(); }
    void join() { thread_.join(); }

    [[nodiscard]] stop_token get_stop_token() const noexcept { return ssource_.get_token(); }
    [[nodiscard]] stop_source& get_stop_source() noexcept { return ssource_; }

    bool request_stop() noexcept { return ssource_.request_stop(); }

    [[nodiscard]] std::thread::id get_id() const noexcept { return thread_.get_id(); }

private:
    stop_source ssource_;
    std::thread thread_;
};

#endif // ORCH_HAS_JTHREAD

} // namespace orch::compat
