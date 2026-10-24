#include "graph/components/mav_extender_component.hpp"

#include "core/event.hpp"
#include "core/event_bus.hpp"
#include "mavlink/mav_events.hpp"
#include "thread_orchestrator/cancellation_token.hpp"

namespace odc::graph {

MavExtenderComponent::MavExtenderComponent(
    EventBus& bus,
    orch::ThreadOrchestrator& orchestrator,
    std::string id,
    nlohmann::json config)
    : OdcComponent(std::move(id), "mavlink.extender", std::move(config)),
      bus_(bus),
      orchestrator_(orchestrator),
      base_cfg_(parse_base_config(config_)) {
    SetInputCount_(2, {"transport", "upstream"});
    SetOutputCount_(1, {"status"});
    udp_port_ = static_cast<uint16_t>(config_.value("udp_port", 14550));
}

MavExtenderComponent::~MavExtenderComponent() {
    stop();
}

void MavExtenderComponent::configure(const nlohmann::json& config) {
    OdcComponent::configure(config);
    base_cfg_ = parse_base_config(config_);
    udp_port_ = static_cast<uint16_t>(config_.value("udp_port", 14550));
}

void MavExtenderComponent::start() {
    if (!enabled_) {
        return;
    }

    if (state_ == ComponentRuntimeState::Running) {
        return;
    }

    set_state(ComponentRuntimeState::Running);

    std::optional<TransportBinding> pending;
    {
        std::lock_guard lock(mutex_);
        pending = pending_binding_;
    }

    if (pending && pending->ip_ready) {
        start_extender(*pending);
    }
}

void MavExtenderComponent::stop() {
    if (state_ == ComponentRuntimeState::Stopped) {
        return;
    }

    set_state(ComponentRuntimeState::Stopping);
    teardown_extender();

    {
        std::lock_guard lock(mutex_);
        pending_binding_.reset();
    }

    set_state(ComponentRuntimeState::Stopped);
}

void MavExtenderComponent::arm() {
    if (!enabled_) {
        return;
    }
    set_state(ComponentRuntimeState::Armed);
}

void MavExtenderComponent::apply_binding(const TransportBinding& binding) {
    if (!enabled_) {
        return;
    }

    {
        std::lock_guard lock(mutex_);
        pending_binding_ = binding;
    }

    if (!binding.ip_ready) {
        return;
    }

    if (state_ != ComponentRuntimeState::Running &&
        state_ != ComponentRuntimeState::Armed) {
        return;
    }

    start_extender(binding);
}

void MavExtenderComponent::apply_router(const RouterRef& router) {
    std::lock_guard lock(mutex_);
    upstream_router_ = static_cast<MavRouter*>(router.router);
}

nlohmann::json MavExtenderComponent::status_json() const {
    std::lock_guard lock(mutex_);

    nlohmann::json status{
        {"id", id_},
        {"type", type_},
        {"enabled", enabled_},
        {"state", std::string(to_string_view(state_))},
        {"udp_port", udp_port_},
        {"active", active_},
    };

    if (pending_binding_) {
        status["target_ip"] = pending_binding_->target_ip;
        status["interface_name"] = pending_binding_->interface_name;
        status["ip_ready"] = pending_binding_->ip_ready;
    }

    if (router_) {
        auto endpoints = router_->get_endpoints();
        uint64_t messages_in = 0;
        uint64_t messages_out = 0;
        for (const auto& ep : endpoints) {
            messages_in += ep.messages_in;
            messages_out += ep.messages_out;
        }
        status["messages_in"] = messages_in;
        status["messages_out"] = messages_out;
    }

    return status;
}

MavlinkConfig MavExtenderComponent::parse_base_config(const nlohmann::json& config) const {
    MavlinkConfig cfg;
    cfg.dedup_period_sec = config.value("dedup_period_sec", 0);
    cfg.sniffer_sysid = config.value("sniffer_sysid", 0);
    cfg.report_stats = config.value("report_stats", true);

    if (config.contains("endpoints") && config["endpoints"].is_array()) {
        nlohmann::json wrapped = {{"mavlink", config}};
        if (auto parsed = odc::parse_mavlink_config(wrapped)) {
            cfg.endpoints = parsed->endpoints;
            if (parsed->dedup_period_sec > 0) {
                cfg.dedup_period_sec = parsed->dedup_period_sec;
            }
            cfg.sniffer_sysid = parsed->sniffer_sysid;
            cfg.report_stats = parsed->report_stats;
        }
    }

    return cfg;
}

MavlinkConfig MavExtenderComponent::build_extender_config(
    const std::string& iface,
    const std::string& target_ip) const {
    MavlinkConfig ext_cfg;
    ext_cfg.enabled = true;
    ext_cfg.tcp_server_port = 0;
    ext_cfg.dedup_period_sec = base_cfg_.dedup_period_sec;
    ext_cfg.sniffer_sysid = base_cfg_.sniffer_sysid;
    ext_cfg.report_stats = base_cfg_.report_stats;

    MavEndpointConfig upstream;
    upstream.type = "udp";
    upstream.name = "upstream_" + iface;
    upstream.address = target_ip;
    upstream.port = static_cast<int>(udp_port_);
    upstream.mode = "client";
    upstream.retry_timeout_sec = 5;
    ext_cfg.endpoints.push_back(std::move(upstream));

    for (const auto& ep : base_cfg_.endpoints) {
        if (ep.type == "uart") {
            ext_cfg.endpoints.push_back(ep);
        }
    }

    return ext_cfg;
}

void MavExtenderComponent::start_extender(const TransportBinding& binding) {
    if (binding.target_ip.empty() || binding.interface_name.empty()) {
        return;
    }

    std::unique_lock lock(mutex_);
    if (active_ &&
        pending_binding_ &&
        pending_binding_->target_ip == binding.target_ip &&
        pending_binding_->interface_name == binding.interface_name) {
        return;
    }

    if (cancel_) {
        cancel_->cancel();
    }
    router_.reset();
    cancel_.reset();

    auto ext_cfg = build_extender_config(binding.interface_name, binding.target_ip);
    router_ = std::make_shared<MavRouter>(ext_cfg, bus_, id_);
    cancel_ = std::make_unique<orch::CancellationSource>();
    pending_binding_ = binding;
    active_ = true;

    auto router_ptr = router_;
    auto token = cancel_->token();
    lock.unlock();

    orchestrator_.submit_simple([router_ptr, token]() mutable {
        if (router_ptr) {
            router_ptr->run(token);
        }
    });

    MavExtenderStatusEvent status_evt;
    status_evt.extender_id = id_;
    status_evt.vpn_connection_id = binding.connection_id;
    status_evt.vpn_interface_name = binding.interface_name;
    status_evt.target_ip = binding.target_ip;
    status_evt.target_port = udp_port_;
    status_evt.connected = true;
    bus_.publish(Event{std::move(status_evt)});
}

void MavExtenderComponent::teardown_extender() {
    std::string extender_id;
    std::string conn_id;
    std::string vpn_iface;
    std::string target_ip;

    {
        std::unique_lock lock(mutex_);
        if (!active_ && !router_) {
            return;
        }

        extender_id = id_;
        if (pending_binding_) {
            conn_id = pending_binding_->connection_id;
            vpn_iface = pending_binding_->interface_name;
            target_ip = pending_binding_->target_ip;
        }

        if (cancel_) {
            cancel_->cancel();
        }

        router_.reset();
        cancel_.reset();
        active_ = false;
    }

    MavExtenderStatusEvent status_evt;
    status_evt.extender_id = extender_id;
    status_evt.vpn_connection_id = conn_id;
    status_evt.vpn_interface_name = vpn_iface;
    status_evt.target_ip = target_ip;
    status_evt.target_port = udp_port_;
    status_evt.connected = false;
    bus_.publish(Event{std::move(status_evt)});
}

void MavExtenderComponent::Process_(DSPatch::SignalBus& inputs, DSPatch::SignalBus& outputs) {
    if (auto* binding = inputs.GetValue<TransportBinding>(kInputTransport)) {
        apply_binding(*binding);
    }

    if (auto* router_ref = inputs.GetValue<RouterRef>(kInputUpstream)) {
        apply_router(*router_ref);
    }

    outputs.SetValue(kOutputStatus, nlohmann::json{
        {"id", id_},
        {"active", active_},
    });
}

} // namespace odc::graph
