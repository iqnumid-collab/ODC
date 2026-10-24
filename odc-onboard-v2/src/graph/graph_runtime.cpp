#include "graph/graph_runtime.hpp"

#include "core/event_bus.hpp"
#include "core/vpn_monitor.hpp"
#include "graph/circuit_registry.hpp"
#include "graph/components/cellular_wan_component.hpp"
#include "graph/components/compression_capabilities_component.hpp"
#include "graph/components/mav_router_component.hpp"
#include "graph/components/odc_component.hpp"
#include "graph/components/vpn_tunnel_component.hpp"
#include "graph/graph_builder.hpp"
#include "graph/graph_validator.hpp"
#include "graph/link_engine.hpp"
#include "graph/persistence_manager.hpp"
#include "graph/trigger_evaluator.hpp"
#include "qmi/qmi_events.hpp"
#include "rtsp/compression_capabilities.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace odc::graph {

GraphRuntime::GraphRuntime(
    EventBus& bus,
    orch::ThreadOrchestrator& orchestrator,
    VpnInterfaceMonitor* vpn_monitor,
    std::shared_ptr<CompressionCapabilities> compression_caps)
    : bus_(bus),
      orchestrator_(orchestrator),
      vpn_monitor_(vpn_monitor),
      compression_caps_(compression_caps
          ? std::move(compression_caps)
          : std::make_shared<CompressionCapabilities>()),
      validator_(std::make_unique<GraphValidator>()) {}

GraphRuntime::~GraphRuntime() {
    stop();
}

bool GraphRuntime::load(const std::filesystem::path& path) {
    const auto schema = load_graph_schema(path);
    if (!schema) {
        return false;
    }

    std::lock_guard lock(graph_mutex_);
    path_ = path;
    schema_ = *schema;
    return true;
}

bool GraphRuntime::start() {
    // Identify GPU / encoder inventory at graph start and autosave.
    if (compression_caps_) {
        compression_caps_->ensure(true);
    }

    std::vector<OdcComponent*> auto_start;
    {
        std::lock_guard lock(graph_mutex_);
        if (running_) {
            return true;
        }

        if (!validator_->is_valid(schema_)) {
            return false;
        }

        build_runtime_locked(&auto_start);
        running_ = true;
    }

    start_components_unlocked(auto_start);
    return true;
}

void GraphRuntime::stop() {
    std::lock_guard lock(graph_mutex_);

    if (!running_) {
        return;
    }

    stop_components_ordered();

    if (event_sub_) {
        bus_.unsubscribe(event_sub_);
        event_sub_ = 0;
    }

    if (circuit_) {
        circuit_->clear();
    }

    circuit_.reset();
    link_engine_.reset();
    trigger_eval_.reset();
    components_.clear();
    component_ptrs_.clear();
    running_ = false;
}

void GraphRuntime::tick() {
    std::lock_guard lock(graph_mutex_);
    if (!running_ || !circuit_) {
        return;
    }
    circuit_->tick();
}

nlohmann::json GraphRuntime::status_json() const {
    std::lock_guard lock(graph_mutex_);

    auto status = schema_to_json(schema_);
    status["running"] = running_;
    status["path"] = path_.string();
    if (compression_caps_) {
        status["compression_capabilities"] = compression_caps_->to_json();
    }

    for (auto& component_json : status["components"]) {
        const auto id = component_json["id"].get<std::string>();
        const auto it = components_.find(id);
        if (it != components_.end()) {
            component_json = it->second->status_json();
        }
    }

    // Include runtime-only nodes (e.g. auto-injected compression.capabilities).
    for (const auto& [id, component] : components_) {
        const bool in_schema = std::any_of(
            schema_.components.begin(), schema_.components.end(),
            [&](const GraphComponentSpec& spec) { return spec.id == id; });
        if (!in_schema && component) {
            status["components"].push_back(component->status_json());
        }
    }

    for (auto& link_json : status["links"]) {
        const auto id = link_json["id"].get<std::string>();
        link_json["active"] = link_engine_ && link_engine_->is_active(id);
    }

    return status;
}

