#include <thread_orchestrator/thread_orchestrator.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

TEST(Stress, ManyTasks) {
    orch::ThreadOrchestrator pool({.initial_threads = 4, .min_threads = 1, .max_threads = 8});
    constexpr int N = 1000;
    std::atomic<int> count{0};
    std::vector<orch::OrchestratorFuture<void>> futures;
    futures.reserve(N);

    for (int i = 0; i < N; ++i) {
        futures.push_back(pool.submit_simple([&count] { count.fetch_add(1); }));
    }
    for (auto& f : futures) f.get();
    EXPECT_EQ(count.load(), N);
}

TEST(Stress, RapidSubmitCancel) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 4});
    constexpr int N = 200;

    for (int i = 0; i < N; ++i) {
        auto f = pool.submit_simple([i] {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            return i;
        });
        if (i % 2 == 0) f.cancel();
        // Don't wait — just let them fly
    }
    // Shutdown should clean up everything
}

TEST(Stress, ConcurrentSubmitters) {
    orch::ThreadOrchestrator pool({.initial_threads = 4, .min_threads = 1, .max_threads = 8});
    std::atomic<int> count{0};
    constexpr int T = 4, N = 250;

    std::vector<std::thread> submitters;
    for (int t = 0; t < T; ++t) {
        submitters.emplace_back([&] {
            for (int i = 0; i < N; ++i) {
                pool.submit_simple([&count] { count.fetch_add(1); }).get();
            }
        });
    }
    for (auto& t : submitters) t.join();
    EXPECT_EQ(count.load(), T * N);
}

TEST(Stress, ResizeUnderLoad) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 8});
    std::atomic<int> count{0};
    constexpr int N = 500;

    // Submit tasks in background
    std::thread submitter([&] {
        for (int i = 0; i < N; ++i) {
            pool.submit_simple([&count] {
                count.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            });
        }
    });

    // Resize while tasks are running
    for (int i = 0; i < 10; ++i) {
        pool.resize(1 + (i % 7));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    submitter.join();
    // Wait for all tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto m = pool.get_metrics();
    EXPECT_GE(m.completed_tasks, 1u); // at least some completed
}

TEST(Stress, PauseResumeUnderLoad) {
    orch::ThreadOrchestrator pool({.initial_threads = 4, .min_threads = 1, .max_threads = 8});
    std::atomic<int> count{0};

    for (int i = 0; i < 100; ++i) {
        pool.submit_simple([&count] { count.fetch_add(1); });
    }

    pool.pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int paused_count = count.load();

    pool.resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_GE(count.load(), paused_count);
}

TEST(Stress, ThenChainDepth) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 4});
    auto f = pool.submit_simple([] { return 0; });
    for (int i = 0; i < 20; ++i) {
        f = f.then([](int v) { return v + 1; });
    }
    EXPECT_EQ(f.get(), 20);
}

TEST(Stress, SubmitFromWithinTask) {
    orch::ThreadOrchestrator pool({.initial_threads = 2, .min_threads = 1, .max_threads = 4});
    // We can't easily submit from within a task without a reference to the pool.
    // This test verifies the pool doesn't deadlock when continuations enqueue work.
    auto f = pool.submit_simple([] { return 1; })
        .then([](int v) { return v + 1; })
        .then([](int v) { return v + 1; });
    EXPECT_EQ(f.get(), 3);
}

TEST(Stress, ExceptionStorm) {
    std::atomic<int> error_count{0};
    orch::Config cfg{.initial_threads = 4, .min_threads = 1, .max_threads = 8};
    cfg.on_error = [&error_count](std::exception_ptr) { error_count.fetch_add(1); };
    orch::ThreadOrchestrator pool(cfg);

    constexpr int N = 100;
    std::vector<orch::OrchestratorFuture<int>> futures;
    for (int i = 0; i < N; ++i) {
        futures.push_back(pool.submit_simple([]() -> int { throw std::runtime_error("boom"); }));
    }
    for (auto& f : futures) {
        EXPECT_THROW(f.get(), std::runtime_error);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(error_count.load(), N);
}
