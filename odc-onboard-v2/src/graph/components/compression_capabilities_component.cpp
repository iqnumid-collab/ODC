#include "graph/components/compression_capabilities_component.hpp"

namespace odc::graph {

CompressionCapabilitiesComponent::CompressionCapabilitiesComponent(
    std::shared_ptr<CompressionCapabilities> caps,
    std::string id,
    nlohmann::json config)
    : OdcComponent(std::move(id), "compression.capabilities", std::move(config)),
      caps_(std::move(caps)) {
    SetInputCount_(0);
    SetOutputCount_(2, {"status", "inventory"});
    auto_refresh_on_start_ = config_.value("auto_refresh_on_start", true);
    set_state(ComponentRuntimeState::Stopped);
}

void CompressionCapabilitiesComponent::configure(const nlohmann::json& config) {
    OdcComponent::configure(config);
    auto_refresh_on_start_ = config_.value("auto_refresh_on_start", true);
}

void CompressionCapabilitiesComponent::start() {
    if (!caps_) {
        set_state(ComponentRuntimeState::Error);
        return;
    }
    if (auto_refresh_on_start_) {
        caps_->ensure(false);
    }
    set_state(ComponentRuntimeState::Running);
}

void CompressionCapabilitiesComponent::stop() {
    set_state(ComponentRuntimeState::Stopped);
}

bool CompressionCapabilitiesComponent::refresh() {
    if (!caps_) return false;
    const bool ok = caps_->refresh();
    if (ok) set_state(ComponentRuntimeState::Running);
    else set_state(ComponentRuntimeState::Error);
    return ok;
}

nlohmann::json CompressionCapabilitiesComponent::status_json() const {
    nlohmann::json status{
        {"id", id_},
        {"type", type_},
        {"enabled", enabled_},
        {"state", std::string(to_string_view(state_))},
        {"auto_refresh_on_start", auto_refresh_on_start_},
    };
    if (caps_) {
        status["capabilities"] = caps_->to_json();
    } else {
        status["capabilities"] = nlohmann::json{{"ready", false}, {"error", "no_service"}};
    }
    return status;
}

void CompressionCapabilitiesComponent::Process_(DSPatch::SignalBus& /*inputs*/,
                                                DSPatch::SignalBus& outputs) {
    nlohmann::json inventory = caps_ ? caps_->to_json() : nlohmann::json{{"ready", false}};
    outputs.SetValue(kOutputStatus, nlohmann::json{
        {"id", id_},
        {"ready", inventory.value("ready", false)},
    });
    outputs.SetValue(kOutputInventory, std::move(inventory));
}

} // namespace odc::graph
