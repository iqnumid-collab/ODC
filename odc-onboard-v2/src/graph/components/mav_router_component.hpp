#pragma once

#include "graph/components/odc_component.hpp"
#include "mavlink/mav_config.hpp"
#include "mavlink/mav_router.hpp"

#include <memory>

namespace orch {
class ThreadOrchestrator;
class CancellationSource;
}

namespace odc {

class EventBus;

namespace graph {

class MavRouterComponent final : public OdcComponent {
public:
    static constexpr int kOutputRouter = 0;

    MavRouterComponent(
        EventBus& bus,
        ::orch::ThreadOrchestrator& orchestrator,
        std::string id,
        nlohmann::json config);

    ~MavRouterComponent() override;

    void configure(const nlohmann::json& config) override;
    void start() override;
    void stop() override;
    nlohmann::json status_json() const override;

    void Process_(DSPatch::SignalBus& inputs, DSPatch::SignalBus& outputs) override;

    MavRouter* get_router();

private:
    static MavlinkConfig parse_mavlink_config(const nlohmann::json& config);

    EventBus& bus_;
    ::orch::ThreadOrchestrator& orchestrator_;
    MavlinkConfig mav_cfg_;

    std::shared_ptr<MavRouter> router_;
    std::unique_ptr<::orch::CancellationSource> cancel_;
};

} // namespace graph
} // namespace odc
