#include "graph/trigger_evaluator.hpp"

#include "graph/components/cellular_wan_component.hpp"
#include "graph/components/odc_component.hpp"
#include "graph/link_engine.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace odc::graph {

TriggerEvaluator::TriggerEvaluator(
    const GraphSchema& schema,
    LinkEngine* link_engine,
    std::unordered_map<std::string, OdcComponent*>* components)
    : schema_(schema),
      link_engine_(link_engine),
      components_(components) {}

const GraphLinkSpec* TriggerEvaluator::find_link(const std::string& link_id) const {
    for (const auto& link : schema_.links) {
        if (link.id == link_id) {
            return &link;
        }
    }
    return nullptr;
}

void TriggerEvaluator::enable_link_target(OdcComponent* component) {
    if (!component) {
        return;
    }
    component->set_runtime_enabled(true);
}

void TriggerEvaluator::arm_link_targets(const std::vector<std::string>& link_ids) {
    if (!components_) {
        return;
    }

    for (const auto& link_id : link_ids) {
        const auto* link = find_link(link_id);
        if (!link) {
            continue;
        }

        const auto it = components_->find(link->to.component);
        if (it != components_->end() && it->second) {
            enable_link_target(it->second);
            it->second->arm();
        }
    }
}

void TriggerEvaluator::stop_link_targets(const std::vector<std::string>& link_ids) {
    if (!components_) {
        return;
    }

    for (const auto& link_id : link_ids) {
        const auto* link = find_link(link_id);
        if (!link) {
            continue;
        }

        const auto it = components_->find(link->to.component);
        if (it != components_->end() && it->second) {
            it->second->stop();
        }
    }
}

std::vector<std::string> TriggerEvaluator::collect_active_link_ids(const std::string& source_id) const {
    std::vector<std::string> link_ids;
    if (!link_engine_) {
        return link_ids;
    }

    for (const auto& link : schema_.links) {
        if (link.from.component == source_id && link_engine_->is_active(link.id)) {
            link_ids.push_back(link.id);
        }
    }
    return link_ids;
}

std::vector<std::string> TriggerEvaluator::collect_vpn_link_ids(const std::string& source_id) const {
    std::vector<std::string> link_ids;

    for (const auto& trigger : schema_.triggers) {
        if (trigger.on_event != "vpn.ip_ready" && trigger.on_event != "vpn.connected") {
            continue;
        }
        if (trigger.source_component != source_id) {
            continue;
        }

        link_ids.insert(link_ids.end(), trigger.activate_links.begin(), trigger.activate_links.end());
    }

    if (link_ids.empty()) {
        return collect_active_link_ids(source_id);
    }

    std::sort(link_ids.begin(), link_ids.end());
    link_ids.erase(std::unique(link_ids.begin(), link_ids.end()), link_ids.end());
    return link_ids;
}

void TriggerEvaluator::activate_vpn_links(const std::string& source_id) {
    if (!link_engine_) {
        return;
    }

    for (const auto& trigger : schema_.triggers) {
        if (trigger.on_event != "vpn.connected") {
            continue;
        }
        if (trigger.source_component != source_id) {
            continue;
        }

        for (const auto& link_id : trigger.activate_links) {
            link_engine_->activate_link(link_id);
        }
        arm_link_targets(trigger.activate_links);
    }
}

void TriggerEvaluator::on_vpn_connected(const std::string& source_id) {
    activate_vpn_links(source_id);
}

void TriggerEvaluator::on_vpn_ip_ready(const std::string& /*source_id*/, const TransportBinding& /*binding*/) {
    // Binding propagation now happens through DSPatch Circuit::Tick() ->
    // VpnTunnelComponent::Process_() emits the binding on the tunnel output bus,
    // and connected downstream components read it via their own Process_().
    // No manual propagation or start() needed here.
}

void TriggerEvaluator::on_vpn_disconnected(const std::string& source_id) {
    if (!link_engine_) {
        return;
    }

    for (const auto& trigger : schema_.triggers) {
        if (trigger.on_event != "vpn.disconnected") {
            continue;
        }
        if (trigger.source_component != source_id) {
            continue;
        }

        stop_link_targets(trigger.deactivate_links);

        for (const auto& link_id : trigger.deactivate_links) {
            link_engine_->deactivate_link(link_id);
        }
    }
}

void TriggerEvaluator::on_cellular_wan_ready(const std::string& component_id, const WanReady& wan) {
    if (!components_ || !wan.ready) {
        return;
    }

    SPDLOG_INFO("trigger: {} from {}", CellularWanComponent::kWanReadyEvent, component_id);

    for (auto& [id, component] : *components_) {
        if (!component || !component->type().starts_with("vpn.")) {
            continue;
        }

        const auto wan_ref = component->config().value("wan", "");
        if (wan_ref != component_id) {
            continue;
        }

        component->apply_wan(wan);
    }
}

void TriggerEvaluator::on_manual_start(const std::string& component_id) {
    if (!components_) {
        return;
    }

    const auto it = components_->find(component_id);
    if (it != components_->end() && it->second) {
        enable_link_target(it->second);
        it->second->start();
    }
}

} // namespace odc::graph
