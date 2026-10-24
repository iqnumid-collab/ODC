#pragma once

#include "HardwareInfo.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <thread_orchestrator/thread_orchestrator.hpp>

#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <deque>
#include <unordered_map>
#include <chrono>

// ---------------------------------------------------------------------------
// Compression statistics (lock-free accumulation on hot path)
// ---------------------------------------------------------------------------
struct CompressionStats {
    std::atomic<uint64_t> total_encoded_bytes{0};
    std::atomic<uint64_t> frame_count{0};
    std::atomic<uint64_t> keyframe_count{0};
    std::atomic<uint64_t> keyframe_bytes{0};
    std::atomic<uint64_t> delta_frame_bytes{0};
    std::atomic<uint64_t> last_frame_size{0};
    std::chrono::steady_clock::time_point start_time;

    void reset() {
        total_encoded_bytes = 0; frame_count = 0;
        keyframe_count = 0; keyframe_bytes = 0;
        delta_frame_bytes = 0; last_frame_size = 0;
        start_time = std::chrono::steady_clock::now();
    }
};

// ---------------------------------------------------------------------------
// Per-client RTCP feedback
// ---------------------------------------------------------------------------
struct ClientFeedback {
    uint32_t ssrc = 0;
    float    fraction_lost = 0.0f;   // 0.0–1.0
    int32_t  cumulative_lost = 0;
    uint32_t jitter = 0;             // RTP timestamp units
    std::chrono::steady_clock::time_point last_update;
};

struct BridgeConfig {
    // Input RTSP stream
    std::string input_uri;
    bool input_use_tcp = false;

    // Output RTSP server
    std::string output_address = "0.0.0.0";
    int output_port = 8554;
    std::string output_path = "/stream";

    // Re-encoding settings (empty = passthrough / copy)
    std::string output_codec;       // e.g. "h264", "h265", "copy"
    int output_bitrate = 0;         // kbps, 0 = keep original
    int output_width = 0;           // 0 = keep original
    int output_height = 0;          // 0 = keep original
    double output_framerate = 0.0;  // 0 = keep original

    // Performance tuning
    int latency = 200;
    bool drop_on_latency = true;
    bool enable_queues = true;
    int queue_max_buffers = 3;
    int queue_max_time_ms = 0;
    std::string queue_leaky;
    std::string speed_preset = "ultrafast";
    int encoder_threads = 0;
    bool sliced_threads = false;
    int processing_threads = 0;
    int key_int_max = 0;
    std::string passthrough_codec = "h264";

    // HW codec request from user config
    bool use_hw_codec = false;

    // Adaptive bitrate control
    bool abr_enabled = false;
    int  abr_min_bitrate = 200;       // kbps
    int  abr_max_bitrate = 0;         // kbps, 0 = use output_bitrate
    int  abr_step_percent = 20;
    double abr_loss_threshold_down = 5.0;  // % packet loss to step down
    double abr_loss_threshold_up   = 1.0;  // % below which step-up allowed
    int  abr_stable_seconds = 10;
    int  abr_interval_seconds = 3;
    // Lab/demo: cycle one step-down then step-up without needing RTCP loss
    bool abr_force_cycle = false;

    // Output RTSP authentication (empty = disabled)
    std::string output_username;
    std::string output_password;

    // Resolved pipeline strategy (set by validateBridgeConfig)
    PipelineStrategy strategy = PipelineStrategy::Software;

    // GLib main context for gst_rtsp_server_attach (nullptr = thread default)
    GMainContext* main_context = nullptr;
};

// ---------------------------------------------------------------------------
// Telemetry snapshots (returned by public getters)
// ---------------------------------------------------------------------------
struct CompressionTelemetrySnapshot {
    double   bitrate_kbps       = 0.0;
    double   compression_ratio  = 0.0;
    double   fps                = 0.0;
    uint64_t keyframe_count     = 0;
    double   avg_keyframe_kb    = 0.0;
    uint64_t delta_frame_count  = 0;
    double   avg_delta_kb       = 0.0;
    uint32_t pace_queue_length  = 0;
    uint32_t connected_clients  = 0;
    double   worst_client_loss_pct = 0.0;
    int32_t  abr_bitrate_kbps  = 0;
    double   uptime_seconds     = 0.0;
    uint64_t total_encoded_bytes = 0;
    uint64_t ingest_frame_count = 0;
    bool     abr_enabled        = false;
    bool     reconnecting       = false;
};

struct SourceAvailabilitySnapshot {
    bool        reachable    = false;
    std::string codec;
    int32_t     width        = 0;
    int32_t     height       = 0;
    double      framerate    = 0.0;
    int32_t     bitrate_bps  = 0;
    std::string error_message;
};

/// Parse a JSON config file into a BridgeConfig struct.
bool parseBridgeConfig(const std::string& json_path, BridgeConfig& config);

/// Serialize a BridgeConfig struct to a JSON file.
bool serializeBridgeConfig(const std::string& json_path, const BridgeConfig& config);

