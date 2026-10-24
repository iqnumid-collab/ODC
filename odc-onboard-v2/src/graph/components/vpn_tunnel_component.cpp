#include "graph/components/vpn_tunnel_component.hpp"

#include "core/event.hpp"
#include "core/event_bus.hpp"
#include "core/vpn_monitor.hpp"
#include "vpn/openvpn_connection.hpp"
#include "vpn/vpn_config.hpp"
#include "vpn/wireguard_connection.hpp"

namespace odc::graph {

VpnTunnelComponent::VpnTunnelComponent(
    EventBus& bus,
    VpnInterfaceMonitor* monitor,
    std::string id,
    std::string type,
    nlohmann::json config)
    : OdcComponent(std::move(id), std::move(type), std::move(config)),
      bus_(bus),
      monitor_(monitor) {
    SetInputCount_(1, {"wan"});
    SetOutputCount_(2, {"tunnel", "state"});
    if (type_ == "vpn.openvpn") {
        vpn_type_ = VpnType::OpenVPN;
    } else {
        vpn_type_ = VpnType::WireGuard;
    }
    configure(config_);
}

VpnTunnelComponent::~VpnTunnelComponent() {
    stop();
}

void VpnTunnelComponent::configure(const nlohmann::json& config) {
    OdcComponent::configure(config);
    config_path_ = config_.value("config_path", "");
    creds_path_ = config_.value("creds_path", "");
    wan_component_id_ = config_.value("wan", "");
}

void VpnTunnelComponent::start() {
    if (!enabled_ || started_) {
        return;
    }

    if (!wan_component_id_.empty() && !wan_ready_) {
        set_state(ComponentRuntimeState::Armed);
        return;
    }

    set_state(ComponentRuntimeState::Starting);

    if (!event_sub_) {
        event_sub_ = bus_.subscribe([this](const Event& evt) {
            on_event(evt);
        });
    }

    ensure_connection();
    if (!connection_) {
        set_state(ComponentRuntimeState::Error);
        return;
    }

    if (!connection_->start()) {
        set_state(ComponentRuntimeState::Error);
        return;
    }

    started_ = true;
    set_state(ComponentRuntimeState::Running);
}

void VpnTunnelComponent::stop() {
    if (state_ == ComponentRuntimeState::Stopped) {
        return;
    }

    set_state(ComponentRuntimeState::Stopping);

    if (event_sub_) {
        bus_.unsubscribe(event_sub_);
        event_sub_ = 0;
    }

    if (connection_) {
        connection_->stop();
        connection_.reset();
    }

    if (monitor_) {
        monitor_->untrack_connection(id_);
    }

    clear_binding();
    started_ = false;
    set_state(ComponentRuntimeState::Stopped);
}

nlohmann::json VpnTunnelComponent::status_json() const {
    std::lock_guard lock(mutex_);

    nlohmann::json status{
        {"id", id_},
        {"type", type_},
        {"enabled", enabled_},
        {"state", std::string(to_string_view(state_))},
        {"config_path", config_path_},
        {"wan", wan_component_id_},
        {"wan_ready", wan_ready_},
        {"health_healthy", health_healthy_},
        {"health_consecutive_failures", health_consecutive_failures_},
    };

    if (!creds_path_.empty()) {
        status["creds_path"] = creds_path_;
    }

    if (connection_) {
        status["vpn_state"] = std::string(to_string_view(connection_->state()));
        status["interface_name"] = connection_->interface_name();
    }

    status["binding"] = {
        {"connection_id", binding_.connection_id},
        {"interface_name", binding_.interface_name},
        {"target_ip", binding_.target_ip},
        {"prefix_len", binding_.prefix_len},
        {"vpn_state", std::string(to_string_view(binding_.vpn_state))},
        {"ip_ready", binding_.ip_ready},
    };

    return status;
}

void VpnTunnelComponent::set_wan_ready(bool ready) {
    wan_ready_ = ready;
    if (ready && enabled_ && !started_ && state_ != ComponentRuntimeState::Running) {
        start();
    }
}

void VpnTunnelComponent::apply_wan(const WanReady& wan) {
    if (wan.ready) {
        set_wan_ready(true);
    }
}

void VpnTunnelComponent::set_binding_callback(BindingCallback cb) {
    std::lock_guard lock(mutex_);
    binding_callback_ = std::move(cb);
    if (binding_callback_ && !binding_.connection_id.empty()) {
        binding_callback_(binding_);
    }
}

TransportBinding VpnTunnelComponent::get_binding() const {
    std::lock_guard lock(mutex_);
    return binding_;
}

void VpnTunnelComponent::ensure_connection() {
    if (connection_) {
        return;
    }

    if (config_path_.empty()) {
        return;
    }

    if (vpn_type_ == VpnType::WireGuard) {
        WireGuardConnectionConfig wg_cfg;
        wg_cfg.id = id_;
        wg_cfg.enabled = enabled_;
        wg_cfg.config_path = config_path_;
        if (config_.contains("health_check")) {
            wg_cfg.health_check = config_["health_check"].get<HealthCheckConfig>();
        }
        if (config_.contains("route_safety")) {
            wg_cfg.route_safety = config_["route_safety"].get<RouteSafetyConfig>();
        }
        if (config_.contains("max_retries")) {
            wg_cfg.max_retries = config_["max_retries"].get<uint32_t>();
        }

        connection_ = std::make_unique<WireGuardConnection>(wg_cfg);
    } else {
        OpenVPNConnectionConfig ovpn_cfg;
        ovpn_cfg.id = id_;
        ovpn_cfg.enabled = enabled_;
        ovpn_cfg.config_path = config_path_;
        if (!creds_path_.empty()) {
            ovpn_cfg.credentials_path = creds_path_;
        }
        if (config_.contains("health_check")) {
            ovpn_cfg.health_check = config_["health_check"].get<HealthCheckConfig>();
        }
        if (config_.contains("route_safety")) {
            ovpn_cfg.route_safety = config_["route_safety"].get<RouteSafetyConfig>();
        }
        if (config_.contains("max_retries")) {
            ovpn_cfg.max_retries = config_["max_retries"].get<uint32_t>();
        }

        connection_ = std::make_unique<OpenVpnConnection>(ovpn_cfg);
    }

    connection_->set_event_bus(&bus_);

    if (monitor_) {
        monitor_->track_connection(id_, expected_interface_name());
    }
}

void VpnTunnelComponent::on_event(const Event& evt) {
    if (const auto* state_evt = std::get_if<VpnStateChangeEvent>(&evt)) {
        if (state_evt->connection_id == id_) {
            on_vpn_state_change(*state_evt);
        }
        return;
    }

    if (const auto* iface_evt = std::get_if<VpnInterfaceEvent>(&evt)) {
        if (iface_evt->connection_id == id_) {
            on_vpn_interface_event(*iface_evt);
        }
        return;
    }

    if (const auto* health_evt = std::get_if<VpnHealthCheckEvent>(&evt)) {
        if (health_evt->connection_id == id_) {
            on_health_check(*health_evt);
        }
    }
}

void VpnTunnelComponent::on_vpn_state_change(const VpnStateChangeEvent& evt) {
    if (evt.new_state == VpnState::Connected) {
        std::lock_guard lock(mutex_);
        binding_.connection_id = id_;
        binding_.interface_name = connection_ ? connection_->interface_name() : expected_interface_name();
        binding_.vpn_state = VpnState::Connected;
        binding_.ip_ready = false;
        binding_.target_ip.clear();
    } else if (evt.new_state == VpnState::Disconnected || evt.new_state == VpnState::Error) {
        clear_binding();
        return;
    } else {
        return;
    }

    emit_binding();
}

void VpnTunnelComponent::on_vpn_interface_event(const VpnInterfaceEvent& evt) {
    if (evt.ipv6 || evt.ip_address.empty()) {
        return;
    }

    switch (evt.action) {
        case VpnInterfaceAction::AddressAdded:
        case VpnInterfaceAction::InterfaceUp:
            break;
        default:
            if (evt.action == VpnInterfaceAction::AddressRemoved) {
                {
                    std::lock_guard lock(mutex_);
                    binding_.ip_ready = false;
                    binding_.target_ip.clear();
                }
                emit_binding();
            }
            return;
    }

    {
        std::lock_guard lock(mutex_);
        binding_.connection_id = id_;
        binding_.interface_name = evt.interface_name.empty()
            ? expected_interface_name()
            : evt.interface_name;
        binding_.target_ip = evt.ip_address;
        binding_.prefix_len = evt.prefix_len > 0 ? evt.prefix_len : 32;
        binding_.vpn_state = connection_ ? connection_->state() : VpnState::Connected;
        binding_.ip_ready = true;
    }

    emit_binding();
}

void VpnTunnelComponent::on_health_check(const VpnHealthCheckEvent& evt) {
    std::lock_guard lock(mutex_);
    health_healthy_ = evt.healthy;
    health_consecutive_failures_ = static_cast<uint32_t>(evt.consecutive_failures);
}

void VpnTunnelComponent::emit_binding() {
    BindingCallback callback;
    TransportBinding binding_copy;
    {
        std::lock_guard lock(mutex_);
        binding_copy = binding_;
        callback = binding_callback_;
    }

    if (callback) {
        callback(binding_copy);
    }
}

void VpnTunnelComponent::clear_binding() {
    BindingCallback callback;
    TransportBinding binding_copy;
    {
        std::lock_guard lock(mutex_);
        binding_ = TransportBinding{};
        binding_.connection_id = id_;
        binding_copy = binding_;
        callback = binding_callback_;
    }

    if (callback) {
        callback(binding_copy);
    }
}

std::string VpnTunnelComponent::expected_interface_name() const {
    if (vpn_type_ == VpnType::OpenVPN) {
        return "ovpn-" + id_;
    }
    return "wg-" + id_;
}

void VpnTunnelComponent::Process_(DSPatch::SignalBus& inputs, DSPatch::SignalBus& outputs) {
    if (auto* wan = inputs.GetValue<WanReady>(kInputWan)) {
        if (wan->ready && enabled_ && !started_ && state_ != ComponentRuntimeState::Running) {
            set_wan_ready(true);
        }
    }

    TransportBinding binding_copy;
    {
        std::lock_guard lock(mutex_);
        binding_copy = binding_;
    }

    if (!binding_copy.connection_id.empty()) {
        outputs.SetValue(kOutputTunnel, binding_copy);
    } else {
        outputs.ClearValue(kOutputTunnel);
    }

    if (connection_) {
        outputs.SetValue(kOutputState, connection_->state());
    } else {
        outputs.ClearValue(kOutputState);
    }
}

} // namespace odc::graph
