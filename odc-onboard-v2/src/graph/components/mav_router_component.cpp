#include "graph/components/mav_router_component.hpp"

#include "core/event_bus.hpp"
#include "thread_orchestrator/cancellation_token.hpp"

namespace odc::graph {

MavRouterComponent::MavRouterComponent(
    EventBus& bus,
    orch::ThreadOrchestrator& orchestrator,
    std::string id,
    nlohmann::json config)
    : OdcComponent(std::move(id), "mavlink.router", std::move(config)),
      bus_(bus),
      orchestrator_(orchestrator),
      mav_cfg_(parse_mavlink_config(config_)) {
    SetOutputCount_(1, {"router"});
    mav_cfg_.enabled = enabled_;
}

MavRouterComponent::~MavRouterComponent() {
    stop();
}

void MavRouterComponent::configure(const nlohmann::json& config) {
    OdcComponent::configure(config);
    mav_cfg_ = parse_mavlink_config(config_);
    mav_cfg_.enabled = enabled_;
}

void MavRouterComponent::start() {
    if (!enabled_ || state_ == ComponentRuntimeState::Running) {
        return;
    }

    set_state(ComponentRuntimeState::Starting);

    if (!router_) {
        router_ = std::make_shared<MavRouter>(mav_cfg_, bus_, "");
    }

    if (!cancel_) {
        cancel_ = std::make_unique<orch::CancellationSource>();
    }

    auto router_ptr = router_;
    auto token = cancel_->token();

    orchestrator_.submit_simple([router_ptr, token]() mutable {
        if (router_ptr) {
            router_ptr->run(token);
        }
    });

    set_state(ComponentRuntimeState::Running);
}

void MavRouterComponent::stop() {
    if (state_ == ComponentRuntimeState::Stopped) {
        return;
    }

    set_state(ComponentRuntimeState::Stopping);

    if (router_) {
        router_->request_stop();
    }

    if (cancel_) {
        cancel_->cancel();
        cancel_.reset();
    }

    router_.reset();
    set_state(ComponentRuntimeState::Stopped);
}

nlohmann::json MavRouterComponent::status_json() const {
    nlohmann::json status{
        {"id", id_},
        {"type", type_},
        {"enabled", enabled_},
        {"state", std::string(to_string_view(state_))},
        {"tcp_server_port", mav_cfg_.tcp_server_port},
        {"endpoint_count", mav_cfg_.endpoints.size()},
    };

    if (router_) {
        auto router_status = router_->get_status();
        status["running"] = router_status.running;
        status["connected_count"] = router_status.connected_count;
    }

    return status;
}

MavRouter* MavRouterComponent::get_router() {
    return router_.get();
}

MavlinkConfig MavRouterComponent::parse_mavlink_config(const nlohmann::json& config) {
    nlohmann::json wrapped = {{"mavlink", config}};
    auto parsed = odc::parse_mavlink_config(wrapped);
    return parsed.value_or(MavlinkConfig{});
}

void MavRouterComponent::Process_(DSPatch::SignalBus&, DSPatch::SignalBus& outputs) {
    if (router_) {
        RouterRef ref;
        ref.router = router_.get();
        outputs.SetValue(kOutputRouter, ref);
    } else {
        outputs.ClearValue(kOutputRouter);
    }
}

} // namespace odc::graph
