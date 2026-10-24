#include "ipc/graph_handlers.hpp"

#include "graph/graph_runtime.hpp"
#include "rtsp/compression_capabilities.hpp"

namespace odc::graph {

namespace {

GraphComponentSpec parse_component_spec(const nlohmann::json& params) {
    GraphComponentSpec spec;
    spec.id = params.value("id", "");
    spec.type = params.value("type", "");
    spec.enabled = params.value("enabled", true);
    if (params.contains("config")) {
        spec.config = params["config"];
    }
    return spec;
}

GraphEndpoint parse_endpoint(const nlohmann::json& node) {
    GraphEndpoint ep;
    ep.component = node.value("component", "");
    ep.port = node.value("port", "");
    return ep;
}

GraphLinkSpec parse_link_spec(const nlohmann::json& params) {
    GraphLinkSpec spec;
    spec.id = params.value("id", "");
    if (params.contains("from")) {
        spec.from = parse_endpoint(params["from"]);
    }
    if (params.contains("to")) {
        spec.to = parse_endpoint(params["to"]);
    }
    if (params.contains("bind") && params["bind"].contains("fields")) {
        for (const auto& field : params["bind"]["fields"]) {
            spec.bind_fields.push_back(field.get<std::string>());
        }
    }
    return spec;
}

GraphTriggerSpec parse_trigger_spec(const nlohmann::json& params) {
    GraphTriggerSpec spec;
    spec.id = params.value("id", "");
    spec.on_event = params.value("on", "");
    spec.source_component = params.value("source", "");
    if (params.contains("activate")) {
        for (const auto& link_id : params["activate"]) {
            spec.activate_links.push_back(link_id.get<std::string>());
        }
    }
    if (params.contains("deactivate")) {
        for (const auto& link_id : params["deactivate"]) {
            spec.deactivate_links.push_back(link_id.get<std::string>());
        }
    }
    return spec;
}

nlohmann::json validation_errors_json(const std::vector<ValidationError>& errors) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& error : errors) {
        arr.push_back(error.message);
    }
    return arr;
}

} // namespace

