#pragma once

#include <atomic>
#include <cstdint>

namespace orch {

// Snapshot of pool metrics (plain values, safe to copy).
struct MetricsSnapshot {
    uint64_t active_tasks    = 0;
    uint64_t pending_tasks   = 0;
    uint64_t completed_tasks = 0;
    std::size_t worker_count = 0;
};

// Live atomic counters, owned by the orchestrator.
struct Metrics {
    std::atomic<uint64_t> active_tasks{0};
    std::atomic<uint64_t> pending_tasks{0};
    std::atomic<uint64_t> completed_tasks{0};
    std::atomic<std::size_t> worker_count{0};

    [[nodiscard]] MetricsSnapshot snapshot() const noexcept {
        return {
            active_tasks.load(std::memory_order_relaxed),
            pending_tasks.load(std::memory_order_relaxed),
            completed_tasks.load(std::memory_order_relaxed),
            worker_count.load(std::memory_order_relaxed)
        };
    }
};

} // namespace orch
