#include "backoff.h"
#include "log.h"
#include <algorithm>
#include <cstdlib>

namespace uqmi {

void Backoff::configure(uint32_t base, uint32_t max, uint32_t jitter, int retries) {
    base_ms_ = base; max_ms_ = max; jitter_ms_ = jitter; max_retries_ = retries;
}

uint32_t Backoff::current_delay_ms() const {
    uint32_t delay = base_ms_;
    for (int i = 0; i < attempt_ && delay < max_ms_; ++i)
        delay = std::min(delay * 2, max_ms_);
    if (jitter_ms_ > 0)
        delay += static_cast<uint32_t>(std::rand()) % (jitter_ms_ * 2) - jitter_ms_;
    return delay;
}

void Backoff::start(Callback cb) {
    cancel();
    cb_ = std::move(cb);
    uint32_t delay = current_delay_ms();
    ++attempt_;
    LOG_I("backoff: attempt %d, delay %u ms", attempt_, delay);
    timer_id_ = g_timeout_add(delay, [](gpointer data) -> gboolean {
        auto* self = static_cast<Backoff*>(data);
        self->timer_id_ = 0;
        if (self->cb_) self->cb_();
        return G_SOURCE_REMOVE;
    }, this);
}

void Backoff::reset() { attempt_ = 0; cancel(); }

void Backoff::cancel() {
    if (timer_id_) { g_source_remove(timer_id_); timer_id_ = 0; }
}

} // namespace uqmi