bool GraphRuntime::add_component(const GraphComponentSpec& spec) {
    OdcComponent* component_to_start = nullptr;

    {
        std::lock_guard lock(graph_mutex_);

        if (std::any_of(schema_.components.begin(), schema_.components.end(),
                [&](const GraphComponentSpec& existing) { return existing.id == spec.id; })) {
            return false;
        }

        schema_.components.push_back(spec);

        if (!running_) {
            return true;
        }

        GraphBuilder builder(bus_, orchestrator_, vpn_monitor_, compression_caps_);
        auto component = builder.build(spec);
        if (!component) {
            schema_.components.pop_back();
            return false;
        }

        const auto id = component->id();
        component_ptrs_[id] = component.get();
        link_engine_->register_component(id, component.get());
        circuit_->add(component.get());
        wire_component_callbacks(component.get());
        components_.emplace(id, std::move(component));

        if (spec.enabled &&
            (spec.type == "cellular.qmi" || spec.type == "mavlink.router" ||
             spec.type == "compression.capabilities" ||
             spec.type.starts_with("vpn."))) {
            component_to_start = component_ptrs_[id];
        }
    }

    if (component_to_start) {
        component_to_start->start();
    }

    return true;
}

bool GraphRuntime::remove_component(const std::string& id) {
    std::lock_guard lock(graph_mutex_);

    const auto spec_it = std::find_if(schema_.components.begin(), schema_.components.end(),
        [&](const GraphComponentSpec& spec) { return spec.id == id; });
    if (spec_it == schema_.components.end()) {
        return false;
    }

    remove_links_for_component(id);
    remove_triggers_for_component(id);
    schema_.components.erase(spec_it);

    const auto comp_it = components_.find(id);
    if (comp_it != components_.end()) {
        comp_it->second->stop();
        if (circuit_) {
            circuit_->remove(id);
        }
        if (link_engine_) {
            link_engine_->unregister_component(id);
        }
        components_.erase(comp_it);
    }

    component_ptrs_.erase(id);
    return true;
}

bool GraphRuntime::configure_component(const std::string& id, const nlohmann::json& config) {
    std::lock_guard lock(graph_mutex_);

    auto* spec = find_component_spec(id);
    if (!spec) {
        return false;
    }

    spec->config = config;
    if (config.contains("enabled") && config["enabled"].is_boolean()) {
        spec->enabled = config["enabled"].get<bool>();
    }

    if (auto* component = find_component(id)) {
        const bool was_running = component->state() == ComponentRuntimeState::Running;
        component->configure(config);
        if (was_running) {
            component->stop();
            component->start();
        }
    }

    return true;
}

bool GraphRuntime::add_link(const GraphLinkSpec& link) {
    std::lock_guard lock(graph_mutex_);

    if (std::any_of(schema_.links.begin(), schema_.links.end(),
            [&](const GraphLinkSpec& existing) { return existing.id == link.id; })) {
        return false;
    }

    GraphSchema candidate = schema_;
    candidate.links.push_back(link);
    if (!validator_->is_valid(candidate)) {
        return false;
    }

    schema_.links.push_back(link);

    if (link_engine_) {
        link_engine_->sync_links();
    }

    return true;
}

bool GraphRuntime::remove_link(const std::string& id) {
    std::lock_guard lock(graph_mutex_);

    const auto it = std::find_if(schema_.links.begin(), schema_.links.end(),
        [&](const GraphLinkSpec& link) { return link.id == id; });
    if (it == schema_.links.end()) {
        return false;
    }

    if (link_engine_) {
        link_engine_->deactivate_link(id);
    }

    schema_.links.erase(it);

    if (link_engine_) {
        link_engine_->sync_links();
    }

    return true;
}

bool GraphRuntime::set_trigger(const GraphTriggerSpec& trigger) {
    {
        std::lock_guard lock(graph_mutex_);

        GraphSchema candidate = schema_;
        const auto it = std::find_if(candidate.triggers.begin(), candidate.triggers.end(),
            [&](const GraphTriggerSpec& existing) { return existing.id == trigger.id; });
        if (it != candidate.triggers.end()) {
            *it = trigger;
        } else {
            candidate.triggers.push_back(trigger);
        }
        if (!validator_->is_valid(candidate)) {
            return false;
        }

        const auto existing = std::find_if(schema_.triggers.begin(), schema_.triggers.end(),
            [&](const GraphTriggerSpec& existing) { return existing.id == trigger.id; });
        if (existing != schema_.triggers.end()) {
            *existing = trigger;
        } else {
            schema_.triggers.push_back(trigger);
        }
    }

    if (trigger.on_event.starts_with("vpn.")) {
        replay_vpn_triggers_for(trigger.source_component);
    }

    return true;
}

