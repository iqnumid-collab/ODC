#include "graph/graph_validator.hpp"

#include <unordered_map>
#include <unordered_set>

namespace odc::graph {

namespace {

const std::unordered_map<std::string, std::unordered_set<std::string>> kOutputPorts = {
    {"cellular.qmi", {"wan", "metrics"}},
    {"vpn.wireguard", {"tunnel", "state"}},
    {"vpn.openvpn", {"tunnel", "state"}},
    {"mavlink.router", {"router"}},
    {"mavlink.extender", {"status"}},
    {"rtsp.compressor", {"status", "metrics"}},
    {"compression.capabilities", {"status", "inventory"}},
};

const std::unordered_map<std::string, std::unordered_set<std::string>> kInputPorts = {
    {"vpn.wireguard", {"wan"}},
    {"vpn.openvpn", {"wan"}},
    {"mavlink.extender", {"transport", "upstream"}},
    {"rtsp.compressor", {"transport"}},
};

bool has_port(const std::unordered_map<std::string, std::unordered_set<std::string>>& map,
              const std::string& type, const std::string& port) {
    auto it = map.find(type);
    if (it == map.end()) return false;
    return it->second.count(port) > 0;
}

} // namespace

std::vector<ValidationError> GraphValidator::validate(const GraphSchema& schema) const {
    std::vector<ValidationError> errors;
    if (schema.version != 2) {
        errors.push_back({"version must be 2"});
    }

    std::unordered_set<std::string> ids;
    std::unordered_map<std::string, std::string> id_to_type;
    for (const auto& c : schema.components) {
        if (c.id.empty()) errors.push_back({"component id must not be empty"});
        if (!ids.insert(c.id).second) errors.push_back({"duplicate component id: " + c.id});
        id_to_type[c.id] = c.type;
    }

    for (const auto& c : schema.components) {
        if (c.type.starts_with("vpn.") && c.config.contains("wan")) {
            const auto wan = c.config["wan"].get<std::string>();
            if (!id_to_type.count(wan)) {
                errors.push_back({"component " + c.id + " references unknown wan: " + wan});
            }
        }
    }

    std::unordered_set<std::string> link_ids;
    for (const auto& l : schema.links) {
        if (l.id.empty()) errors.push_back({"link id must not be empty"});
        if (!link_ids.insert(l.id).second) errors.push_back({"duplicate link id: " + l.id});
        if (!id_to_type.count(l.from.component)) {
            errors.push_back({"link " + l.id + " unknown from component: " + l.from.component});
        } else if (!has_port(kOutputPorts, id_to_type[l.from.component], l.from.port)) {
            errors.push_back({"link " + l.id + " invalid from port: " + l.from.port});
        }
        if (!id_to_type.count(l.to.component)) {
            errors.push_back({"link " + l.id + " unknown to component: " + l.to.component});
        } else if (!has_port(kInputPorts, id_to_type[l.to.component], l.to.port)) {
            errors.push_back({"link " + l.id + " invalid to port: " + l.to.port});
        }
    }

    for (const auto& t : schema.triggers) {
        if (!id_to_type.count(t.source_component)) {
            errors.push_back({"trigger " + t.id + " unknown source: " + t.source_component});
        }
        for (const auto& lid : t.activate_links) {
            if (!link_ids.count(lid)) errors.push_back({"trigger " + t.id + " unknown activate link: " + lid});
        }
        for (const auto& lid : t.deactivate_links) {
            if (!link_ids.count(lid)) errors.push_back({"trigger " + t.id + " unknown deactivate link: " + lid});
        }
    }

    return errors;
}

bool GraphValidator::is_valid(const GraphSchema& schema) const {
    return validate(schema).empty();
}

} // namespace odc::graph