/// Validate config against detected hardware. Adjusts strategy and use_hw_codec.
void validateBridgeConfig(BridgeConfig& config, const HardwareInfo& hw);

// Bridge event types
enum class BridgeEventType : int {
    AbrStepDown = 1, AbrStepUp = 2,
    SourceLost = 3, SourceRecovered = 4,
    ClientConnected = 5, ClientDisconnected = 6,
    AuthChanged = 7,
};

struct BridgeEvent {
    BridgeEventType type;
    std::string     detail;
};

class RtspBridge {
public:
    explicit RtspBridge(orch::ThreadOrchestrator& orchestrator);
    ~RtspBridge();

    RtspBridge(const RtspBridge&) = delete;
    RtspBridge& operator=(const RtspBridge&) = delete;

    bool start(const BridgeConfig& config);
    void stop();
    bool isRunning() const;

    // --- Public API for external consumers ---
    CompressionTelemetrySnapshot getCompressionTelemetry() const;
    SourceAvailabilitySnapshot   getSourceAvailability() const;
    void applyAbrOverride(bool enable, int bitrate_kbps);
    bool triggerReconnect();
    void updateAuth(const std::string& username, const std::string& password);
    std::vector<BridgeEvent> drainEvents();
    void setEventListener(std::function<void(BridgeEventType, const std::string&)> fn) {
        std::lock_guard<std::mutex> lk(event_mutex_);
        event_listener_ = std::move(fn);
    }

private:
    orch::ThreadOrchestrator& orch_;
    std::atomic<bool> running_{false};
    GstRTSPServer*   server_          = nullptr;
    guint            server_source_id_ = 0;
    GstElement*      ingest_pipeline_ = nullptr;
    GstElement*      appsink_         = nullptr;
    GstRTSPMediaFactory* factory_    = nullptr;
    BridgeConfig     config_;

    // Client appsrc tracking (HW path) + software RTSP client count
    mutable std::mutex       appsrc_mutex_;
    std::vector<GstElement*> appsrcs_;
    std::atomic<uint32_t>    soft_client_count_{0};
    std::atomic<bool>        soft_source_lost_{false};

    // Cached first keyframe
    std::mutex               keyframe_mutex_;
    std::condition_variable  keyframe_cv_;
    GstBuffer*               cached_keyframe_ = nullptr;

    // Pacing — orchestrator-managed task
    mutable std::mutex            pace_mutex_;
    std::condition_variable       pace_cv_;
    std::deque<GstBuffer*>        pace_queue_;
    orch::OrchestratorFuture<void> pace_future_;

    // Reconnect — orchestrator-managed long-lived task
    orch::OrchestratorFuture<void> reconnect_future_;
    std::atomic<bool>             reconnecting_{false};
    std::atomic<bool>             ingest_error_{false};
    std::mutex                    reconnect_signal_mutex_;
    std::condition_variable       reconnect_signal_cv_;
    std::atomic<bool>             reconnect_needed_{false};

    uint64_t                      ingest_buf_count_ = 0;

    // Compression stats
    CompressionStats              comp_stats_;

    // RTCP client feedback
    mutable std::mutex                                   feedback_mutex_;
    std::unordered_map<uint32_t, ClientFeedback>     client_feedback_;

    // ABR state
    GstElement*                   encoder_         = nullptr;  // ref to encoder in ingest pipeline
    int                           current_bitrate_kbps_ = 0;
    int                           stable_timer_    = 0;
    bool                          abr_supported_   = true;
    std::atomic<bool>             abr_enabled_{false};
    int                           abr_force_phase_ = 0; // 0=idle,1=waiting up,2=done

    // Stats/ABR reporter — orchestrator task
    orch::OrchestratorFuture<void> stats_future_;

    // Event queue
    mutable std::mutex            event_mutex_;
    std::vector<BridgeEvent>      pending_events_;
    std::function<void(BridgeEventType, const std::string&)> event_listener_;
    void emitEvent(BridgeEventType type, const std::string& detail);

    std::string codec_lower() const;
    std::string buildQueueElement() const;
    std::string buildIngestString() const;
    std::string buildLaunchString() const;
    bool startIngestPipeline();

    void paceLoop(orch::CancellationToken token);
    void reconnectLoop(orch::CancellationToken token);
    void restartIngestPipeline(orch::CancellationToken token);
    void statsLoop(orch::CancellationToken token);
    void printCompressionStats();
    void evaluateABR();
    void applyBitrate(int new_kbps);
    void setupAuth();

    static gboolean onIngestBusMessage(GstBus* bus, GstMessage* msg, gpointer user_data);
    static void onMediaConfigure(GstRTSPMediaFactory* factory, GstRTSPMedia* media,
                                 gpointer user_data);
    static void onReceivingRtcp(GObject* session, GstBuffer* buf, gpointer user_data);
    static void onClientConnected(GstRTSPServer* server, GstRTSPClient* client,
                                  gpointer user_data);
};