bool GraphRuntime::apply(const std::vector<std::string>& component_ids, bool force) {
    std::vector<OdcComponent*> auto_start;

    {
        std::lock_guard lock(graph_mutex_);

        if (!validator_->is_valid(schema_)) {
            return false;
        }

        if (!running_) {
            return true;
        }

        if (force) {
            stop_components_ordered();
            build_runtime_locked(&auto_start);
            // Start outside this lock — VPN start publishes events that re-enter graph_mutex_.
        } else if (component_ids.empty()) {
            return reconcile_locked();
        } else {
            GraphBuilder builder(bus_, orchestrator_, vpn_monitor_, compression_caps_);

            for (const auto& id : component_ids) {
                const auto* spec = find_component_spec(id);
                if (!spec) {
                    continue;
                }

                const auto comp_it = components_.find(id);
                if (comp_it != components_.end()) {
                    comp_it->second->stop();
                    if (circuit_) {
                        circuit_->remove(id);
                    }
                    if (link_engine_) {
                        link_engine_->unregister_component(id);
                    }
                    component_ptrs_.erase(id);
                    components_.erase(comp_it);
                }

                auto component = builder.build(*spec);
                if (!component) {
                    continue;
                }

                component_ptrs_[id] = component.get();
                link_engine_->register_component(id, component.get());
                circuit_->add(component.get());
                wire_component_callbacks(component.get());
                components_.emplace(id, std::move(component));

                if (spec->enabled) {
                    auto_start.push_back(component_ptrs_[id]);
                }
            }

            if (link_engine_) {
                link_engine_->sync_links();
            }
        }
    }

    start_components_unlocked(auto_start);
    return true;
}

std::vector<ValidationError> GraphRuntime::validate() const {
    std::lock_guard lock(graph_mutex_);
    return validator_->validate(schema_);
}

bool GraphRuntime::persist() {
    std::lock_guard lock(graph_mutex_);
    if (path_.empty()) {
        return false;
    }
    return PersistenceManager{path_}.save(schema_);
}

bool GraphRuntime::reconcile_locked() {
    std::unordered_set<std::string> schema_ids;
    for (const auto& spec : schema_.components) {
        if (spec.enabled) schema_ids.insert(spec.id);
    }

    std::unordered_set<std::string> runtime_ids;
    for (const auto& [id, _] : components_) {
        runtime_ids.insert(id);
    }

    for (const auto& id : runtime_ids) {
        if (schema_ids.count(id)) continue;
        auto* spec = find_component_spec(id);
        if (spec && spec->enabled) continue;
        remove_component(id);
    }

    GraphBuilder builder(bus_, orchestrator_, vpn_monitor_, compression_caps_);
    std::vector<OdcComponent*> to_start;

    for (const auto& spec : schema_.components) {
        if (!spec.enabled) continue;

        const auto comp_it = components_.find(spec.id);
        if (comp_it != components_.end()) {
            const auto& old_config = comp_it->second->config();
            if (old_config != spec.config) {
                comp_it->second->configure(spec.config);
            }
        } else {
            auto component = builder.build(spec);
            if (!component) continue;

            const auto id = component->id();
            component_ptrs_[id] = component.get();
            link_engine_->register_component(id, component.get());
            circuit_->add(component.get());
            wire_component_callbacks(component.get());
            components_.emplace(id, std::move(component));
            to_start.push_back(component_ptrs_[id]);
        }
    }

    if (link_engine_) {
        link_engine_->sync_links();
    }

    for (auto* c : to_start) {
        if (c->type() == "cellular.qmi" || c->type() == "mavlink.router"
            || c->type().starts_with("vpn.")) {
            c->start();
        }
    }

    return true;
}

