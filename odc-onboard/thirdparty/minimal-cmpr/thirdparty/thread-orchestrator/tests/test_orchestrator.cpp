#include <thread_orchestrator/thread_orchestrator.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

TEST(Orchestrator, SubmitSimpleAndGet) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 4});
    auto f = pool.submit_simple([] { return 42; });
    EXPECT_EQ(f.get(), 42);
}

TEST(Orchestrator, SubmitWithToken) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 4});
    auto f = pool.submit([](orch::CancellationToken) { return std::string("hello"); });
    EXPECT_EQ(f.get(), "hello");
}

TEST(Orchestrator, SubmitVoid) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 4});
    std::atomic<int> x{0};
    auto f = pool.submit_simple([&x] { x.store(1); });
    f.get();
    EXPECT_EQ(x.load(), 1);
}

TEST(Orchestrator, ExceptionPropagation) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 4});
    auto f = pool.submit_simple([]() -> int { throw std::runtime_error("fail"); });
    EXPECT_THROW(f.get(), std::runtime_error);
}

TEST(Orchestrator, OnErrorCallback) {
    std::atomic<bool> called{false};
    orch::Config cfg{.initial_threads = 2, .min_threads = 1, .max_threads = 4};
    cfg.on_error = [&called](std::exception_ptr) { called.store(true); };
    orch::ThreadOrchestrator pool(cfg);

    auto f = pool.submit_simple([]() -> int { throw std::runtime_error("oops"); });
    try { f.get(); } catch (...) {}
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(called.load());
}

TEST(Orchestrator, ThenChainPool) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 4});
    auto f = pool.submit_simple([] { return 10; })
        .then([](int v) { return v * 2; })
        .then([](int v) { return v + 1; });
    EXPECT_EQ(f.get(), 21);
}

TEST(Orchestrator, ThenChainInline) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 4});
    auto f = pool.submit_simple([] { return 5; })
        .then([](int v) { return v * 3; }, orch::ExecutionPolicy::Inline);
    EXPECT_EQ(f.get(), 15);
}

TEST(Orchestrator, CancelPendingTask) {
    orch::ThreadOrchestrator pool({.initial_threads = 1, .min_threads = 1, .max_threads = 1});
    // Block the single worker
    std::atomic<bool> release{false};
    pool.submit_simple([&release] {
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // This task should be pending in the queue
    auto f = pool.submit_simple([] { return 99; });
    f.cancel();
    release.store(true);

    EXPECT_THROW(f.get(), orch::CancelledException);
}

TEST(Orchestrator, CooperativeCancellation) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 4});
    auto f = pool.submit([](orch::CancellationToken token) {
        while (!token.is_cancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return std::string("cancelled");
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    f.cancel();
    EXPECT_EQ(f.get(), "cancelled");
}

TEST(Orchestrator, PauseResume) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 4});
    pool.pause();

    std::atomic<bool> ran{false};
    auto f = pool.submit_simple([&ran] { ran.store(true); return 1; });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(ran.load());

    pool.resume();
    EXPECT_EQ(f.get(), 1);
    EXPECT_TRUE(ran.load());
}

TEST(Orchestrator, Resize) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 8});
    auto m1 = pool.get_metrics();
    EXPECT_EQ(m1.worker_count, 2u);

    pool.resize(4);
    auto m2 = pool.get_metrics();
    EXPECT_EQ(m2.worker_count, 4u);

    pool.resize(1);
    // Give workers time to exit
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto m3 = pool.get_metrics();
    EXPECT_EQ(m3.worker_count, 1u);
}

TEST(Orchestrator, ResizeClampsBounds) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 2, .max_threads = 4});
    pool.resize(1); // should clamp to 2
    auto m = pool.get_metrics();
    EXPECT_GE(m.worker_count, 2u);

    pool.resize(100); // should clamp to 4
    m = pool.get_metrics();
    EXPECT_LE(m.worker_count, 4u);
}

TEST(Orchestrator, Metrics) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 4});
    std::vector<orch::OrchestratorFuture<int>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.submit_simple([i] { return i; }));
    }
    for (auto& f : futures) f.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto m = pool.get_metrics();
    EXPECT_GE(m.completed_tasks, 10u);
}

TEST(Orchestrator, CleanShutdown) {
    auto pool = std::make_unique<orch::ThreadOrchestrator>(
        orch::Config{.initial_threads = 4, .min_threads = 1, .max_threads = 8});
    for (int i = 0; i < 20; ++i) {
        pool->submit_simple([i] {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return i;
        });
    }
    pool.reset(); // destructor calls shutdown — should not hang
}
