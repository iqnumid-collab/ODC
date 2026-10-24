#include "graph/graph_builder.hpp"

#include "core/event_bus.hpp"
#include "core/vpn_monitor.hpp"
#include "graph/components/cellular_wan_component.hpp"
#include "graph/components/compression_capabilities_component.hpp"
#include "graph/components/mav_extender_component.hpp"
#include "graph/components/mav_router_component.hpp"
#include "graph/components/odc_component.hpp"
#include "graph/components/rtsp_compressor_component.hpp"
#include "graph/components/vpn_tunnel_component.hpp"
#include "rtsp/compression_capabilities.hpp"

namespace odc::graph {

GraphBuilder::GraphBuilder(
    EventBus& bus,
    orch::ThreadOrchestrator& orchestrator,
    VpnInterfaceMonitor* vpn_monitor,
    std::shared_ptr<CompressionCapabilities> compression_caps)
    : bus_(bus),
      orchestrator_(orchestrator),
      vpn_monitor_(vpn_monitor),
      compression_caps_(std::move(compression_caps)) {}

std::unique_ptr<OdcComponent> GraphBuilder::build(const GraphComponentSpec& spec) {
    if (spec.type == "cellular.qmi") {
        auto component = std::make_unique<CellularWanComponent>(bus_, spec.id, spec.config);
        component->set_runtime_enabled(spec.enabled);
        return component;
    }

    if (spec.type == "vpn.wireguard" || spec.type == "vpn.openvpn") {
        auto component = std::make_unique<VpnTunnelComponent>(
            bus_, vpn_monitor_, spec.id, spec.type, spec.config);
        component->set_runtime_enabled(spec.enabled);
        return component;
    }

    if (spec.type == "mavlink.router") {
        auto component = std::make_unique<MavRouterComponent>(bus_, orchestrator_, spec.id, spec.config);
        component->set_runtime_enabled(spec.enabled);
        return component;
    }

    if (spec.type == "mavlink.extender") {
        auto component = std::make_unique<MavExtenderComponent>(bus_, orchestrator_, spec.id, spec.config);
        component->set_runtime_enabled(spec.enabled);
        return component;
    }

    if (spec.type == "compression.capabilities") {
        auto caps = compression_caps_ ? compression_caps_
                                      : std::make_shared<CompressionCapabilities>();
        auto component = std::make_unique<CompressionCapabilitiesComponent>(
            std::move(caps), spec.id, spec.config);
        component->set_runtime_enabled(spec.enabled);
        return component;
    }

    if (spec.type == "rtsp.compressor") {
        auto component = std::make_unique<RtspCompressorComponent>(
            bus_, orchestrator_, compression_caps_, spec.id, spec.config);
        component->set_runtime_enabled(spec.enabled);
        return component;
    }

    return nullptr;
}

} // namespace odc::graph
