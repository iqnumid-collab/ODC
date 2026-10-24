#pragma once

#include <atomic>
#include <memory>

namespace orch {

class CancellationSource;

class CancellationToken {
public:
    CancellationToken() noexcept = default;

    [[nodiscard]] bool is_cancelled() const noexcept {
        return state_ && state_->load(std::memory_order_acquire);
    }

private:
    friend class CancellationSource;
    explicit CancellationToken(std::shared_ptr<std::atomic<bool>> s) : state_(std::move(s)) {}
    std::shared_ptr<std::atomic<bool>> state_;
};

class CancellationSource {
public:
    CancellationSource() : state_(std::make_shared<std::atomic<bool>>(false)) {}

    void cancel() noexcept { state_->store(true, std::memory_order_release); }

    [[nodiscard]] bool is_cancelled() const noexcept {
        return state_->load(std::memory_order_acquire);
    }

    [[nodiscard]] CancellationToken token() const noexcept {
        return CancellationToken{state_};
    }

private:
    std::shared_ptr<std::atomic<bool>> state_;
};

} // namespace orch
