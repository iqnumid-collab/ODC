#include <thread_orchestrator/thread_orchestrator.hpp>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

// Use a minimal orchestrator to create futures properly.
class FutureTest : public ::testing::Test {
protected:
    orch::ThreadOrchestrator pool{{.initial_threads = 2, .min_threads = 1, .max_threads = 4}};
};

TEST_F(FutureTest, GetValue) {
    auto f = pool.submit_simple([] { return 42; });
    EXPECT_EQ(f.get(), 42);
}

TEST_F(FutureTest, GetException) {
    auto f = pool.submit_simple([]() -> int { throw std::runtime_error("boom"); });
    EXPECT_THROW(f.get(), std::runtime_error);
}

TEST_F(FutureTest, Cancel) {
    // Block the pool so the task stays pending
    std::atomic<bool> release{false};
    pool.submit_simple([&release] {
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    });
    pool.submit_simple([&release] {
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto f = pool.submit_simple([] { return 99; });
    f.cancel();
    release.store(true);
    EXPECT_THROW(f.get(), orch::CancelledException);
}

TEST_F(FutureTest, ThenInline) {
    auto f = pool.submit_simple([] { return 10; })
        .then([](int v) { return v * 2; }, orch::ExecutionPolicy::Inline);
    EXPECT_EQ(f.get(), 20);
}

TEST_F(FutureTest, ThenAfterReady) {
    auto f = pool.submit_simple([] { return 5; });
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // let it complete
    auto chained = f.then([](int v) { return v + 1; }, orch::ExecutionPolicy::Inline);
    EXPECT_EQ(chained.get(), 6);
}

TEST_F(FutureTest, ThenChain) {
    auto f = pool.submit_simple([] { return 10; })
        .then([](int v) { return v * 2; }, orch::ExecutionPolicy::Inline)
        .then([](int v) { return v + 1; }, orch::ExecutionPolicy::Inline);
    EXPECT_EQ(f.get(), 21);
}

TEST_F(FutureTest, VoidFuture) {
    std::atomic<bool> ran{false};
    auto f = pool.submit_simple([&ran] { ran.store(true); });
    f.get();
    EXPECT_TRUE(ran.load());
}

TEST_F(FutureTest, ThenPoolPolicy) {
    auto f = pool.submit_simple([] { return 10; })
        .then([](int v) { return v * 3; }); // default = Pool
    EXPECT_EQ(f.get(), 30);
}