bool GraphRuntime::ensure_component_runtime(const std::string& id) {
    if (component_ptrs_.count(id) > 0) {
        return true;
    }
    if (!running_) {
        return false;
    }

    const auto* spec = find_component_spec(id);
    if (!spec) {
        return false;
    }

    GraphBuilder builder(bus_, orchestrator_, vpn_monitor_, compression_caps_);
    auto component = builder.build(*spec);
    if (!component) {
        return false;
    }

    component_ptrs_[id] = component.get();
    link_engine_->register_component(id, component.get());
    circuit_->add(component.get());
    wire_component_callbacks(component.get());
    components_.emplace(id, std::move(component));
    return true;
}

bool GraphRuntime::start_component(const std::string& id) {
    OdcComponent* component = nullptr;

    {
        std::lock_guard lock(graph_mutex_);
        if (!ensure_component_runtime(id)) {
            return false;
        }

        if (auto* spec = find_component_spec(id)) {
            spec->enabled = true;
        }

        component = component_ptrs_[id];
        if (component) {
            component->set_runtime_enabled(true);
        }
    }

    if (!component) {
        return false;
    }

    component->start();

    {
        std::lock_guard lock(graph_mutex_);
        if (component->type().starts_with("vpn.")) {
            replay_vpn_triggers_for_unlocked(id);
        } else if (component->type() == "mavlink.router") {
            if (link_engine_) {
                link_engine_->apply_active_router_links(id);
            }
        }
    }

    return component->state() != ComponentRuntimeState::Error;
}

bool GraphRuntime::stop_component(const std::string& id) {
    OdcComponent* component = nullptr;
    {
        std::lock_guard lock(graph_mutex_);
        component = find_component(id);
    }
    if (!component) {
        return false;
    }
    component->stop();
    return true;
}

bool GraphRuntime::connect_component(const std::string& id) {
    OdcComponent* component = nullptr;
    {
        std::lock_guard lock(graph_mutex_);
        if (!ensure_component_runtime(id)) {
            return false;
        }
        component = find_component(id);
    }
    if (!component || component->type() != "cellular.qmi") {
        return false;
    }
    if (component->state() != ComponentRuntimeState::Running) {
        component->start();
    }
    auto* cellular = dynamic_cast<CellularWanComponent*>(component);
    if (!cellular) {
        return false;
    }
    return cellular->request_connect();
}

bool GraphRuntime::reset_component(const std::string& id) {
    OdcComponent* component = nullptr;
    {
        std::lock_guard lock(graph_mutex_);
        if (!ensure_component_runtime(id)) {
            return false;
        }
        component = find_component(id);
    }
    if (!component || component->type() != "cellular.qmi") {
        return false;
    }
    if (component->state() != ComponentRuntimeState::Running) {
        component->start();
    }
    auto* cellular = dynamic_cast<CellularWanComponent*>(component);
    if (!cellular) {
        return false;
    }
    return cellular->request_hard_reset();
}

nlohmann::json GraphRuntime::list_component_profiles(const std::string& id, std::string* error_out) {
    OdcComponent* component = nullptr;
    {
        std::lock_guard lock(graph_mutex_);
        if (!ensure_component_runtime(id)) {
            if (error_out) *error_out = "component not found";
            return nlohmann::json::array();
        }
        component = find_component(id);
    }
    if (!component || component->type() != "cellular.qmi") {
        if (error_out) *error_out = "cellular.qmi only";
        return nlohmann::json::array();
    }
    auto* cellular = dynamic_cast<CellularWanComponent*>(component);
    if (!cellular) {
        if (error_out) *error_out = "not a cellular component";
        return nlohmann::json::array();
    }

    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;
    nlohmann::json profiles = nlohmann::json::array();
    std::string error;

    cellular->list_profiles([&](bool success, nlohmann::json result, std::string err) {
        std::lock_guard lock(mu);
        ok = success;
        profiles = std::move(result);
        error = std::move(err);
        done = true;
        cv.notify_one();
    });

    {
        std::unique_lock lock(mu);
        if (!cv.wait_for(lock, std::chrono::seconds(20), [&] { return done; })) {
            if (error_out) *error_out = "timeout waiting for profile list";
            return nlohmann::json::array();
        }
    }
    if (!ok && error_out) {
        *error_out = error.empty() ? "list profiles failed" : error;
    }
    return profiles;
}

