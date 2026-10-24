#pragma once

#include "graph/graph_schema.hpp"
#include "graph/transport_binding.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace odc::graph {

class LinkEngine;
class OdcComponent;

class TriggerEvaluator {
public:
    TriggerEvaluator(
        const GraphSchema& schema,
        LinkEngine* link_engine,
        std::unordered_map<std::string, OdcComponent*>* components);

    void on_vpn_connected(const std::string& source_id);
    void on_vpn_ip_ready(const std::string& source_id, const TransportBinding& binding);
    void on_vpn_disconnected(const std::string& source_id);
    void on_cellular_wan_ready(const std::string& component_id, const WanReady& wan);
    void on_manual_start(const std::string& component_id);

private:
    const GraphLinkSpec* find_link(const std::string& link_id) const;
    void enable_link_target(OdcComponent* component);
    void arm_link_targets(const std::vector<std::string>& link_ids);
    void stop_link_targets(const std::vector<std::string>& link_ids);
    void activate_vpn_links(const std::string& source_id);
    std::vector<std::string> collect_active_link_ids(const std::string& source_id) const;
    std::vector<std::string> collect_vpn_link_ids(const std::string& source_id) const;

    const GraphSchema& schema_;
    LinkEngine* link_engine_;
    std::unordered_map<std::string, OdcComponent*>* components_;
};

} // namespace odc::graph
