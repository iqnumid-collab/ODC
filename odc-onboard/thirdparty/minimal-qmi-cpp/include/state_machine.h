#pragma once
#include "states.h"
#include <atomic>
#include <functional>
#include <vector>

namespace uqmi {

class Controller; // forward

using GuardFn  = std::function<bool(Controller&)>;
using ActionFn = std::function<void(Controller&)>;

struct Transition {
    State    from;
    Event    event;
    State    to;
    GuardFn  guard;   // nullptr = always true
    ActionFn action;  // nullptr = no-op
};

class StateMachine {
public:
    explicit StateMachine(State initial) : state_(initial) {}

    void set_transitions(const Transition* table, size_t count);
    bool process(Event event, Controller& ctx);
    State state() const { return state_.load(std::memory_order_acquire); }

private:
    std::atomic<State> state_;
    const Transition* table_ = nullptr;
    size_t table_size_ = 0;
};

} // namespace uqmi
