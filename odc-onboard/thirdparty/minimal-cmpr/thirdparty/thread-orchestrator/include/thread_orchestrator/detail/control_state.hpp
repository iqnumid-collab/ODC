#pragma once

#include <atomic>
#include <cstddef>

namespace orch::detail {

struct ControlState {
    std::atomic<bool> stopped{false};
    std::atomic<bool> paused{false};
    std::size_t min_workers = 1;
    std::size_t max_workers = 0; // 0 = no limit

    void pause()  noexcept { paused.store(true, std::memory_order_release); }
    void resume() noexcept { paused.store(false, std::memory_order_release); }
    void stop()   noexcept { stopped.store(true, std::memory_order_release); }

    [[nodiscard]] bool is_paused()  const noexcept { return paused.load(std::memory_order_acquire); }
    [[nodiscard]] bool is_stopped() const noexcept { return stopped.load(std::memory_order_acquire); }

    [[nodiscard]] std::size_t clamp(std::size_t n) const noexcept {
        if (n < min_workers) n = min_workers;
        if (max_workers > 0 && n > max_workers) n = max_workers;
        return n;
    }
};

} // namespace orch::detail
