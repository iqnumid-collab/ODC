#include "graph/graph_schema.hpp"

#include <fstream>
#include <yaml-cpp/yaml.h>

namespace odc::graph {

namespace {

YAML::Node json_to_yaml(const nlohmann::json& j) {
    if (j.is_null())            return YAML::Node(YAML::NodeType::Null);
    if (j.is_boolean())         return YAML::Node(j.get<bool>());
    if (j.is_number_integer())  return YAML::Node(j.get<int64_t>());
    if (j.is_number_unsigned()) return YAML::Node(j.get<uint64_t>());
    if (j.is_number_float())    return YAML::Node(j.get<double>());
    if (j.is_string())          return YAML::Node(j.get<std::string>());

    if (j.is_array()) {
        YAML::Node arr(YAML::NodeType::Sequence);
        for (const auto& item : j) {
            arr.push_back(json_to_yaml(item));
        }
        return arr;
    }

    if (j.is_object()) {
        YAML::Node obj(YAML::NodeType::Map);
        for (auto it = j.begin(); it != j.end(); ++it) {
            obj[it.key()] = json_to_yaml(it.value());
        }
        return obj;
    }

    return YAML::Node();
}

nlohmann::json yaml_to_json(const YAML::Node& node) {
    if (node.IsNull())    return nullptr;
    if (node.IsScalar()) {
        const std::string val = node.as<std::string>();
        if (!val.empty() && (val.front() == '{' || val.front() == '[')) {
            try {
                return nlohmann::json::parse(val);
            } catch (...) {}
        }
        if (val == "true" || val == "True" || val == "TRUE")  return true;
        if (val == "false" || val == "False" || val == "FALSE") return false;
        try {
            return node.as<int64_t>();
        } catch (...) {}
        try {
            return node.as<double>();
        } catch (...) {}
        return val;
    }
    if (node.IsSequence()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& item : node) {
            arr.push_back(yaml_to_json(item));
        }
        return arr;
    }
    if (node.IsMap()) {
        nlohmann::json obj = nlohmann::json::object();
        for (auto it = node.begin(); it != node.end(); ++it) {
            obj[it->first.as<std::string>()] = yaml_to_json(it->second);
        }
        return obj;
    }
    return nullptr;
}

GraphEndpoint parse_endpoint(const YAML::Node& node) {
    GraphEndpoint ep;
    if (node["component"]) ep.component = node["component"].as<std::string>();
    if (node["port"]) ep.port = node["port"].as<std::string>();
    return ep;
}

YAML::Node endpoint_to_yaml(const GraphEndpoint& ep) {
    YAML::Node node;
    node["component"] = ep.component;
    node["port"] = ep.port;
    return node;
}

std::optional<GraphSchema> load_graph_schema_impl(const std::filesystem::path& path) {
    try {
        YAML::Node root = YAML::LoadFile(path.string());
        GraphSchema schema;
        schema.version = root["version"] ? root["version"].as<int>() : 2;

        if (root["general"]) {
            const YAML::Node g = root["general"];
            if (g["log_level"]) schema.general.log_level = g["log_level"].as<std::string>();
            if (g["socket"]) schema.general.socket = g["socket"].as<std::string>();
        }

        if (root["components"]) {
            for (const auto& c : root["components"]) {
                GraphComponentSpec spec;
                spec.id = c["id"].as<std::string>();
                spec.type = c["type"].as<std::string>();
                if (c["enabled"]) spec.enabled = c["enabled"].as<bool>();
                if (c["config"]) {
                    spec.config = yaml_to_json(c["config"]);
                }
                schema.components.push_back(std::move(spec));
            }
        }

        if (root["links"]) {
            for (const auto& l : root["links"]) {
                GraphLinkSpec spec;
                spec.id = l["id"].as<std::string>();
                spec.from = parse_endpoint(l["from"]);
                spec.to = parse_endpoint(l["to"]);
                if (l["bind"] && l["bind"]["fields"]) {
                    for (const auto& f : l["bind"]["fields"]) {
                        spec.bind_fields.push_back(f.as<std::string>());
                    }
                }
                schema.links.push_back(std::move(spec));
            }
        }

        if (root["triggers"]) {
            for (const auto& t : root["triggers"]) {
                GraphTriggerSpec spec;
                spec.id = t["id"].as<std::string>();
                spec.on_event = t["on"].as<std::string>();
                spec.source_component = t["source"].as<std::string>();
                if (t["activate"]) {
                    for (const auto& a : t["activate"]) spec.activate_links.push_back(a.as<std::string>());
                }
                if (t["deactivate"]) {
                    for (const auto& d : t["deactivate"]) spec.deactivate_links.push_back(d.as<std::string>());
                }
                schema.triggers.push_back(std::move(spec));
            }
        }

        return schema;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

std::optional<GraphSchema> load_graph_schema(const std::filesystem::path& path) {
    if (auto schema = load_graph_schema_impl(path)) {
        return schema;
    }

    const auto bak = path.string() + ".bak";
    if (std::filesystem::exists(bak)) {
        return load_graph_schema_impl(bak);
    }

    return std::nullopt;
}

bool save_graph_schema(const std::filesystem::path& path, const GraphSchema& schema) {
    try {
        YAML::Node root;
        root["version"] = schema.version;

        YAML::Node general;
        general["log_level"] = schema.general.log_level;
        general["socket"] = schema.general.socket;
        root["general"] = general;

        YAML::Node components;
        for (const auto& c : schema.components) {
            YAML::Node node;
            node["id"] = c.id;
            node["type"] = c.type;
            node["enabled"] = c.enabled;
            if (!c.config.is_null() && !c.config.empty()) {
                node["config"] = json_to_yaml(c.config);
            }
            components.push_back(node);
        }
        root["components"] = components;

        YAML::Node links;
        for (const auto& l : schema.links) {
            YAML::Node node;
            node["id"] = l.id;
            node["from"] = endpoint_to_yaml(l.from);
            node["to"] = endpoint_to_yaml(l.to);
            if (!l.bind_fields.empty()) {
                YAML::Node fields;
                for (const auto& f : l.bind_fields) fields.push_back(f);
                node["bind"]["fields"] = fields;
            }
            links.push_back(node);
        }
        root["links"] = links;

        YAML::Node triggers;
        for (const auto& t : schema.triggers) {
            YAML::Node node;
            node["id"] = t.id;
            node["on"] = t.on_event;
            node["source"] = t.source_component;
            if (!t.activate_links.empty()) {
                YAML::Node act;
                for (const auto& a : t.activate_links) act.push_back(a);
                node["activate"] = act;
            }
            if (!t.deactivate_links.empty()) {
                YAML::Node deact;
                for (const auto& d : t.deactivate_links) deact.push_back(d);
                node["deactivate"] = deact;
            }
            triggers.push_back(node);
        }
        root["triggers"] = triggers;

        const auto tmp = path.string() + ".tmp";
        const auto bak = path.string() + ".bak";

        YAML::Emitter emitter;
        emitter.SetIndent(2);
        emitter.SetMapFormat(YAML::Block);
        emitter << root;

        {
            std::ofstream ofs(tmp);
            if (!ofs) return false;
            ofs << emitter.c_str();
        }
        if (std::filesystem::exists(path)) {
            std::filesystem::copy_file(path, bak, std::filesystem::copy_options::overwrite_existing);
        }
        std::filesystem::rename(tmp, path);
        return true;
    } catch (...) {
        return false;
    }
}

nlohmann::json schema_to_json(const GraphSchema& schema) {
    nlohmann::json j;
    j["version"] = schema.version;
    j["general"] = {{"log_level", schema.general.log_level}, {"socket", schema.general.socket}};
    j["components"] = nlohmann::json::array();
    for (const auto& c : schema.components) {
        j["components"].push_back({
            {"id", c.id}, {"type", c.type}, {"enabled", c.enabled}, {"config", c.config}
        });
    }
    j["links"] = nlohmann::json::array();
    for (const auto& l : schema.links) {
        j["links"].push_back({
            {"id", l.id},
            {"from", {{"component", l.from.component}, {"port", l.from.port}}},
            {"to", {{"component", l.to.component}, {"port", l.to.port}}},
            {"bind", {{"fields", l.bind_fields}}},
            {"active", false}
        });
    }
    j["triggers"] = nlohmann::json::array();
    for (const auto& t : schema.triggers) {
        j["triggers"].push_back({
            {"id", t.id}, {"on", t.on_event}, {"source", t.source_component},
            {"activate", t.activate_links}, {"deactivate", t.deactivate_links}
        });
    }
    return j;
}

} // namespace odc::graph
