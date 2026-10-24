#pragma once

#include "graph/components/odc_component.hpp"
#include "core/event.hpp"
#include "core/event_bus.hpp"
#include "core/vpn_monitor.hpp"
#include "vpn/vpn_connection.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace odc::graph {

class VpnTunnelComponent final : public OdcComponent {
public:
    static constexpr int kInputWan      = 0;
    static constexpr int kOutputTunnel  = 0;
    static constexpr int kOutputState   = 1;

    using BindingCallback = std::function<void(const TransportBinding&)>;

    VpnTunnelComponent(
        EventBus& bus,
        VpnInterfaceMonitor* monitor,
        std::string id,
        std::string type,
        nlohmann::json config);

    ~VpnTunnelComponent() override;

    void configure(const nlohmann::json& config) override;
    void start() override;
    void stop() override;
    nlohmann::json status_json() const override;

    void Process_(DSPatch::SignalBus& inputs, DSPatch::SignalBus& outputs) override;

    void set_wan_ready(bool ready);
    void apply_wan(const WanReady& wan) override;
    void set_binding_callback(BindingCallback cb);

    TransportBinding get_binding() const;
    const std::string& connection_id() const { return id_; }

private:
    void ensure_connection();
    void on_event(const Event& evt);
    void on_vpn_state_change(const VpnStateChangeEvent& evt);
    void on_vpn_interface_event(const VpnInterfaceEvent& evt);
    void on_health_check(const VpnHealthCheckEvent& evt);
    void emit_binding();
    void clear_binding();
    std::string expected_interface_name() const;

    EventBus& bus_;
    VpnInterfaceMonitor* monitor_;
    std::unique_ptr<VpnConnection> connection_;
    VpnType vpn_type_{VpnType::WireGuard};

    std::string config_path_;
    std::string creds_path_;
    std::string wan_component_id_;

    mutable std::mutex mutex_;
    TransportBinding binding_;
    BindingCallback binding_callback_;
    bool wan_ready_{false};
    bool started_{false};
    bool health_healthy_{true};
    uint32_t health_consecutive_failures_{0};

    EventBus::SubscriptionId event_sub_{0};
};

} // namespace odc::graph
