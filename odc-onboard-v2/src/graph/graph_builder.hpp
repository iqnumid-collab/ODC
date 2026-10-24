#pragma once

#include "graph/graph_schema.hpp"

#include <memory>

namespace orch {
class ThreadOrchestrator;
}

namespace odc {

class EventBus;
class VpnInterfaceMonitor;
class CompressionCapabilities;

namespace graph {

class OdcComponent;

class GraphBuilder {
public:
    GraphBuilder(
        EventBus& bus,
        ::orch::ThreadOrchestrator& orchestrator,
        VpnInterfaceMonitor* vpn_monitor,
        std::shared_ptr<CompressionCapabilities> compression_caps = nullptr);

    std::unique_ptr<OdcComponent> build(const GraphComponentSpec& spec);

private:
    EventBus& bus_;
    ::orch::ThreadOrchestrator& orchestrator_;
    VpnInterfaceMonitor* vpn_monitor_;
    std::shared_ptr<CompressionCapabilities> compression_caps_;
};

} // namespace graph
} // namespace odc
