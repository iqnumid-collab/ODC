#pragma once

#include "graph/components/odc_component.hpp"
#include "mavlink/mav_config.hpp"
#include "mavlink/mav_router.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace orch {
class ThreadOrchestrator;
class CancellationSource;
}

namespace odc {

class EventBus;

namespace graph {

class MavExtenderComponent final : public OdcComponent {
public:
    static constexpr int kInputTransport = 0;
    static constexpr int kInputUpstream  = 1;
    static constexpr int kOutputStatus   = 0;

    MavExtenderComponent(
        EventBus& bus,
        ::orch::ThreadOrchestrator& orchestrator,
        std::string id,
        nlohmann::json config);

    ~MavExtenderComponent() override;

    void configure(const nlohmann::json& config) override;
    void start() override;
    void stop() override;
    void arm() override;
    void apply_binding(const TransportBinding& binding) override;
    void apply_router(const RouterRef& router) override;
    nlohmann::json status_json() const override;

    void Process_(DSPatch::SignalBus& inputs, DSPatch::SignalBus& outputs) override;

private:
    MavlinkConfig parse_base_config(const nlohmann::json& config) const;
    MavlinkConfig build_extender_config(const std::string& iface, const std::string& target_ip) const;
    void start_extender(const TransportBinding& binding);
    void teardown_extender();

    EventBus& bus_;
    ::orch::ThreadOrchestrator& orchestrator_;
    MavlinkConfig base_cfg_;
    uint16_t udp_port_{14550};

    mutable std::mutex mutex_;
    std::shared_ptr<MavRouter> router_;
    std::unique_ptr<::orch::CancellationSource> cancel_;
    MavRouter* upstream_router_{nullptr};
    std::optional<TransportBinding> pending_binding_;
    bool active_{false};
};

} // namespace graph
} // namespace odc
