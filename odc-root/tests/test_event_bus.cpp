#include <catch2/catch_test_macros.hpp>

#include "event_bus.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace odc::hook;
using namespace std::chrono_literals;

// ── T4.1: EventBus unit tests ───────────────────────────────────────────────

TEST_CASE("EventBus: publish and consume single event", "[event_bus]") {
    EventBus bus(16);

    nlohmann::json sent = {{"type", "test"}, {"value", 42}};
    bus.publish(sent);

    auto received = bus.consume(100ms);
    REQUIRE_FALSE(received.is_null());
    REQUIRE(received["type"] == "test");
    REQUIRE(received["value"] == 42);
}

TEST_CASE("EventBus: publish and consume many events", "[event_bus]") {
    EventBus bus(4096);
    const int N = 100;

    for (int i = 0; i < N; ++i) {
        bus.publish({{"seq", i}});
    }

    int count = 0;
    for (int i = 0; i < N; ++i) {
        auto ev = bus.consume(100ms);
        REQUIRE_FALSE(ev.is_null());
        REQUIRE(ev["seq"] == i);
        ++count;
    }
    REQUIRE(count == N);
    REQUIRE(bus.consumed_count() == static_cast<uint64_t>(N));
    REQUIRE(bus.published_count() == static_cast<uint64_t>(N));
}

TEST_CASE("EventBus: try_publish returns false when full", "[event_bus]") {
    const size_t cap = 4;
    EventBus bus(cap);

    for (size_t i = 0; i < cap; ++i) {
        REQUIRE(bus.try_publish({{"n", i}}));
    }
    // Queue is full now
    REQUIRE_FALSE(bus.try_publish({{"n", 999}}));
    REQUIRE(bus.published_count() == cap);
}

TEST_CASE("EventBus: backpressure — publisher blocks when full, consumer unblocks", "[event_bus]") {
    EventBus bus(4);

    // Fill the queue
    for (int i = 0; i < 4; ++i) {
        bus.publish({{"n", i}});
    }

    std::atomic<bool> published{false};

    // Publisher thread will block on publish()
    std::thread producer([&]() {
        bus.publish({{"n", 42}});
        published = true;
    });

    // Give producer time to block
    std::this_thread::sleep_for(100ms);
    REQUIRE_FALSE(published.load());

    // Consumer drains one item, unblocking producer
    auto ev = bus.consume(100ms);
    REQUIRE_FALSE(ev.is_null());
    REQUIRE(ev["n"] == 0);

    // Producer should now have unblocked
    producer.join();
    REQUIRE(published.load());
}

TEST_CASE("EventBus: shutdown blocks publish and consume returns null", "[event_bus]") {
    EventBus bus(16);

    bus.publish({{"a", 1}});
    bus.shutdown();

    // Consume still drains existing events
    auto ev1 = bus.consume(10ms);
    REQUIRE_FALSE(ev1.is_null());
    REQUIRE(ev1["a"] == 1);

    // After drain, consume returns null
    auto ev2 = bus.consume(10ms);
    REQUIRE(ev2.is_null());

    // publish after shutdown is no-op
    bus.publish({{"b", 2}});
    REQUIRE(bus.published_count() == 1);
}

TEST_CASE("EventBus: multi-producer multi-consumer", "[event_bus]") {
    EventBus bus(4096);
    const int producers = 4;
    const int consumers = 4;
    const int per_producer = 250;
    const int total = producers * per_producer;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    std::vector<std::thread> producer_threads;
    for (int p = 0; p < producers; ++p) {
        producer_threads.emplace_back([&bus, &produced, per_producer]() {
            for (int i = 0; i < per_producer; ++i) {
                bus.publish({{"p", i}});
                produced.fetch_add(1);
            }
        });
    }

    std::vector<std::thread> consumer_threads;
    for (int c = 0; c < consumers; ++c) {
        consumer_threads.emplace_back([&bus, &consumed, total]() {
            while (consumed.load() < total) {
                auto ev = bus.consume(50ms);
                if (!ev.is_null()) {
                    consumed.fetch_add(1);
                } else if (bus.published_count() >= static_cast<uint64_t>(total)) {
                    break;
                }
            }
        });
    }

    for (auto& t : producer_threads) t.join();
    for (auto& t : consumer_threads) t.join();

    // Drain any remaining events
    while (true) {
        auto ev = bus.consume(10ms);
        if (ev.is_null()) break;
        consumed.fetch_add(1);
    }

    REQUIRE(produced.load() == total);
    REQUIRE(consumed.load() == total);
    REQUIRE(bus.published_count() == static_cast<uint64_t>(total));
    REQUIRE(bus.consumed_count() == static_cast<uint64_t>(total));
}

TEST_CASE("EventBus: statistics match after all events", "[event_bus]") {
    EventBus bus(64);

    for (int i = 0; i < 50; ++i) {
        bus.publish({{"i", i}});
    }

    int consumed = 0;
    for (int i = 0; i < 50; ++i) {
        auto ev = bus.consume(100ms);
        if (!ev.is_null()) ++consumed;
    }

    REQUIRE(consumed == 50);
    REQUIRE(bus.published_count() == 50);
    REQUIRE(bus.consumed_count() == 50);
    REQUIRE(bus.size() == 0);
}
