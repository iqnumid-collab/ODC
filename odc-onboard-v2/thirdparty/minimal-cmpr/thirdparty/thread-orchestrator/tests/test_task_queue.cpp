#include <thread_orchestrator/detail/task_queue.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <thread>

using orch::detail::TaskQueue;
using orch::detail::UniqueFunction;

TEST(TaskQueue, PushPop) {
    TaskQueue q;
    std::atomic<bool> stop{false}, paused{false}, local_stop{false};
    int x = 0;
    q.push(UniqueFunction([&x] { x = 42; }));
    EXPECT_EQ(q.size(), 1u);

    UniqueFunction task;
    EXPECT_TRUE(q.pop_or_stop(task, stop, paused, local_stop));
    task();
    EXPECT_EQ(x, 42);
    EXPECT_EQ(q.size(), 0u);
}

TEST(TaskQueue, StopUnblocks) {
    TaskQueue q;
    std::atomic<bool> stop{false}, paused{false}, local_stop{false};

    std::thread t([&] {
        UniqueFunction task;
        q.pop_or_stop(task, stop, paused, local_stop);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true);
    q.notify_all();
    t.join(); // should not hang
}

TEST(TaskQueue, LocalStopUnblocks) {
    TaskQueue q;
    std::atomic<bool> stop{false}, paused{false}, local_stop{false};

    std::thread t([&] {
        UniqueFunction task;
        q.pop_or_stop(task, stop, paused, local_stop);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    local_stop.store(true);
    q.notify_all();
    t.join();
}

TEST(TaskQueue, PauseBlocksConsumer) {
    TaskQueue q;
    std::atomic<bool> stop{false}, paused{true}, local_stop{false};
    q.push(UniqueFunction([] {}));

    std::atomic<bool> popped{false};
    std::thread t([&] {
        UniqueFunction task;
        q.pop_or_stop(task, stop, paused, local_stop);
        popped.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(popped.load());

    paused.store(false);
    q.notify_all();
    t.join();
    EXPECT_TRUE(popped.load());
}

TEST(TaskQueue, Drain) {
    TaskQueue q;
    q.push(UniqueFunction([] {}));
    q.push(UniqueFunction([] {}));
    auto drained = q.drain();
    EXPECT_EQ(drained.size(), 2u);
    EXPECT_EQ(q.size(), 0u);
}

TEST(TaskQueue, MultiProducer) {
    TaskQueue q;
    std::atomic<int> count{0};
    std::atomic<bool> stop{false}, paused{false}, local_stop{false};

    constexpr int N = 100;
    std::thread producer1([&] {
        for (int i = 0; i < N; ++i)
            q.push(UniqueFunction([&count] { count.fetch_add(1); }));
    });
    std::thread producer2([&] {
        for (int i = 0; i < N; ++i)
            q.push(UniqueFunction([&count] { count.fetch_add(1); }));
    });

    producer1.join();
    producer2.join();

    // Consume all
    UniqueFunction task;
    while (q.size() > 0) {
        if (q.pop_or_stop(task, stop, paused, local_stop))
            task();
    }
    EXPECT_EQ(count.load(), 2 * N);
}
