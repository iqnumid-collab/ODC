#pragma once

#include <nlohmann/json.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace odc::graph {

struct GraphEndpoint {
    std::string component;
    std::string port;
};

struct GraphComponentSpec {
    std::string id;
    std::string type;
    bool enabled{true};
    nlohmann::json config = nlohmann::json::object();
};

struct GraphLinkSpec {
    std::string id;
    GraphEndpoint from;
    GraphEndpoint to;
    std::vector<std::string> bind_fields;
};

struct GraphTriggerSpec {
    std::string id;
    std::string on_event;
    std::string source_component;
    std::vector<std::string> activate_links;
    std::vector<std::string> deactivate_links;
};

struct GraphGeneralSpec {
    std::string log_level{"info"};
    std::string socket{"/run/odc-onboard/odc-onboard.sock"};
};

struct GraphSchema {
    int version{2};
    GraphGeneralSpec general;
    std::vector<GraphComponentSpec> components;
    std::vector<GraphLinkSpec> links;
    std::vector<GraphTriggerSpec> triggers;
};

std::optional<GraphSchema> load_graph_schema(const std::filesystem::path& path);
bool save_graph_schema(const std::filesystem::path& path, const GraphSchema& schema);
nlohmann::json schema_to_json(const GraphSchema& schema);

} // namespace odc::graph
