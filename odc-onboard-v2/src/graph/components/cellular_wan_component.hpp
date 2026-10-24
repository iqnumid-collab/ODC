#pragma once

#include "graph/components/odc_component.hpp"
#include "core/event.hpp"
#include "core/event_bus.hpp"
#include "qmi/qmi_config.hpp"
#include "qmi/qmi_modem_controller.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace odc::graph {

class CellularWanComponent final : public OdcComponent {
public:
    static constexpr int kOutputWan     = 0;
    static constexpr int kOutputMetrics = 1;
    static constexpr const char* kWanReadyEvent = "cellular.wan_ready";

    using WanReadyCallback = std::function<void(const WanReady&)>;

    CellularWanComponent(
        EventBus& bus,
        std::string id,
        nlohmann::json config);

    ~CellularWanComponent() override;

    void configure(const nlohmann::json& config) override;
    void start() override;
    void stop() override;
    nlohmann::json status_json() const override;

    void Process_(DSPatch::SignalBus& inputs, DSPatch::SignalBus& outputs) override;

    void set_wan_ready_callback(WanReadyCallback cb);
    std::optional<WanReady> get_wan_ready() const;

    /** Explicit PDP connect when auto_connect is false (or to retry). */
    bool request_connect();
    /** Hard reset modem (DMS and/or USB per config). */
    bool request_hard_reset();
    /** List modem 3GPP profiles via WDS (async result through callback on glib thread). */
    void list_profiles(std::function<void(bool ok, nlohmann::json profiles, std::string error)> cb);

private:
    static qmi::QmiConfig parse_qmi_config(const nlohmann::json& config);

    void on_event(const Event& evt);
    void update_wan_ready(const WanReady& wan);
    void clear_wan_ready();
    void update_metrics(const CellularMetrics& metrics);

    EventBus& bus_;
    qmi::QmiConfig qmi_cfg_;
    std::unique_ptr<qmi::QmiModemController> modem_;

    mutable std::mutex mutex_;
    WanReady wan_ready_;
    CellularMetrics metrics_;
    WanReadyCallback wan_callback_;
    std::vector<std::string> last_events_;

    EventBus::SubscriptionId event_sub_{0};
};

} // namespace odc::graph
