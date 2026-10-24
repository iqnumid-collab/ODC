#include "graph/components/rtsp_compressor_component.hpp"

#include "core/event.hpp"
#include "core/event_bus.hpp"
#include "rtsp/compression_capabilities.hpp"
#include "rtsp/rtsp_events.hpp"

#include <glib.h>

#ifdef ODC_HAS_RTSP_COMPRESSOR
#include "RtspBridge.h"
#endif

namespace odc::graph {
namespace {

#ifdef ODC_HAS_RTSP_COMPRESSOR
RtspCompressorEventType map_bridge_event(BridgeEventType t) {
    switch (t) {
        case BridgeEventType::AbrStepDown: return RtspCompressorEventType::AbrStepDown;
        case BridgeEventType::AbrStepUp: return RtspCompressorEventType::AbrStepUp;
        case BridgeEventType::SourceLost: return RtspCompressorEventType::SourceLost;
        case BridgeEventType::SourceRecovered: return RtspCompressorEventType::SourceRecovered;
        case BridgeEventType::ClientConnected: return RtspCompressorEventType::ClientConnected;
        case BridgeEventType::ClientDisconnected: return RtspCompressorEventType::ClientDisconnected;
        case BridgeEventType::AuthChanged: return RtspCompressorEventType::AuthChanged;
    }
    return RtspCompressorEventType::Started;
}
#endif

} // namespace

RtspCompressorComponent::RtspCompressorComponent(
    EventBus& bus,
    orch::ThreadOrchestrator& orchestrator,
    std::shared_ptr<CompressionCapabilities> compression_caps,
    std::string id,
    nlohmann::json config)
    : OdcComponent(std::move(id), "rtsp.compressor", std::move(config)),
      bus_(bus),
      orchestrator_(orchestrator),
      compression_caps_(std::move(compression_caps)) {
    SetInputCount_(1, {"transport"});
    SetOutputCount_(2, {"status", "metrics"});
    parse_codec_config(config_);
}

RtspCompressorComponent::~RtspCompressorComponent() {
    stop();
}

void RtspCompressorComponent::configure(const nlohmann::json& config) {
    OdcComponent::configure(config);
    parse_codec_config(config_);
}

void RtspCompressorComponent::arm() {
    if (!enabled_) {
        return;
    }
    set_state(ComponentRuntimeState::Armed);
}

void RtspCompressorComponent::start() {
    if (!enabled_) {
        return;
    }

    service_enabled_ = true;
    set_state(ComponentRuntimeState::Starting);

    TransportBinding binding;
    bool have_binding = false;
    {
        std::lock_guard lock(mutex_);
        if (pending_binding_ && pending_binding_->ip_ready) {
            binding = *pending_binding_;
            have_binding = true;
        }
    }

    if (!have_binding) {
        binding.connection_id = id_;
        binding.interface_name = "lo";
        binding.target_ip = "127.0.0.1";
        binding.ip_ready = true;
    }

    start_bridge(binding);
}

void RtspCompressorComponent::stop() {
    if (state_ == ComponentRuntimeState::Stopped) {
        return;
    }

    set_state(ComponentRuntimeState::Stopping);
    service_enabled_ = false;
    teardown_bridge();

    {
        std::lock_guard lock(mutex_);
        pending_binding_.reset();
        active_binding_.reset();
    }

    set_state(ComponentRuntimeState::Stopped);
}

void RtspCompressorComponent::apply_binding(const TransportBinding& binding) {
    if (!enabled_) {
        return;
    }

    {
        std::lock_guard lock(mutex_);
        pending_binding_ = binding;
    }

    if (!binding.ip_ready || !service_enabled_) {
        return;
    }

    start_bridge(binding);
}

void RtspCompressorComponent::publish_bridge_event(RtspCompressorEventType type,
                                                   const std::string& detail) {
    RtspCompressorEvent ev;
    ev.connection_id = id_;
    ev.type = type;
    ev.detail = detail;
    // Sync publish so IPC subscribers see the event before the producer continues.
    // Async submit_simple can race with short-lived lab drains.
    bus_.publish_sync(Event{std::move(ev)});
}

void RtspCompressorComponent::refresh_telemetry_locked() {
#ifdef ODC_HAS_RTSP_COMPRESSOR
    if (!bridge_ptr_) return;
    auto snap = bridge_ptr_->getCompressionTelemetry();
    telemetry_bitrate_kbps_ = snap.bitrate_kbps;
    telemetry_fps_ = snap.fps;
    telemetry_ratio_ = snap.compression_ratio;
    telemetry_clients_ = static_cast<int>(snap.connected_clients);
    telemetry_loss_pct_ = snap.worst_client_loss_pct;
    telemetry_abr_enabled_ = snap.abr_enabled;
    telemetry_abr_bitrate_ = snap.abr_bitrate_kbps;
    telemetry_reconnecting_ = snap.reconnecting;
#endif
}

nlohmann::json RtspCompressorComponent::status_json() const {
    std::lock_guard lock(mutex_);
#ifdef ODC_HAS_RTSP_COMPRESSOR
    if (bridge_ptr_) {
        const_cast<RtspCompressorComponent*>(this)->refresh_telemetry_locked();
    }
#endif

    nlohmann::json status{
        {"id", id_},
        {"type", type_},
        {"enabled", enabled_},
        {"state", std::string(to_string_view(state_))},
        {"running", bridge_running_.load()},
        {"input_host", codec_cfg_.input_host},
        {"input_port", codec_cfg_.input_port},
        {"input_path", codec_cfg_.input_path},
        {"output_port", codec_cfg_.output_port},
        {"output_path", codec_cfg_.output_path},
        {"output_codec", codec_cfg_.output_codec},
        {"output_bitrate_kbps", codec_cfg_.output_bitrate_kbps},
        {"hw_policy", codec_cfg_.hw_policy},
        {"use_hw_codec", applied_use_hw_},
        {"pipeline_strategy", applied_strategy_},
        {"abr_enabled", codec_cfg_.abr_enabled},
        {"bitrate_kbps", telemetry_bitrate_kbps_},
        {"fps", telemetry_fps_},
        {"compression_ratio", telemetry_ratio_},
        {"connected_clients", telemetry_clients_},
        {"worst_client_loss_pct", telemetry_loss_pct_},
        {"abr_bitrate_kbps", telemetry_abr_bitrate_},
        {"reconnecting", telemetry_reconnecting_},
    };

    if (active_binding_) {
        status["target_ip"] = active_binding_->target_ip;
        status["interface_name"] = active_binding_->interface_name;
    } else if (pending_binding_) {
        status["target_ip"] = pending_binding_->target_ip;
        status["interface_name"] = pending_binding_->interface_name;
        status["ip_ready"] = pending_binding_->ip_ready;
    }

    return status;
}

void RtspCompressorComponent::parse_codec_config(const nlohmann::json& config) {
    codec_cfg_.input_host = config.value("input_host", codec_cfg_.input_host);
    codec_cfg_.input_port = config.value("input_port", codec_cfg_.input_port);
    codec_cfg_.input_path = config.value("input_path", codec_cfg_.input_path);
    codec_cfg_.output_port = config.value("output_port", codec_cfg_.output_port);
    codec_cfg_.output_path = config.value("output_path", codec_cfg_.output_path);
    codec_cfg_.input_use_tcp = config.value("input_use_tcp", codec_cfg_.input_use_tcp);
    codec_cfg_.output_codec = config.value("output_codec", codec_cfg_.output_codec);
    codec_cfg_.output_bitrate_kbps = config.value("output_bitrate_kbps", codec_cfg_.output_bitrate_kbps);
    codec_cfg_.output_width = config.value("output_width", codec_cfg_.output_width);
    codec_cfg_.output_height = config.value("output_height", codec_cfg_.output_height);
    codec_cfg_.output_framerate = config.value("output_framerate", codec_cfg_.output_framerate);
    codec_cfg_.abr_enabled = config.value("abr_enabled", codec_cfg_.abr_enabled);
    codec_cfg_.abr_min_bitrate = config.value("abr_min_bitrate", codec_cfg_.abr_min_bitrate);
    codec_cfg_.abr_max_bitrate = config.value("abr_max_bitrate", codec_cfg_.abr_max_bitrate);
    codec_cfg_.abr_step_percent = config.value("abr_step_percent", codec_cfg_.abr_step_percent);
    codec_cfg_.abr_loss_threshold_down = config.value("abr_loss_threshold_down", codec_cfg_.abr_loss_threshold_down);
    codec_cfg_.abr_loss_threshold_up = config.value("abr_loss_threshold_up", codec_cfg_.abr_loss_threshold_up);
    codec_cfg_.abr_stable_seconds = config.value("abr_stable_seconds", codec_cfg_.abr_stable_seconds);
    codec_cfg_.abr_interval_seconds = config.value("abr_interval_seconds", codec_cfg_.abr_interval_seconds);
    codec_cfg_.abr_force_cycle = config.value("abr_force_cycle", codec_cfg_.abr_force_cycle);
    codec_cfg_.output_username = config.value("output_username", codec_cfg_.output_username);
    codec_cfg_.output_password = config.value("output_password", codec_cfg_.output_password);
    codec_cfg_.hw_policy = config.value("hw_policy", codec_cfg_.hw_policy);
    if (config.contains("use_hw_codec") && !config["use_hw_codec"].is_null()) {
        codec_cfg_.use_hw_codec = config["use_hw_codec"].get<bool>();
    }
}

void RtspCompressorComponent::start_bridge(const TransportBinding& binding) {
    if (!binding.ip_ready && binding.target_ip.empty()) {
        return;
    }

    {
        std::lock_guard lock(mutex_);
        if (bridge_running_.load() &&
            active_binding_ &&
            active_binding_->target_ip == binding.target_ip &&
            active_binding_->interface_name == binding.interface_name) {
            return;
        }
        pending_binding_ = binding;
    }

    teardown_bridge();

#ifdef ODC_HAS_RTSP_COMPRESSOR
    {
        std::lock_guard lock(mutex_);
        active_binding_ = binding;
    }

    glib_thread_ = std::thread([this, binding]() {
        compressor_thread_main(binding);
    });
#else
    RtspCompressorStatusEvent status;
    status.connection_id = id_;
    status.interface_name = binding.interface_name;
    status.target_ip = binding.target_ip;
    status.error_message = "RTSP compressor support not built";
    bus_.publish(Event{std::move(status)});
#endif
}

void RtspCompressorComponent::teardown_bridge() {
    GMainLoop* loop_ptr = nullptr;
    bool joinable = false;

    {
        std::lock_guard lock(mutex_);
        loop_ptr = main_loop_;
        joinable = glib_thread_.joinable();
    }

    if (loop_ptr) {
        g_main_loop_quit(loop_ptr);
    }

    if (joinable) {
        std::thread worker;
        {
            std::lock_guard lock(mutex_);
            if (glib_thread_.joinable()) {
                worker = std::move(glib_thread_);
            }
        }
        if (worker.joinable()) {
            worker.join();
        }
    }

    {
        std::lock_guard lock(mutex_);
        main_loop_ = nullptr;
        bridge_running_.store(false);
#ifdef ODC_HAS_RTSP_COMPRESSOR
        bridge_ptr_ = nullptr;
        poll_source_id_ = 0;
#endif
    }
}

#ifdef ODC_HAS_RTSP_COMPRESSOR
gboolean RtspCompressorComponent::on_poll_bridge(gpointer user_data) {
    auto* self = static_cast<RtspCompressorComponent*>(user_data);
    if (!self->bridge_ptr_ || !self->bridge_running_.load()) {
        return G_SOURCE_CONTINUE;
    }

    for (const auto& be : self->bridge_ptr_->drainEvents()) {
        self->publish_bridge_event(map_bridge_event(be.type), be.detail);
    }

    {
        std::lock_guard lock(self->mutex_);
        self->refresh_telemetry_locked();
    }

    RtspCompressorStatusEvent status;
    status.connection_id = self->id_;
    {
        std::lock_guard lock(self->mutex_);
        if (self->active_binding_) {
            status.interface_name = self->active_binding_->interface_name;
            status.target_ip = self->active_binding_->target_ip;
        }
        status.running = true;
        status.reconnecting = self->telemetry_reconnecting_;
        status.abr_enabled = self->telemetry_abr_enabled_;
        status.abr_bitrate_kbps = self->telemetry_abr_bitrate_;
        status.bitrate_kbps = self->telemetry_bitrate_kbps_;
        status.fps = self->telemetry_fps_;
        status.compression_ratio = self->telemetry_ratio_;
        status.connected_clients = self->telemetry_clients_;
        status.worst_client_loss_pct = self->telemetry_loss_pct_;
    }
    self->bus_.publish(Event{std::move(status)});
    return G_SOURCE_CONTINUE;
}

void RtspCompressorComponent::compressor_thread_main(const TransportBinding binding) {
    auto loop = g_main_loop_new(nullptr, FALSE);
    GMainContext* context = g_main_loop_get_context(loop);
    g_main_context_push_thread_default(context);
    {
        std::lock_guard lock(mutex_);
        main_loop_ = loop;
    }

    auto bridge = std::make_unique<RtspBridge>(orchestrator_);
    bridge->setEventListener([this](BridgeEventType type, const std::string& detail) {
        publish_bridge_event(map_bridge_event(type), detail);
    });

    BridgeConfig cfg;
    cfg.input_uri = "rtsp://" + codec_cfg_.input_host + ":"
        + std::to_string(codec_cfg_.input_port) + codec_cfg_.input_path;
    cfg.input_use_tcp = codec_cfg_.input_use_tcp;
    cfg.output_address = "0.0.0.0";
    cfg.output_port = codec_cfg_.output_port;
    cfg.output_path = codec_cfg_.output_path;
    cfg.output_codec = codec_cfg_.output_codec;
    cfg.output_bitrate = codec_cfg_.output_bitrate_kbps;
    cfg.output_width = codec_cfg_.output_width;
    cfg.output_height = codec_cfg_.output_height;
    cfg.output_framerate = codec_cfg_.output_framerate;
    cfg.abr_enabled = codec_cfg_.abr_enabled;
    cfg.abr_min_bitrate = codec_cfg_.abr_min_bitrate;
    cfg.abr_max_bitrate = codec_cfg_.abr_max_bitrate > 0
        ? codec_cfg_.abr_max_bitrate
        : codec_cfg_.output_bitrate_kbps;
    cfg.abr_step_percent = codec_cfg_.abr_step_percent;
    cfg.abr_loss_threshold_down = codec_cfg_.abr_loss_threshold_down;
    cfg.abr_loss_threshold_up = codec_cfg_.abr_loss_threshold_up;
    cfg.abr_stable_seconds = codec_cfg_.abr_stable_seconds;
    cfg.abr_interval_seconds = codec_cfg_.abr_interval_seconds;
    cfg.abr_force_cycle = codec_cfg_.abr_force_cycle;
    cfg.output_username = codec_cfg_.output_username;
    cfg.output_password = codec_cfg_.output_password;
    cfg.main_context = context;

    auto policy = CompressionCapabilities::parse_hw_policy(codec_cfg_.hw_policy);
    if (codec_cfg_.use_hw_codec.has_value()) {
        policy = *codec_cfg_.use_hw_codec
            ? CompressionCapabilities::HwPolicy::ForceHw
            : CompressionCapabilities::HwPolicy::ForceSw;
    }

    if (compression_caps_) {
        compression_caps_->ensure(false);
        std::string rule_error;
        if (!compression_caps_->apply_rules(cfg, policy, &rule_error)) {
            set_state(ComponentRuntimeState::Error);
            RtspCompressorStatusEvent status;
            status.connection_id = id_;
            status.interface_name = binding.interface_name;
            status.target_ip = binding.target_ip;
            status.error_message = rule_error.empty()
                ? "compression capability rules rejected codec"
                : rule_error;
            bus_.publish(Event{std::move(status)});
            g_main_loop_unref(loop);
            {
                std::lock_guard lock(mutex_);
                main_loop_ = nullptr;
            }
            g_main_context_pop_thread_default(context);
            return;
        }
    } else {
        cfg.use_hw_codec = (policy == CompressionCapabilities::HwPolicy::ForceHw);
    }

    {
        std::lock_guard lock(mutex_);
        applied_use_hw_ = cfg.use_hw_codec;
        applied_strategy_ = strategyName(cfg.strategy);
    }

    bool started = bridge->start(cfg);
    if (started) {
        bridge_running_.store(true);
        set_state(ComponentRuntimeState::Running);
        {
            std::lock_guard lock(mutex_);
            bridge_ptr_ = bridge.get();
        }

        publish_bridge_event(RtspCompressorEventType::Started, "bridge started");

        RtspCompressorStatusEvent status;
        status.connection_id = id_;
        status.interface_name = binding.interface_name;
        status.target_ip = binding.target_ip;
        status.running = true;
        status.abr_enabled = codec_cfg_.abr_enabled;
        bus_.publish(Event{std::move(status)});

        // Must attach to this thread's GMainContext — g_timeout_add() uses the
        // process default context and would never fire on the compressor loop.
        GSource* poll_src = g_timeout_source_new(1000);
        g_source_set_callback(poll_src, on_poll_bridge, this, nullptr);
        poll_source_id_ = g_source_attach(poll_src, context);
        g_source_unref(poll_src);
    } else {
        set_state(ComponentRuntimeState::Error);
        RtspCompressorStatusEvent status;
        status.connection_id = id_;
        status.interface_name = binding.interface_name;
        status.target_ip = binding.target_ip;
        status.error_message = "Failed to start RTSP bridge";
        bus_.publish(Event{std::move(status)});
        g_main_loop_unref(loop);
        {
            std::lock_guard lock(mutex_);
            main_loop_ = nullptr;
        }
        g_main_context_pop_thread_default(context);
        return;
    }

    g_main_loop_run(loop);

    if (poll_source_id_ != 0) {
        g_source_remove(poll_source_id_);
        poll_source_id_ = 0;
    }

    {
        std::lock_guard lock(mutex_);
        bridge_ptr_ = nullptr;
    }

    bridge->stop();
    bridge.reset();
    bridge_running_.store(false);

    publish_bridge_event(RtspCompressorEventType::Stopped, "bridge stopped");

    g_main_loop_unref(loop);
    {
        std::lock_guard lock(mutex_);
        main_loop_ = nullptr;
    }
    g_main_context_pop_thread_default(context);

    RtspCompressorStatusEvent status;
    status.connection_id = id_;
    status.interface_name = binding.interface_name;
    status.target_ip = binding.target_ip;
    status.running = false;
    bus_.publish(Event{std::move(status)});
}
#endif

void RtspCompressorComponent::Process_(DSPatch::SignalBus& inputs, DSPatch::SignalBus& outputs) {
    if (auto* binding = inputs.GetValue<TransportBinding>(kInputTransport)) {
        apply_binding(*binding);
    }

    std::lock_guard lock(mutex_);
    outputs.SetValue(kOutputStatus, nlohmann::json{
        {"id", id_},
        {"running", bridge_running_.load()},
        {"output_port", codec_cfg_.output_port},
        {"connected_clients", telemetry_clients_},
    });
    outputs.SetValue(kOutputMetrics, nlohmann::json{
        {"bitrate_kbps", telemetry_bitrate_kbps_},
        {"fps", telemetry_fps_},
        {"compression_ratio", telemetry_ratio_},
        {"connected_clients", telemetry_clients_},
        {"worst_client_loss_pct", telemetry_loss_pct_},
        {"abr_enabled", telemetry_abr_enabled_},
        {"abr_bitrate_kbps", telemetry_abr_bitrate_},
        {"reconnecting", telemetry_reconnecting_},
    });
}

} // namespace odc::graph
