#pragma once

#include "graph/graph_schema.hpp"
#include "graph/transport_binding.hpp"

#include <DSPatch.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace odc::graph {

class OdcComponent;

int input_port_index(const std::string& component_type, const std::string& port_name);
int output_port_index(const std::string& component_type, const std::string& port_name);

class LinkEngine {
public:
    LinkEngine(const GraphSchema& schema, DSPatch::Circuit& circuit);

    void register_component(const std::string& id, OdcComponent* component);
    void unregister_component(const std::string& id);
    void register_links();
    void sync_links();

    bool activate_link(const std::string& link_id);
    bool deactivate_link(const std::string& link_id);
    bool is_active(const std::string& link_id) const;

    void propagate(const TransportBinding& binding, const std::vector<std::string>& link_ids = {});
    void apply_active_router_links(const std::string& router_id);

private:
    const GraphLinkSpec* find_link(const std::string& link_id) const;
    void apply_link(const GraphLinkSpec& link, const TransportBinding& binding);

    const GraphSchema& schema_;
    DSPatch::Circuit& circuit_;
    std::unordered_map<std::string, bool> active_links_;
    std::unordered_map<std::string, OdcComponent*> components_;
    std::unordered_map<std::string, int> wired_inputs_;
};

} // namespace odc::graph
