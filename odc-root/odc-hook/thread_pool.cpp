#include "thread_pool.hpp"
#include "odc/logging.hpp"

namespace odc::hook {

ThreadPool::ThreadPool(size_t num_threads, EventBus& bus, EventHandler handler,
                       std::chrono::milliseconds timeout)
    : num_threads_(num_threads == 0 ? std::thread::hardware_concurrency() : num_threads)
    , bus_(bus)
    , handler_(std::move(handler))
    , timeout_(timeout) {
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::start() {
    if (running_) return;

    running_ = true;
    auto logger = get_logger("thread_pool");

    for (size_t i = 0; i < num_threads_; ++i) {
        threads_.emplace_back([this, i]() {
            auto logger = get_logger("thread_pool");
            SPDLOG_LOGGER_DEBUG(logger, "Worker {} started", i);
            worker_loop();
            SPDLOG_LOGGER_DEBUG(logger, "Worker {} stopped", i);
        });
    }

    SPDLOG_LOGGER_INFO(logger, "Thread pool started with {} threads", num_threads_);
}

void ThreadPool::stop() {
    if (!running_) return;
    running_ = false;

    auto logger = get_logger("thread_pool");
    SPDLOG_LOGGER_INFO(logger, "Stopping thread pool (active: {})",
                       active_count_.load());

    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();

    SPDLOG_LOGGER_INFO(logger, "Thread pool stopped");
}

void ThreadPool::wait() {
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
}

void ThreadPool::worker_loop() {
    while (running_.load(std::memory_order_acquire)) {
        nlohmann::json event = bus_.consume(timeout_);

        if (event.is_null()) {
            if (!running_.load(std::memory_order_acquire)) break;
            continue;
        }

        active_count_.fetch_add(1, std::memory_order_relaxed);

        try {
            handler_(event);
        } catch (const std::exception& e) {
            auto logger = get_logger("thread_pool");
            SPDLOG_LOGGER_ERROR(logger, "Event handler exception: {}", e.what());
        } catch (...) {
            auto logger = get_logger("thread_pool");
            SPDLOG_LOGGER_ERROR(logger, "Event handler unknown exception");
        }

        active_count_.fetch_sub(1, std::memory_order_relaxed);
    }
}

} // namespace odc::hook