void GraphRuntime::start_components_unlocked(const std::vector<OdcComponent*>& components) const {
    for (auto* component : components) {
        if (!component) {
            continue;
        }
        const auto& type = component->type();
        if (type == "compression.capabilities" || type == "cellular.qmi" ||
            type == "mavlink.router" || type.starts_with("vpn.")) {
            component->start();
        }
    }
}

void GraphRuntime::ensure_capabilities_component_locked() {
    const bool present = std::any_of(
        schema_.components.begin(), schema_.components.end(),
        [](const GraphComponentSpec& spec) {
            return spec.type == "compression.capabilities";
        });
    if (present) {
        return;
    }

    // Runtime-only inventory node (not persisted unless operator adds it).
    GraphComponentSpec spec;
    spec.id = "compression-capabilities";
    spec.type = "compression.capabilities";
    spec.enabled = true;
    spec.config = nlohmann::json{{"auto_refresh_on_start", true}};

    GraphBuilder builder(bus_, orchestrator_, vpn_monitor_, compression_caps_);
    auto component = builder.build(spec);
    if (!component) {
        return;
    }
    const auto id = component->id();
    component_ptrs_[id] = component.get();
    if (link_engine_) {
        link_engine_->register_component(id, component.get());
    }
    if (circuit_) {
        circuit_->add(component.get());
    }
    wire_component_callbacks(component.get());
    components_.emplace(id, std::move(component));
}

void GraphRuntime::build_runtime_locked(std::vector<OdcComponent*>* auto_start) {
    components_.clear();
    component_ptrs_.clear();

    circuit_ = std::make_unique<CircuitRegistry>();
    link_engine_ = std::make_unique<LinkEngine>(schema_, circuit_->circuit());
    trigger_eval_ = std::make_unique<TriggerEvaluator>(schema_, link_engine_.get(), &component_ptrs_);

    GraphBuilder builder(bus_, orchestrator_, vpn_monitor_, compression_caps_);
    for (const auto& spec : schema_.components) {
        if (!spec.enabled) {
            continue;
        }

        auto component = builder.build(spec);
        if (!component) {
            continue;
        }

        const auto id = component->id();
        component_ptrs_[id] = component.get();
        link_engine_->register_component(id, component.get());
        circuit_->add(component.get());
        wire_component_callbacks(component.get());
        components_.emplace(id, std::move(component));
    }

    ensure_capabilities_component_locked();

    link_engine_->register_links();

    if (!event_sub_) {
        event_sub_ = bus_.subscribe([this](const Event& evt) {
            on_event(evt);
        });
    }

    if (!auto_start) {
        return;
    }

    for (auto& [id, component] : components_) {
        if (component->type() == "compression.capabilities" ||
            component->type() == "cellular.qmi" || component->type() == "mavlink.router" ||
            component->type().starts_with("vpn.")) {
            auto_start->push_back(component.get());
        }
    }
}

void GraphRuntime::wire_component_callbacks(OdcComponent* component) {
    if (!component) {
        return;
    }

    if (auto* cellular = dynamic_cast<CellularWanComponent*>(component)) {
        cellular->set_wan_ready_callback([this, id = component->id()](const WanReady& wan) {
            std::lock_guard lock(graph_mutex_);
            if (trigger_eval_) {
                trigger_eval_->on_cellular_wan_ready(id, wan);
            }
        });
        return;
    }
}

void GraphRuntime::stop_components_ordered() {
    const auto stop_type = [this](const std::string& type) {
        for (auto& [id, component] : components_) {
            if (component->type() == type) {
                component->stop();
            }
        }
    };

    stop_type("rtsp.compressor");
    stop_type("mavlink.extender");
    stop_type("mavlink.router");

    for (auto& [id, component] : components_) {
        if (component->type().starts_with("vpn.")) {
            component->stop();
        }
    }

    stop_type("cellular.qmi");
    stop_type("compression.capabilities");
}

