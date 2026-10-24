#pragma once

#include "graph/transport_binding.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace odc {

struct GraphComponentStateEvent {
    std::string component_id;
    std::string component_type;
    odc::graph::ComponentRuntimeState state{odc::graph::ComponentRuntimeState::Stopped};
    std::string detail;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct GraphLinkActivatedEvent {
    std::string link_id;
    std::string from_component;
    std::string to_component;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct GraphLinkDeactivatedEvent {
    std::string link_id;
    std::string reason;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct GraphTriggerFiredEvent {
    std::string trigger_id;
    std::string on_event;
    std::string source_component;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct TransportBindingEvent {
    odc::graph::TransportBinding binding;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct GraphMutationEvent {
    std::string operation;
    std::string detail;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

} // namespace odc
