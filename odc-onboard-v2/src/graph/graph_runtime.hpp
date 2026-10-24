#pragma once

#include "graph/graph_schema.hpp"
#include "graph/graph_events.hpp"
#include "graph/graph_validator.hpp"
#include "core/event.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace orch {
class ThreadOrchestrator;
}

namespace odc {

class EventBus;
class VpnInterfaceMonitor;
class MavRouter;
class CompressionCapabilities;

namespace graph {

class LinkEngine;
class TriggerEvaluator;
class CircuitRegistry;
class GraphValidator;
class OdcComponent;

class GraphRuntime {
public:
    GraphRuntime(
        EventBus& bus,
        ::orch::ThreadOrchestrator& orchestrator,
        VpnInterfaceMonitor* vpn_monitor,
        std::shared_ptr<CompressionCapabilities> compression_caps = nullptr);

    ~GraphRuntime();

    bool load(const std::filesystem::path& path);
    bool start();
    void stop();
    void tick();

    nlohmann::json status_json() const;

    bool add_component(const GraphComponentSpec& spec);
    bool remove_component(const std::string& id);
    bool configure_component(const std::string& id, const nlohmann::json& config);
    bool add_link(const GraphLinkSpec& link);
    bool remove_link(const std::string& id);
    bool set_trigger(const GraphTriggerSpec& trigger);
    bool apply(const std::vector<std::string>& component_ids = {}, bool force = false);
    std::vector<ValidationError> validate() const;
    bool persist();

    bool start_component(const std::string& id);
    bool stop_component(const std::string& id);
    /** Explicit cellular PDP connect (honors auto_connect=false dormancy). */
    bool connect_component(const std::string& id);
    /** Cellular hard reset (DMS/USB). */
    bool reset_component(const std::string& id);
    /** List WDS 3GPP profiles for a cellular component (blocking wait up to timeout). */
    nlohmann::json list_component_profiles(const std::string& id, std::string* error_out = nullptr);

    /** First running mavlink.router component's router, or nullptr. */
    MavRouter* primary_mav_router();

    /** Host compression/GPU inventory (shared with compressor rules). */
    std::shared_ptr<CompressionCapabilities> compression_capabilities() const {
        return compression_caps_;
    }

    /** Force re-detect + autosave of compression capabilities. */
    bool refresh_compression_capabilities();

    bool is_running() const { return running_; }
    const GraphSchema& schema() const { return schema_; }

private:
    void build_runtime_locked(std::vector<OdcComponent*>* auto_start = nullptr);
    void start_components_unlocked(const std::vector<OdcComponent*>& components) const;
    void wire_component_callbacks(OdcComponent* component);
    void stop_components_ordered();
    void on_event(const Event& evt);
    GraphComponentSpec* find_component_spec(const std::string& id);
    OdcComponent* find_component(const std::string& id);
    bool ensure_component_runtime(const std::string& id);
    bool reconcile_locked();
    void remove_links_for_component(const std::string& id);
    void remove_triggers_for_component(const std::string& id);
    void replay_vpn_triggers_for(const std::string& source_id);
    void replay_vpn_triggers_for_unlocked(const std::string& source_id);
    void ensure_capabilities_component_locked();

    EventBus& bus_;
    ::orch::ThreadOrchestrator& orchestrator_;
    VpnInterfaceMonitor* vpn_monitor_;
    std::shared_ptr<CompressionCapabilities> compression_caps_;

    GraphSchema schema_;
    std::filesystem::path path_;

    std::unique_ptr<LinkEngine> link_engine_;
    std::unique_ptr<TriggerEvaluator> trigger_eval_;
    std::unique_ptr<CircuitRegistry> circuit_;
    std::unique_ptr<GraphValidator> validator_;

    std::unordered_map<std::string, std::unique_ptr<OdcComponent>> components_;
    std::unordered_map<std::string, OdcComponent*> component_ptrs_;

    mutable std::mutex graph_mutex_;
    uint64_t event_sub_{0};
    bool running_{false};
};

} // namespace graph
} // namespace odc