void GraphRuntime::on_event(const Event& evt) {
    if (const auto* vpn_evt = std::get_if<VpnStateChangeEvent>(&evt)) {
        std::lock_guard lock(graph_mutex_);
        if (!running_ || !trigger_eval_) {
            return;
        }

        if (vpn_evt->new_state == VpnState::Connected) {
            trigger_eval_->on_vpn_connected(vpn_evt->connection_id);
        } else if (vpn_evt->new_state == VpnState::Disconnected || vpn_evt->new_state == VpnState::Error) {
            trigger_eval_->on_vpn_disconnected(vpn_evt->connection_id);
        }
        return;
    }

    if (const auto* qmi_evt = std::get_if<qmi::QmiConnectionEvent>(&evt)) {
        std::lock_guard lock(graph_mutex_);
        if (!running_ || !trigger_eval_ || qmi_evt->ipv4_address.empty()) {
            return;
        }

        for (auto& [id, component] : components_) {
            if (component->type() != "cellular.qmi") {
                continue;
            }

            WanReady wan;
            wan.component_id = id;
            wan.iface_name = qmi_evt->iface_name;
            wan.ipv4_address = qmi_evt->ipv4_address;
            wan.ready = true;
            trigger_eval_->on_cellular_wan_ready(id, wan);
        }
        return;
    }

    if (const auto* health_evt = std::get_if<VpnHealthCheckEvent>(&evt)) {
        std::lock_guard lock(graph_mutex_);
        if (!running_ || !trigger_eval_) {
            return;
        }
        if (!health_evt->healthy && health_evt->consecutive_failures >= 3) {
            trigger_eval_->on_vpn_disconnected(health_evt->connection_id);
        }
    }
}

GraphComponentSpec* GraphRuntime::find_component_spec(const std::string& id) {
    const auto it = std::find_if(schema_.components.begin(), schema_.components.end(),
        [&](GraphComponentSpec& spec) { return spec.id == id; });
    return it != schema_.components.end() ? &(*it) : nullptr;
}

OdcComponent* GraphRuntime::find_component(const std::string& id) {
    const auto it = component_ptrs_.find(id);
    return it != component_ptrs_.end() ? it->second : nullptr;
}

MavRouter* GraphRuntime::primary_mav_router() {
    std::lock_guard lock(graph_mutex_);
    for (auto& [id, component] : components_) {
        (void)id;
        if (!component || component->type() != "mavlink.router") {
            continue;
        }
        auto* router_comp = dynamic_cast<MavRouterComponent*>(component.get());
        if (!router_comp) {
            continue;
        }
        if (auto* router = router_comp->get_router()) {
            return router;
        }
    }
    return nullptr;
}

bool GraphRuntime::refresh_compression_capabilities() {
    if (!compression_caps_) {
        return false;
    }
    return compression_caps_->refresh();
}

void GraphRuntime::remove_links_for_component(const std::string& id) {
    schema_.links.erase(
        std::remove_if(schema_.links.begin(), schema_.links.end(),
            [&](const GraphLinkSpec& link) {
                return link.from.component == id || link.to.component == id;
            }),
        schema_.links.end());

    if (link_engine_) {
        link_engine_->sync_links();
    }
}

void GraphRuntime::remove_triggers_for_component(const std::string& id) {
    schema_.triggers.erase(
        std::remove_if(schema_.triggers.begin(), schema_.triggers.end(),
            [&](const GraphTriggerSpec& trigger) { return trigger.source_component == id; }),
        schema_.triggers.end());
}

void GraphRuntime::replay_vpn_triggers_for(const std::string& source_id) {
    std::lock_guard lock(graph_mutex_);
    replay_vpn_triggers_for_unlocked(source_id);
}

void GraphRuntime::replay_vpn_triggers_for_unlocked(const std::string& source_id) {
    if (!running_ || !trigger_eval_) {
        return;
    }

    auto* vpn = dynamic_cast<VpnTunnelComponent*>(find_component(source_id));
    if (!vpn) {
        return;
    }

    const auto binding = vpn->get_binding();
    if (binding.vpn_state == VpnState::Connected || binding.ip_ready) {
        trigger_eval_->on_vpn_connected(source_id);
    }
}

} // namespace odc::graph
