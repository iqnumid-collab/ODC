#include "state_machine.h"
#include "log.h"

namespace uqmi {

void StateMachine::set_transitions(const Transition* table, size_t count) {
    table_ = table;
    table_size_ = count;
}

bool StateMachine::process(Event event, Controller& ctx) {
    State cur = state_.load(std::memory_order_acquire);
    for (size_t i = 0; i < table_size_; ++i) {
        auto& t = table_[i];
        if (t.from != cur || t.event != event) continue;
        if (t.guard && !t.guard(ctx)) continue;

        LOG_I("transition: %s + %s -> %s",
              state_name(cur), event_name(event), state_name(t.to));
        state_.store(t.to, std::memory_order_release);
        if (t.action) t.action(ctx);
        return true;
    }
    LOG_D("no transition for %s + %s", state_name(cur), event_name(event));
    return false;
}

} // namespace uqmi
