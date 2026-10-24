#include "graph/link_engine.hpp"

#include "graph/components/mav_router_component.hpp"
#include "graph/components/odc_component.hpp"

#include <algorithm>

namespace odc::graph {

namespace {

TransportBinding filter_binding(const TransportBinding& src, const std::vector<std::string>& fields) {
    TransportBinding out;
    for (const auto& field : fields) {
        if (field == "connection_id") {
            out.connection_id = src.connection_id;
        } else if (field == "interface_name") {
            out.interface_name = src.interface_name;
        } else if (field == "target_ip") {
            out.target_ip = src.target_ip;
        } else if (field == "prefix_len") {
            out.prefix_len = src.prefix_len;
        } else if (field == "vpn_state") {
            out.vpn_state = src.vpn_state;
        } else if (field == "ip_ready") {
            out.ip_ready = src.ip_ready;
        }
    }
    return out;
}

bool contains_field(const std::vector<std::string>& fields, const std::string& name) {
    return std::find(fields.begin(), fields.end(), name) != fields.end();
}

struct PortIndexMap {
    std::unordered_map<std::string, int> output_ports;
    std::unordered_map<std::string, int> input_ports;
};

const std::unordered_map<std::string, PortIndexMap> kPortIndexMap = {
    {"cellular.qmi",        {{ {"wan", 0}, {"metrics", 1} }, {}}},
    {"vpn.wireguard",       {{ {"tunnel", 0}, {"state", 1} }, {{"wan", 0}}}},
    {"vpn.openvpn",         {{ {"tunnel", 0}, {"state", 1} }, {{"wan", 0}}}},
    {"mavlink.router",      {{ {"router", 0} },                {}}},
    {"mavlink.extender",    {{ {"status", 0} },                {{"transport", 0}, {"upstream", 1}}}},
    {"rtsp.compressor",     {{ {"status", 0}, {"metrics", 1} }, {{"transport", 0}}}},
    {"compression.capabilities", {{ {"status", 0}, {"inventory", 1} }, {}}},
};

} // namespace

int input_port_index(const std::string& component_type, const std::string& port_name) {
    const auto type_it = kPortIndexMap.find(component_type);
    if (type_it == kPortIndexMap.end()) return -1;
    const auto port_it = type_it->second.input_ports.find(port_name);
    return port_it != type_it->second.input_ports.end() ? port_it->second : -1;
}

int output_port_index(const std::string& component_type, const std::string& port_name) {
    const auto type_it = kPortIndexMap.find(component_type);
    if (type_it == kPortIndexMap.end()) return -1;
    const auto port_it = type_it->second.output_ports.find(port_name);
    return port_it != type_it->second.output_ports.end() ? port_it->second : -1;
}

LinkEngine::LinkEngine(const GraphSchema& schema, DSPatch::Circuit& circuit)
    : schema_(schema), circuit_(circuit) {}

void LinkEngine::register_component(const std::string& id, OdcComponent* component) {
    if (component) {
        components_[id] = component;
    }
}

void LinkEngine::unregister_component(const std::string& id) {
    components_.erase(id);
}

void LinkEngine::register_links() {
    active_links_.clear();
    for (const auto& link : schema_.links) {
        active_links_[link.id] = false;
    }
}

void LinkEngine::sync_links() {
    std::unordered_map<std::string, bool> updated;
    for (const auto& link : schema_.links) {
        const auto it = active_links_.find(link.id);
        updated[link.id] = (it != active_links_.end()) ? it->second : false;
    }
    active_links_ = std::move(updated);

    for (auto wire_it = wired_inputs_.begin(); wire_it != wired_inputs_.end(); ) {
        if (active_links_.find(wire_it->first) == active_links_.end()) {
            wire_it = wired_inputs_.erase(wire_it);
        } else {
            ++wire_it;
        }
    }
}

bool LinkEngine::activate_link(const std::string& link_id) {
    const auto it = active_links_.find(link_id);
    if (it == active_links_.end()) {
        return false;
    }

    const auto* link = find_link(link_id);
    if (!link) return false;

    const auto src_it = components_.find(link->from.component);
    const auto dst_it = components_.find(link->to.component);
    if (src_it == components_.end() || dst_it == components_.end()) return false;

    const int src_out_idx = output_port_index(src_it->second->type(), link->from.port);
    const int dst_in_idx  = input_port_index(dst_it->second->type(), link->to.port);
    if (src_out_idx < 0 || dst_in_idx < 0) return false;

    auto src_sp = std::shared_ptr<DSPatch::Component>(
        src_it->second, [](DSPatch::Component*) {});
    auto dst_sp = std::shared_ptr<DSPatch::Component>(
        dst_it->second, [](DSPatch::Component*) {});
    circuit_.ConnectOutToIn(src_sp, src_out_idx, dst_sp, dst_in_idx);

    it->second = true;
    wired_inputs_[link_id] = dst_in_idx;

    if (contains_field(link->bind_fields, "router_ref")) {
        apply_link(*link, TransportBinding{});
    }
    return true;
}

bool LinkEngine::deactivate_link(const std::string& link_id) {
    const auto it = active_links_.find(link_id);
    if (it == active_links_.end()) {
        return false;
    }

    const auto* link = find_link(link_id);
    if (!link) return false;

    const auto dst_it = components_.find(link->to.component);
    if (dst_it != components_.end() && dst_it->second) {
        const auto wire_it = wired_inputs_.find(link_id);
        if (wire_it != wired_inputs_.end()) {
            dst_it->second->DisconnectInput(wire_it->second);
        }
    }

    it->second = false;
    wired_inputs_.erase(link_id);
    return true;
}

bool LinkEngine::is_active(const std::string& link_id) const {
    const auto it = active_links_.find(link_id);
    return it != active_links_.end() && it->second;
}

void LinkEngine::propagate(const TransportBinding& binding, const std::vector<std::string>& link_ids) {
    const auto& source_id = binding.connection_id;

    for (const auto& link : schema_.links) {
        if (!link_ids.empty()
            && std::find(link_ids.begin(), link_ids.end(), link.id) == link_ids.end()) {
            continue;
        }
        if (!is_active(link.id)) {
            continue;
        }
        if (link.from.component != source_id) {
            continue;
        }

        apply_link(link, binding);
    }
}

const GraphLinkSpec* LinkEngine::find_link(const std::string& link_id) const {
    for (const auto& link : schema_.links) {
        if (link.id == link_id) {
            return &link;
        }
    }
    return nullptr;
}

void LinkEngine::apply_link(const GraphLinkSpec& link, const TransportBinding& binding) {
    const auto target_it = components_.find(link.to.component);
    if (target_it == components_.end() || !target_it->second) {
        return;
    }

    OdcComponent* target = target_it->second;

    if (contains_field(link.bind_fields, "router_ref")) {
        const auto source_it = components_.find(link.from.component);
        if (source_it != components_.end() && source_it->second) {
            if (auto* router = dynamic_cast<MavRouterComponent*>(source_it->second)) {
                RouterRef ref;
                ref.router = router->get_router();
                target->apply_router(ref);
            }
        }
    }

    if (!link.bind_fields.empty()) {
        const bool only_router = link.bind_fields.size() == 1
            && link.bind_fields.front() == "router_ref";
        if (!only_router) {
            target->apply_binding(filter_binding(binding, link.bind_fields));
        }
    }
}

void LinkEngine::apply_active_router_links(const std::string& router_id) {
    for (const auto& link : schema_.links) {
        if (!is_active(link.id)) {
            continue;
        }
        if (link.from.component != router_id) {
            continue;
        }
        if (!contains_field(link.bind_fields, "router_ref")) {
            continue;
        }
        apply_link(link, TransportBinding{});
    }
}

} // namespace odc::graph
