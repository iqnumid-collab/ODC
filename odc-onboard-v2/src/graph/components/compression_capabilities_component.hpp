#pragma once

#include "graph/components/odc_component.hpp"
#include "rtsp/compression_capabilities.hpp"

#include <memory>

namespace odc::graph {

/// Read-only graph node exposing host compression/GPU encoder inventory.
/// start()/refresh re-probes and autosaves capabilities.
class CompressionCapabilitiesComponent final : public OdcComponent {
public:
    static constexpr int kOutputStatus = 0;
    static constexpr int kOutputInventory = 1;

    CompressionCapabilitiesComponent(
        std::shared_ptr<CompressionCapabilities> caps,
        std::string id,
        nlohmann::json config);

    void configure(const nlohmann::json& config) override;
    void start() override;
    void stop() override;
    nlohmann::json status_json() const override;
    void Process_(DSPatch::SignalBus& inputs, DSPatch::SignalBus& outputs) override;

    bool refresh();

private:
    std::shared_ptr<CompressionCapabilities> caps_;
    bool auto_refresh_on_start_{true};
};

} // namespace odc::graph
