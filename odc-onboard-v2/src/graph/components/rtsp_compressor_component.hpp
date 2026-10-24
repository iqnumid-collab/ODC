#pragma once

#include "graph/components/odc_component.hpp"
#include "rtsp/rtsp_events.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#ifdef ODC_HAS_RTSP_COMPRESSOR
#include <glib.h>
#else
struct _GMainLoop;
typedef struct _GMainLoop GMainLoop;
#endif

class RtspBridge;

namespace orch {
class ThreadOrchestrator;
}

namespace odc {

class EventBus;
class CompressionCapabilities;

namespace graph {

class RtspCompressorComponent final : public OdcComponent {
public:
    static constexpr int kInputTransport = 0;
    static constexpr int kOutputStatus   = 0;
    static constexpr int kOutputMetrics  = 1;

    RtspCompressorComponent(
        EventBus& bus,
        ::orch::ThreadOrchestrator& orchestrator,
        std::shared_ptr<CompressionCapabilities> compression_caps,
        std::string id,
        nlohmann::json config);

    ~RtspCompressorComponent() override;

    void configure(const nlohmann::json& config) override;
    void start() override;
    void stop() override;
    void arm() override;
    void apply_binding(const TransportBinding& binding) override;
    nlohmann::json status_json() const override;

    void Process_(DSPatch::SignalBus& inputs, DSPatch::SignalBus& outputs) override;

private:
    struct CodecConfig {
        std::string input_host{"127.0.0.1"};
        int input_port{8554};
        std::string input_path{"/stream"};
        int output_port{8555};
        std::string output_path{"/stream"};
        bool input_use_tcp{false};
        std::string output_codec{"h265"};
        int output_bitrate_kbps{800};
        int output_width{1280};
        int output_height{720};
        double output_framerate{30.0};
        bool abr_enabled{false};
        int abr_min_bitrate{200};
        int abr_max_bitrate{2000};
        int abr_step_percent{20};
        double abr_loss_threshold_down{5.0};
        double abr_loss_threshold_up{1.0};
        int abr_stable_seconds{10};
        int abr_interval_seconds{3};
        bool abr_force_cycle{false};
        std::string output_username;
        std::string output_password;
        /// auto | force_hw | force_sw — resolved against host inventory at start
        std::string hw_policy{"auto"};
        /// Optional explicit override; when set, maps to force_hw / force_sw
        std::optional<bool> use_hw_codec;
    };

    void parse_codec_config(const nlohmann::json& config);
    void start_bridge(const TransportBinding& binding);
    void teardown_bridge();
    void publish_bridge_event(RtspCompressorEventType type, const std::string& detail);
    void refresh_telemetry_locked();

#ifdef ODC_HAS_RTSP_COMPRESSOR
    void compressor_thread_main(const TransportBinding binding);
    static gboolean on_poll_bridge(gpointer user_data);
#endif

    EventBus& bus_;
    ::orch::ThreadOrchestrator& orchestrator_;
    std::shared_ptr<CompressionCapabilities> compression_caps_;
    CodecConfig codec_cfg_;
    std::string applied_strategy_;
    bool applied_use_hw_{false};

    mutable std::mutex mutex_;
    std::optional<TransportBinding> pending_binding_;
    std::optional<TransportBinding> active_binding_;
    std::thread glib_thread_;
    GMainLoop* main_loop_{nullptr};
    std::atomic<bool> bridge_running_{false};
    bool service_enabled_{false};

#ifdef ODC_HAS_RTSP_COMPRESSOR
    RtspBridge* bridge_ptr_{nullptr};
    guint poll_source_id_{0};
#endif
    // Last telemetry snapshot for status_json / metrics port
    double telemetry_bitrate_kbps_{0};
    double telemetry_fps_{0};
    double telemetry_ratio_{0};
    int telemetry_clients_{0};
    double telemetry_loss_pct_{0};
    bool telemetry_abr_enabled_{false};
    int telemetry_abr_bitrate_{0};
    bool telemetry_reconnecting_{false};
};

} // namespace graph
} // namespace odc
