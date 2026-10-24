#include "qmi_state_machine.hpp"
#include "qmi_modem_controller.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdlib>

namespace odc {
namespace qmi {

void QmiStateMachine::set_transitions(const Transition* table, std::size_t count) {
    table_ = table;
    table_size_ = count;
}

bool QmiStateMachine::process(SmEvent event, QmiModemController& ctrl) {
    QmiState cur = state_.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < table_size_; ++i) {
        auto& t = table_[i];
        if (t.from != cur || t.event != event) continue;
        if (t.guard && !t.guard(ctrl)) continue;

        SPDLOG_INFO("qmi: transition {} + {} -> {}",
                     to_string_view(cur), sm_event_name(event), to_string_view(t.to));
        state_.store(t.to, std::memory_order_release);
        if (t.action) t.action(ctrl);
        return true;
    }
    SPDLOG_DEBUG("qmi: no transition for {} + {}", to_string_view(cur), sm_event_name(event));
    return false;
}

QmiBackoff::~QmiBackoff() { cancel(); }

void QmiBackoff::configure(uint32_t base, uint32_t max, uint32_t jitter, int retries) {
    base_ms_ = base; max_ms_ = max; jitter_ms_ = jitter; max_retries_ = retries;
}

uint32_t QmiBackoff::current_delay_ms() const {
    uint32_t delay = base_ms_;
    for (int i = 0; i < attempt_ && delay < max_ms_; ++i)
        delay = std::min(delay * 2, max_ms_);
    if (jitter_ms_ > 0)
        delay += static_cast<uint32_t>(std::rand()) % (jitter_ms_ * 2) - jitter_ms_;
    return delay;
}

void QmiBackoff::start(Callback cb) {
    cancel();
    cb_ = std::move(cb);
    uint32_t delay = current_delay_ms();
    ++attempt_;
    SPDLOG_INFO("qmi: backoff attempt {}, delay {}ms", attempt_, delay);
    timer_id_ = g_timeout_add(delay, [](gpointer data) -> gboolean {
        auto* self = static_cast<QmiBackoff*>(data);
        self->timer_id_ = 0;
        if (self->cb_) self->cb_();
        return G_SOURCE_REMOVE;
    }, this);
}

void QmiBackoff::reset() { attempt_ = 0; cancel(); }

void QmiBackoff::cancel() {
    if (timer_id_) { g_source_remove(timer_id_); timer_id_ = 0; }
}

} // namespace qmi
} // namespace odc
