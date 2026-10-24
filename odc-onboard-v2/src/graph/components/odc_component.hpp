#pragma once

#include "graph/transport_binding.hpp"

#include <DSPatch.h>
#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace odc::graph {

class OdcComponent : public DSPatch::Component {
public:
    OdcComponent(std::string id, std::string type, nlohmann::json config);
    ~OdcComponent() override = default;

    OdcComponent(const OdcComponent&) = delete;
    OdcComponent& operator=(const OdcComponent&) = delete;

    const std::string& id() const { return id_; }
    const std::string& type() const { return type_; }
    bool enabled() const { return enabled_; }
    ComponentRuntimeState state() const { return state_; }
    const nlohmann::json& config() const { return config_; }

    virtual void configure(const nlohmann::json& config);
    void set_runtime_enabled(bool enabled) { enabled_ = enabled; }
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void arm() {}
    virtual void apply_binding(const TransportBinding& /*binding*/) {}
    virtual void apply_wan(const WanReady& /*wan*/) {}
    virtual void apply_router(const RouterRef& /*router*/) {}
    virtual nlohmann::json status_json() const = 0;

protected:
    void set_state(ComponentRuntimeState state) { state_ = state; }
    void set_enabled(bool enabled) { enabled_ = enabled; }

    std::string id_;
    std::string type_;
    bool enabled_{true};
    ComponentRuntimeState state_{ComponentRuntimeState::Stopped};
    nlohmann::json config_;

    void Process_(DSPatch::SignalBus& /*inputs*/, DSPatch::SignalBus& /*outputs*/) override {}
};

inline OdcComponent::OdcComponent(std::string id, std::string type, nlohmann::json config)
    : id_(std::move(id)), type_(std::move(type)), config_(std::move(config)) {
    if (config_.contains("enabled") && config_["enabled"].is_boolean()) {
        enabled_ = config_["enabled"].get<bool>();
    }
}

inline void OdcComponent::configure(const nlohmann::json& config) {
    config_ = config;
    if (config_.contains("enabled") && config_["enabled"].is_boolean()) {
        enabled_ = config_["enabled"].get<bool>();
    }
}

} // namespace odc::graph