void register_graph_methods(odc::ipc::MethodRegistry& registry, GraphRuntime& runtime) {
    registry.register_method("graph.status", [&runtime](const nlohmann::json&) {
        return runtime.status_json();
    });

    registry.register_method("graph.capabilities", [&runtime](const nlohmann::json&) {
        auto caps = runtime.compression_capabilities();
        if (!caps) {
            return nlohmann::json{{"ready", false}, {"error", "unavailable"}};
        }
        return caps->to_json();
    });

    registry.register_method("graph.capabilities.refresh", [&runtime](const nlohmann::json&) {
        const bool ok = runtime.refresh_compression_capabilities();
        auto caps = runtime.compression_capabilities();
        return nlohmann::json{
            {"success", ok},
            {"capabilities", caps ? caps->to_json() : nlohmann::json{{"ready", false}}},
        };
    });

    registry.register_method("graph.validate", [&runtime](const nlohmann::json&) {
        const auto errors = runtime.validate();
        return nlohmann::json{
            {"valid", errors.empty()},
            {"errors", validation_errors_json(errors)},
        };
    });

    registry.register_method("graph.components.list", [&runtime](const nlohmann::json&) {
        const auto status = runtime.status_json();
        return status.value("components", nlohmann::json::array());
    });

    registry.register_method("graph.components.add", [&runtime](const nlohmann::json& params) {
        const auto spec = parse_component_spec(params);
        const bool ok = runtime.add_component(spec);
        if (!ok) {
            return nlohmann::json{{"success", false}, {"error", "failed to add component"}};
        }
        runtime.persist();
        return nlohmann::json{{"success", true}, {"status", runtime.status_json()}};
    });

    registry.register_method("graph.components.remove", [&runtime](const nlohmann::json& params) {
        const auto id = params.value("id", "");
        const bool ok = runtime.remove_component(id);
        if (!ok) {
            return nlohmann::json{{"success", false}, {"error", "component not found"}};
        }
        runtime.persist();
        return nlohmann::json{{"success", true}, {"status", runtime.status_json()}};
    });

    registry.register_method("graph.components.configure", [&runtime](const nlohmann::json& params) {
        const auto id = params.value("id", "");
        const auto& config = params.contains("config") ? params["config"] : nlohmann::json::object();
        const bool ok = runtime.configure_component(id, config);
        if (!ok) {
            return nlohmann::json{{"success", false}, {"error", "component not found"}};
        }
        runtime.persist();
        return nlohmann::json{{"success", true}, {"status", runtime.status_json()}};
    });

    registry.register_method("graph.components.start", [&runtime](const nlohmann::json& params) {
        const auto id = params.value("id", "");
        const bool ok = runtime.start_component(id);
        return nlohmann::json{{"success", ok}};
    });

    registry.register_method("graph.components.connect", [&runtime](const nlohmann::json& params) {
        const auto id = params.value("id", "");
        const bool ok = runtime.connect_component(id);
        if (!ok) {
            return nlohmann::json{
                {"success", false},
                {"error", "connect failed (cellular.qmi only; component must exist)"},
            };
        }
        return nlohmann::json{{"success", true}};
    });

    registry.register_method("graph.components.reset", [&runtime](const nlohmann::json& params) {
        const auto id = params.value("id", "");
        const bool ok = runtime.reset_component(id);
        if (!ok) {
            return nlohmann::json{
                {"success", false},
                {"error", "reset failed (cellular.qmi only; component must exist)"},
            };
        }
        return nlohmann::json{{"success", true}};
    });

    registry.register_method("graph.components.profiles.list", [&runtime](const nlohmann::json& params) {
        const auto id = params.value("id", "");
        std::string error;
        auto profiles = runtime.list_component_profiles(id, &error);
        if (!error.empty() && profiles.empty()) {
            return nlohmann::json{{"success", false}, {"error", error}, {"profiles", profiles}};
        }
        return nlohmann::json{{"success", true}, {"profiles", profiles}};
    });

    registry.register_method("graph.components.stop", [&runtime](const nlohmann::json& params) {
        const auto id = params.value("id", "");
        const bool ok = runtime.stop_component(id);
        return nlohmann::json{{"success", ok}};
    });

    registry.register_method("graph.links.add", [&runtime](const nlohmann::json& params) {
        bool dry_run = params.value("dry_run", false);
        const auto spec = parse_link_spec(params);

        if (dry_run) {
            const auto errors = runtime.validate();
            return nlohmann::json{
                {"success", errors.empty()},
                {"valid", errors.empty()},
                {"errors", validation_errors_json(errors)},
            };
        }

        const bool ok = runtime.add_link(spec);
        if (!ok) {
            const auto errors = runtime.validate();
            return nlohmann::json{
                {"success", false},
                {"error", "failed to add link"},
                {"errors", validation_errors_json(errors)},
            };
        }
        runtime.persist();
        return nlohmann::json{{"success", true}, {"status", runtime.status_json()}};
    });

    registry.register_method("graph.links.remove", [&runtime](const nlohmann::json& params) {
        const auto id = params.value("id", "");
        const bool ok = runtime.remove_link(id);
        if (!ok) {
            return nlohmann::json{{"success", false}, {"error", "link not found"}};
        }
        runtime.persist();
        return nlohmann::json{{"success", true}, {"status", runtime.status_json()}};
    });

    registry.register_method("graph.triggers.set", [&runtime](const nlohmann::json& params) {
        bool dry_run = params.value("dry_run", false);
        const auto spec = parse_trigger_spec(params);

        if (dry_run) {
            const auto errors = runtime.validate();
            return nlohmann::json{
                {"success", errors.empty()},
                {"valid", errors.empty()},
                {"errors", validation_errors_json(errors)},
            };
        }

        const bool ok = runtime.set_trigger(spec);
        if (!ok) {
            const auto errors = runtime.validate();
            return nlohmann::json{
                {"success", false},
                {"error", "failed to set trigger"},
                {"errors", validation_errors_json(errors)},
            };
        }
        runtime.persist();
        return nlohmann::json{{"success", true}, {"status", runtime.status_json()}};
    });

    registry.register_method("graph.apply", [&runtime](const nlohmann::json& params) {
        std::vector<std::string> component_ids;
        if (params.contains("component_ids")) {
            for (const auto& id : params["component_ids"]) {
                component_ids.push_back(id.get<std::string>());
            }
        }
        bool force = params.value("force", false);

        const bool ok = runtime.apply(component_ids, force);
        if (!ok) {
            return nlohmann::json{
                {"success", false},
                {"errors", validation_errors_json(runtime.validate())},
            };
        }

        return nlohmann::json{{"success", true}, {"status", runtime.status_json()}};
    });
}

} // namespace odc::graph
