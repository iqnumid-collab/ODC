#pragma once
#include <glib.h>
#include <functional>
#include <cstdint>

namespace uqmi {

class Backoff {
public:
    using Callback = std::function<void()>;

    void configure(uint32_t base_ms, uint32_t max_ms, uint32_t jitter_ms, int max_retries);
    void start(Callback cb);
    void reset();
    void cancel();

    int  attempt() const { return attempt_; }
    bool exhausted() const { return max_retries_ > 0 && attempt_ >= max_retries_; }

private:
    uint32_t base_ms_    = 1000;
    uint32_t max_ms_     = 60000;
    uint32_t jitter_ms_  = 500;
    int      max_retries_= 5;
    int      attempt_    = 0;
    guint    timer_id_   = 0;
    Callback cb_;

    uint32_t current_delay_ms() const;
};

} // namespace uqmi
