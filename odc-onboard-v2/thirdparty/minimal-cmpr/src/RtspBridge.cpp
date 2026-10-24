#include "RtspBridge.h"
#include <nlohmann/json.hpp>

#include <gst/rtp/gstrtcpbuffer.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iomanip>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Config Parser
// ---------------------------------------------------------------------------

bool parseBridgeConfig(const std::string& json_path, BridgeConfig& config) {
    std::ifstream file(json_path);
    if (!file.is_open()) {
        std::cerr << "[BridgeConfig] Cannot open config file: " << json_path << "\n";
        return false;
    }

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "[BridgeConfig] JSON parse error: " << e.what() << "\n";
        return false;
    }

    if (j.contains("input_uri"))     j.at("input_uri").get_to(config.input_uri);
    if (j.contains("input_use_tcp")) j.at("input_use_tcp").get_to(config.input_use_tcp);
    if (j.contains("output_address"))   j.at("output_address").get_to(config.output_address);
    if (j.contains("output_port"))      j.at("output_port").get_to(config.output_port);
    if (j.contains("output_path"))      j.at("output_path").get_to(config.output_path);
    if (j.contains("output_codec"))     j.at("output_codec").get_to(config.output_codec);
    if (j.contains("output_bitrate"))   j.at("output_bitrate").get_to(config.output_bitrate);
    if (j.contains("output_width"))     j.at("output_width").get_to(config.output_width);
    if (j.contains("output_height"))    j.at("output_height").get_to(config.output_height);
    if (j.contains("output_framerate")) j.at("output_framerate").get_to(config.output_framerate);
    if (j.contains("latency"))            j.at("latency").get_to(config.latency);
    if (j.contains("drop_on_latency"))    j.at("drop_on_latency").get_to(config.drop_on_latency);
    if (j.contains("enable_queues"))      j.at("enable_queues").get_to(config.enable_queues);
    if (j.contains("queue_max_buffers"))  j.at("queue_max_buffers").get_to(config.queue_max_buffers);
    if (j.contains("queue_max_time_ms"))  j.at("queue_max_time_ms").get_to(config.queue_max_time_ms);
    if (j.contains("queue_leaky"))        j.at("queue_leaky").get_to(config.queue_leaky);
    if (j.contains("speed_preset"))       j.at("speed_preset").get_to(config.speed_preset);
    if (j.contains("encoder_threads"))    j.at("encoder_threads").get_to(config.encoder_threads);
    if (j.contains("sliced_threads"))     j.at("sliced_threads").get_to(config.sliced_threads);
    if (j.contains("processing_threads")) j.at("processing_threads").get_to(config.processing_threads);
    if (j.contains("key_int_max"))        j.at("key_int_max").get_to(config.key_int_max);
    if (j.contains("passthrough_codec"))  j.at("passthrough_codec").get_to(config.passthrough_codec);
    if (j.contains("use_hw_codec"))       j.at("use_hw_codec").get_to(config.use_hw_codec);

    // ABR
    if (j.contains("abr_enabled"))            j.at("abr_enabled").get_to(config.abr_enabled);
    if (j.contains("abr_min_bitrate"))        j.at("abr_min_bitrate").get_to(config.abr_min_bitrate);
    if (j.contains("abr_max_bitrate"))        j.at("abr_max_bitrate").get_to(config.abr_max_bitrate);
    if (j.contains("abr_step_percent"))       j.at("abr_step_percent").get_to(config.abr_step_percent);
    if (j.contains("abr_loss_threshold_down")) j.at("abr_loss_threshold_down").get_to(config.abr_loss_threshold_down);
    if (j.contains("abr_loss_threshold_up"))   j.at("abr_loss_threshold_up").get_to(config.abr_loss_threshold_up);
    if (j.contains("abr_stable_seconds"))     j.at("abr_stable_seconds").get_to(config.abr_stable_seconds);
    if (j.contains("abr_interval_seconds"))   j.at("abr_interval_seconds").get_to(config.abr_interval_seconds);
    if (j.contains("abr_force_cycle"))        j.at("abr_force_cycle").get_to(config.abr_force_cycle);

    // Auth
    if (j.contains("output_username")) j.at("output_username").get_to(config.output_username);
    if (j.contains("output_password")) j.at("output_password").get_to(config.output_password);

    if (config.input_uri.empty()) {
        std::cerr << "[BridgeConfig] 'input_uri' is required in config.\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Config Validation against HardwareInfo
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Config Serializer
// ---------------------------------------------------------------------------

bool serializeBridgeConfig(const std::string& json_path, const BridgeConfig& c) {
    json j;
    j["input_uri"]          = c.input_uri;
    j["input_use_tcp"]      = c.input_use_tcp;
    j["output_address"]     = c.output_address;
    j["output_port"]        = c.output_port;
    j["output_path"]        = c.output_path;
    j["output_codec"]       = c.output_codec;
    j["output_bitrate"]     = c.output_bitrate;
    j["output_width"]       = c.output_width;
    j["output_height"]      = c.output_height;
    j["output_framerate"]   = c.output_framerate;
    j["latency"]            = c.latency;
    j["drop_on_latency"]    = c.drop_on_latency;
    j["enable_queues"]      = c.enable_queues;
    j["queue_max_buffers"]  = c.queue_max_buffers;
    j["queue_max_time_ms"]  = c.queue_max_time_ms;
    j["queue_leaky"]        = c.queue_leaky;
    j["speed_preset"]       = c.speed_preset;
    j["encoder_threads"]    = c.encoder_threads;
    j["sliced_threads"]     = c.sliced_threads;
    j["processing_threads"] = c.processing_threads;
    j["key_int_max"]        = c.key_int_max;
    j["passthrough_codec"]  = c.passthrough_codec;
    j["use_hw_codec"]       = c.use_hw_codec;
    j["abr_enabled"]            = c.abr_enabled;
    j["abr_min_bitrate"]        = c.abr_min_bitrate;
    j["abr_max_bitrate"]        = c.abr_max_bitrate;
    j["abr_step_percent"]       = c.abr_step_percent;
    j["abr_loss_threshold_down"] = c.abr_loss_threshold_down;
    j["abr_loss_threshold_up"]   = c.abr_loss_threshold_up;
    j["abr_stable_seconds"]     = c.abr_stable_seconds;
    j["abr_interval_seconds"]   = c.abr_interval_seconds;
    j["output_username"]        = c.output_username;
    j["output_password"]        = c.output_password;

    std::ofstream file(json_path);
    if (!file.is_open()) {
        std::cerr << "[BridgeConfig] Cannot write config file: " << json_path << "\n";
        return false;
    }
    file << j.dump(4) << "\n";
    return file.good();
}

// ---------------------------------------------------------------------------
// Config Validation against HardwareInfo
// ---------------------------------------------------------------------------

void validateBridgeConfig(BridgeConfig& config, const HardwareInfo& hw) {
    std::string codec = config.output_codec;
    std::transform(codec.begin(), codec.end(), codec.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    bool passthrough = codec.empty() || codec == "copy";

    if (passthrough) {
        config.strategy = PipelineStrategy::Software; // irrelevant for passthrough
        return;
    }

    const bool wants_h264 = (codec == "h264" || codec == "avc");
    const bool wants_h265 = (codec == "h265" || codec == "hevc");
    const bool codec_has_hw = wants_h264 ? hw.has_h264_hw_encoder
                            : wants_h265 ? hw.has_h265_hw_encoder
                            : hardwareHasGpuEncoder(hw);
    const bool codec_has_sw = wants_h264
        ? (hw.has_x264enc || hw.has_openh264enc || hw.has_v4l2h264enc)
        : wants_h265 ? hw.has_x265enc
        : true;

    if (config.use_hw_codec) {
        if (codec_has_hw && hw.has_jetson_hw_encoder) {
            config.strategy = PipelineStrategy::JetsonHW;
        } else if (codec_has_hw && hw.has_nvenc_encoder) {
            config.strategy = PipelineStrategy::DesktopNVENC;
        } else if (codec_has_hw && hw.has_vaapi_encoder) {
            config.strategy = PipelineStrategy::VAAPI;
        } else {
            std::cerr << "[BridgeConfig] WARNING: use_hw_codec=true but no HW encoder "
                      << "for codec '" << config.output_codec << "'. "
                      << "Falling back to software encoding.\n";
            config.use_hw_codec = false;
            config.strategy = PipelineStrategy::Software;
        }
    } else {
        config.strategy = PipelineStrategy::Software;
        if (codec_has_hw) {
            std::cout << "[BridgeConfig] INFO: HW acceleration available ("
                      << strategyName(hw.strategy) << ") but use_hw_codec=false in config.\n";
        }
    }

    if (!config.use_hw_codec && !codec_has_sw) {
        std::cerr << "[BridgeConfig] ERROR: codec '" << config.output_codec
                  << "' has neither HW nor SW encoder plugins available.\n";
    }
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

RtspBridge::RtspBridge(orch::ThreadOrchestrator& orchestrator)
    : orch_(orchestrator) {
    gst_init(nullptr, nullptr);
}

RtspBridge::~RtspBridge() {
    stop();
}

bool RtspBridge::isRunning() const {
    return running_;
}

std::string RtspBridge::codec_lower() const {
    std::string c = config_.output_codec;
    std::transform(c.begin(), c.end(), c.begin(),
                   [](unsigned char ch){ return std::tolower(ch); });
    return c;
}

std::string RtspBridge::buildQueueElement() const {
    std::ostringstream q;
    q << "queue";
    if (config_.queue_max_buffers > 0)
        q << " max-size-buffers=" << config_.queue_max_buffers;
    if (config_.queue_max_time_ms > 0)
        q << " max-size-time=" << (static_cast<uint64_t>(config_.queue_max_time_ms) * 1000000ULL);
    if (!config_.queue_leaky.empty() && config_.queue_leaky != "no")
        q << " leaky=" << config_.queue_leaky;
    return q.str();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool RtspBridge::start(const BridgeConfig& config) {
    if (running_) {
        std::cerr << "[RtspBridge] Already running. Call stop() first.\n";
        return false;
    }

    config_ = config;
    const std::string codec = codec_lower();
    const bool passthrough  = codec.empty() || codec == "copy";
    const bool hw_transcode = config_.use_hw_codec && !passthrough;

    if (hw_transcode) {
        running_ = true;
        // Submit pace and reconnect as orchestrator-managed tasks
        pace_future_ = orch_.submit([this](orch::CancellationToken token) {
            paceLoop(token);
        });
        reconnect_future_ = orch_.submit([this](orch::CancellationToken token) {
            reconnectLoop(token);
        });

        // Signal the reconnect task to perform initial connection
        {
            std::lock_guard<std::mutex> lk(reconnect_signal_mutex_);
            reconnect_needed_ = true;
        }
        reconnect_signal_cv_.notify_one();

        // Wait for first keyframe
        std::cout << "[RtspBridge] Waiting for first keyframe from ingest...\n";
        std::cout.flush();
        std::unique_lock<std::mutex> lk(keyframe_mutex_);
        keyframe_cv_.wait(lk, [this]{ return cached_keyframe_ != nullptr || !running_; });
        if (!running_) {
            std::cerr << "[RtspBridge] Aborted while waiting for first keyframe.\n";
            return false;
        }
        std::cout << "[RtspBridge] First keyframe received, starting RTSP server.\n";
        std::cout.flush();
    }

    // Create RTSP server
    server_ = gst_rtsp_server_new();
    if (!server_) {
        std::cerr << "[RtspBridge] Failed to create RTSP server.\n";
        return false;
    }
    gst_rtsp_server_set_address(server_, config_.output_address.c_str());
    gchar port_str[16];
    g_snprintf(port_str, sizeof(port_str), "%d", config_.output_port);
    gst_rtsp_server_set_service(server_, port_str);

    // Build and mount factory
    std::string launch = buildLaunchString();
    std::cout << "[RtspBridge] Factory launch string:\n  " << launch << "\n";
    std::cout.flush();

    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, launch.c_str());
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    gst_rtsp_media_factory_set_suspend_mode(factory, GST_RTSP_SUSPEND_MODE_NONE);
    g_signal_connect(factory, "media-configure",
                     G_CALLBACK(RtspBridge::onMediaConfigure), this);

    factory_ = factory;

    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(server_);
    gst_rtsp_mount_points_add_factory(mounts, config_.output_path.c_str(), factory);
    g_object_unref(mounts);

    setupAuth();

    GMainContext* context = config_.main_context
        ? config_.main_context
        : g_main_context_get_thread_default();
    if ((server_source_id_ = gst_rtsp_server_attach(server_, context)) == 0) {
        std::cerr << "[RtspBridge] Failed to attach RTSP server.\n";
        g_object_unref(server_);
        server_ = nullptr;
        return false;
    }

    // Software encode path has no appsrc — track RTSP clients on the server.
    g_signal_connect(server_, "client-connected",
                     G_CALLBACK(RtspBridge::onClientConnected), this);

    running_ = true;

    // Initialize ABR state
    current_bitrate_kbps_ = config_.output_bitrate;
    if (config_.abr_max_bitrate <= 0)
        config_.abr_max_bitrate = config_.output_bitrate;
    // x265enc doesn't support runtime bitrate changes
    abr_supported_ = (config_.strategy != PipelineStrategy::Software)
                   || (codec_lower() != "h265" && codec_lower() != "hevc");
    abr_enabled_ = config_.abr_enabled;
    if (abr_enabled_ && !abr_supported_)
        std::cerr << "[ABR] WARNING: x265enc does not support runtime bitrate changes. "
                  << "ABR disabled. Use VAAPI/NVENC or H.264 for ABR.\n";

    // Submit stats/ABR reporter task
    stats_future_ = orch_.submit([this](orch::CancellationToken token) {
        statsLoop(token);
    });

    int ncores = static_cast<int>(std::thread::hardware_concurrency());
    if (ncores < 1) ncores = 4;
    int enc_t  = (config_.encoder_threads   > 0) ? config_.encoder_threads   : ncores;
    int proc_t = (config_.processing_threads > 0) ? config_.processing_threads : ncores;

    std::cout << "[RtspBridge] RTSP server started.\n"
              << "  Input    : " << config_.input_uri << "\n"
              << "  Output   : rtsp://" << config_.output_address
              << ":" << config_.output_port << config_.output_path << "\n"
              << "  Cores    : " << ncores << " (encoder=" << enc_t
              << ", processing=" << proc_t << ")\n"
              << "  Strategy : " << strategyName(config_.strategy) << "\n";
    std::cout.flush();
    return true;
}

static void teardownPipeline(GstElement*& pipeline, GstElement*& sink) {
    if (!pipeline) return;
    gst_element_set_state(pipeline, GST_STATE_READY);
    gst_element_get_state(pipeline, nullptr, nullptr, 3 * GST_SECOND);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_element_get_state(pipeline, nullptr, nullptr, 3 * GST_SECOND);
    gst_object_unref(pipeline);
    pipeline = nullptr;
    sink     = nullptr;
}

void RtspBridge::stop() {
    if (!running_) return;
    running_ = false;

    // Wake all blocked threads
    keyframe_cv_.notify_all();
    pace_cv_.notify_all();
    reconnect_signal_cv_.notify_all();

    // Cancel orchestrator tasks
    pace_future_.cancel();
    reconnect_future_.cancel();
    stats_future_.cancel();

    // Wait for tasks to finish
    try { pace_future_.get(); } catch (...) {}
    try { reconnect_future_.get(); } catch (...) {}
    try { stats_future_.get(); } catch (...) {}

    // Drain pace queue
    {
        std::lock_guard<std::mutex> lk(pace_mutex_);
        for (GstBuffer* b : pace_queue_) gst_buffer_unref(b);
        pace_queue_.clear();
    }

    teardownPipeline(ingest_pipeline_, appsink_);
    if (encoder_) { gst_object_unref(encoder_); encoder_ = nullptr; }
    {
        std::lock_guard<std::mutex> lk(keyframe_mutex_);
        if (cached_keyframe_) {
            gst_buffer_unref(cached_keyframe_);
            cached_keyframe_ = nullptr;
        }
    }
    if (server_) {
        if (server_source_id_ > 0) {
            g_source_remove(server_source_id_);
            server_source_id_ = 0;
        }
        g_object_unref(server_);
        server_ = nullptr;
    }
    factory_ = nullptr;  // owned by mount points, released with server
    std::cout << "[RtspBridge] Bridge stopped.\n";
}

// ---------------------------------------------------------------------------
// RTSP Authentication
// ---------------------------------------------------------------------------

void RtspBridge::setupAuth() {
    if (!server_ || !factory_) return;

    if (config_.output_username.empty() || config_.output_password.empty()) {
        gst_rtsp_server_set_auth(server_, nullptr);
        std::cout << "[RtspBridge] RTSP auth disabled (no credentials configured).\n";
        return;
    }

    GstRTSPAuth* auth = gst_rtsp_auth_new();

    // Default token (anonymous) — no permissions → 401 for unauthenticated
    GstRTSPToken* anon = gst_rtsp_token_new(
        GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, "anonymous", nullptr);
    gst_rtsp_auth_set_default_token(auth, anon);
    gst_rtsp_token_unref(anon);

    // Authenticated user token (Basic auth)
    GstRTSPToken* user_token = gst_rtsp_token_new(
        GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, "user", nullptr);
    gchar* basic = gst_rtsp_auth_make_basic(
        config_.output_username.c_str(), config_.output_password.c_str());
    gst_rtsp_auth_add_basic(auth, basic, user_token);
    g_free(basic);
    gst_rtsp_token_unref(user_token);

    // Grant "user" role full access; "anonymous" can see but not play
    gst_rtsp_media_factory_add_role(factory_, "user",
        GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
        GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, nullptr);
    gst_rtsp_media_factory_add_role(factory_, "anonymous",
        GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
        GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, FALSE, nullptr);

    gst_rtsp_server_set_auth(server_, auth);
    g_object_unref(auth);

    std::cout << "[RtspBridge] RTSP auth enabled for user: "
              << config_.output_username << "\n";
}

void RtspBridge::updateAuth(const std::string& username, const std::string& password) {
    if (!server_) return;
    config_.output_username = username;
    config_.output_password = password;
    setupAuth();
    emitEvent(BridgeEventType::AuthChanged,
              username.empty() ? "disabled" : "user=" + username);
}

void RtspBridge::emitEvent(BridgeEventType type, const std::string& detail) {
    std::function<void(BridgeEventType, const std::string&)> listener;
    {
        std::lock_guard<std::mutex> lk(event_mutex_);
        pending_events_.push_back({type, detail});
        listener = event_listener_;
    }
    if (listener) {
        listener(type, detail);
    }
}

void RtspBridge::onClientConnected(GstRTSPServer* /*server*/, GstRTSPClient* client,
                                   gpointer user_data) {
    auto* self = static_cast<RtspBridge*>(user_data);
    if (!self || !client) return;

    uint32_t n = self->soft_client_count_.fetch_add(1) + 1;
    std::cout << "[RtspBridge] client-connected (soft_clients=" << n << ")\n";
    std::cout.flush();
    self->emitEvent(BridgeEventType::ClientConnected, "clients=" + std::to_string(n));

    g_signal_connect(client, "closed",
        G_CALLBACK(+[](GstRTSPClient* /*c*/, gpointer ud) {
            auto* s = static_cast<RtspBridge*>(ud);
            if (!s) return;
            uint32_t left = s->soft_client_count_.load();
            if (left > 0) left = s->soft_client_count_.fetch_sub(1) - 1;
            std::cout << "[RtspBridge] client-closed (soft_clients=" << left << ")\n";
            std::cout.flush();
            s->emitEvent(BridgeEventType::ClientDisconnected,
                         "clients=" + std::to_string(left));
        }), self);
}

std::vector<BridgeEvent> RtspBridge::drainEvents() {
    std::lock_guard<std::mutex> lk(event_mutex_);
    std::vector<BridgeEvent> out;
    out.swap(pending_events_);
    return out;
}

// ---------------------------------------------------------------------------
// Pace loop — orchestrator task
// ---------------------------------------------------------------------------

void RtspBridge::paceLoop(orch::CancellationToken token) {
    double fps = config_.output_framerate;
    if (fps <= 0.0) fps = 30.0;
    const auto frame_interval = std::chrono::microseconds(
        static_cast<int64_t>(1000000.0 / fps));
    auto next_push = std::chrono::steady_clock::now();
    uint64_t pushed = 0;

    while (!token.is_cancelled() && running_) {
        GstBuffer* buf = nullptr;
        {
            std::unique_lock<std::mutex> lk(pace_mutex_);
            pace_cv_.wait_for(lk, frame_interval,
                [&]{ return !pace_queue_.empty() || token.is_cancelled() || !running_; });
            if (token.is_cancelled() || !running_) break;
            if (pace_queue_.empty()) continue;
            buf = pace_queue_.front();
            pace_queue_.pop_front();
        }

        auto now = std::chrono::steady_clock::now();
        if (now < next_push)
            std::this_thread::sleep_until(next_push);
        next_push = std::chrono::steady_clock::now() + frame_interval;

        {
            std::lock_guard<std::mutex> lk(appsrc_mutex_);
            for (GstElement* src : appsrcs_) {
                GstBuffer* copy = gst_buffer_copy(buf);
                GST_BUFFER_PTS(copy) = GST_CLOCK_TIME_NONE;
                GST_BUFFER_DTS(copy) = GST_CLOCK_TIME_NONE;
                GstFlowReturn r;
                g_signal_emit_by_name(src, "push-buffer", copy, &r);
                gst_buffer_unref(copy);
            }
        }
        gst_buffer_unref(buf);
        ++pushed;
        if (pushed <= 5 || (pushed % 1000 == 0)) {
            std::cout << "[Pace] pushed #" << pushed << "\n";
            std::cout.flush();
        }
    }
}

// ---------------------------------------------------------------------------
// Reconnect loop — orchestrator task, sleeps until signaled
// ---------------------------------------------------------------------------

void RtspBridge::reconnectLoop(orch::CancellationToken token) {
    while (!token.is_cancelled() && running_) {
        // Wait for a reconnect signal
        {
            std::unique_lock<std::mutex> lk(reconnect_signal_mutex_);
            reconnect_signal_cv_.wait(lk, [&]{
                return reconnect_needed_.load() || token.is_cancelled() || !running_;
            });
            if (token.is_cancelled() || !running_) break;
            reconnect_needed_ = false;
        }
        restartIngestPipeline(token);
    }
}

void RtspBridge::restartIngestPipeline(orch::CancellationToken token) {
    reconnecting_ = true;

    teardownPipeline(ingest_pipeline_, appsink_);

    {
        std::lock_guard<std::mutex> lk(keyframe_mutex_);
        if (cached_keyframe_) {
            gst_buffer_unref(cached_keyframe_);
            cached_keyframe_ = nullptr;
        }
    }
    {
        std::lock_guard<std::mutex> lk(pace_mutex_);
        for (GstBuffer* b : pace_queue_) gst_buffer_unref(b);
        pace_queue_.clear();
    }

    int consecutive_parse_failures = 0;
    while (!token.is_cancelled() && running_) {
        std::cout << "[RtspBridge] Reconnecting to ingest source...\n";
        std::cout.flush();
        ingest_error_.store(false);
        emitEvent(BridgeEventType::SourceLost, "reconnecting");

        if (startIngestPipeline()) {
            consecutive_parse_failures = 0;
            std::unique_lock<std::mutex> lk(keyframe_mutex_);
            keyframe_cv_.wait_for(lk, std::chrono::seconds(5),
                [&]{ return cached_keyframe_ != nullptr
                          || ingest_error_.load()
                          || token.is_cancelled()
                          || !running_; });

            if (token.is_cancelled() || !running_) break;

            if (cached_keyframe_ != nullptr) {
                std::cout << "[RtspBridge] Reconnect successful.\n";
                std::cout.flush();
                emitEvent(BridgeEventType::SourceRecovered, "");
                break;
            }
            std::cerr << "[RtspBridge] Reconnect: no keyframe within 5 s, retrying...\n";
        }

        teardownPipeline(ingest_pipeline_, appsink_);

        {
            std::lock_guard<std::mutex> lk(keyframe_mutex_);
            if (cached_keyframe_) {
                gst_buffer_unref(cached_keyframe_);
                cached_keyframe_ = nullptr;
            }
        }
        {
            std::lock_guard<std::mutex> lk(pace_mutex_);
            for (GstBuffer* b : pace_queue_) gst_buffer_unref(b);
            pace_queue_.clear();
        }

        // Fail fast if pipeline can't even be parsed (missing element)
        ++consecutive_parse_failures;
        if (consecutive_parse_failures >= 3) {
            std::cerr << "[RtspBridge] Pipeline parse failed " << consecutive_parse_failures
                      << " times — element likely missing. Giving up.\n";
            running_ = false;
            keyframe_cv_.notify_all();
            break;
        }

        // Brief pause
        {
            std::unique_lock<std::mutex> lk(reconnect_signal_mutex_);
            reconnect_signal_cv_.wait_for(lk, std::chrono::milliseconds(500),
                [&]{ return token.is_cancelled() || !running_; });
        }
    }

    reconnecting_ = false;
}

// ---------------------------------------------------------------------------
// Ingest pipeline (HW two-pipeline path: Jetson + NVENC)
// ---------------------------------------------------------------------------

std::string RtspBridge::buildIngestString() const {
    const std::string codec = codec_lower();
    std::ostringstream ss;

    ss << "rtspsrc location=" << config_.input_uri
       << " latency=" << config_.latency
       << " tcp-timeout=2000000";
    if (config_.drop_on_latency) ss << " drop-on-latency=true";
    if (config_.input_use_tcp)   ss << " protocols=tcp";

    ss << " ! decodebin"
       << " ! queue max-size-buffers=4 leaky=downstream";

    const bool has_scale = (config_.output_width > 0 && config_.output_height > 0);
    const bool has_fps   = (config_.output_framerate > 0.0);

    if (config_.strategy == PipelineStrategy::JetsonHW) {
        // Jetson path: nvvidconv + nvv4l2 encoders
        ss << " ! nvvidconv";
        if (has_scale || has_fps) {
            ss << " ! video/x-raw(memory:NVMM)";
            if (has_scale) ss << ",width=" << config_.output_width << ",height=" << config_.output_height;
            if (has_fps)   ss << ",framerate=" << static_cast<int>(config_.output_framerate) << "/1";
        }
        ss << " ! queue max-size-buffers=4 leaky=downstream";
        if (codec == "h265" || codec == "hevc") {
            ss << " ! nvv4l2h265enc name=enc";
            if (config_.output_bitrate > 0) ss << " bitrate=" << (config_.output_bitrate * 1000);
            if (config_.key_int_max > 0)    ss << " iframeinterval=" << config_.key_int_max;
            ss << " ! h265parse";
        } else {
            ss << " ! nvv4l2h264enc name=enc";
            if (config_.output_bitrate > 0) ss << " bitrate=" << (config_.output_bitrate * 1000);
            if (config_.key_int_max > 0)    ss << " iframeinterval=" << config_.key_int_max;
            ss << " ! h264parse";
        }
    } else if (config_.strategy == PipelineStrategy::VAAPI) {
        // VA-API path: videoconvert + vaapih264enc/vaapih265enc
        ss << " ! videoconvert";
        if (has_scale) {
            ss << " ! videoscale"
               << " ! video/x-raw,width=" << config_.output_width
               << ",height=" << config_.output_height;
        }
        if (has_fps) {
            ss << " ! videorate"
               << " ! video/x-raw,framerate=" << static_cast<int>(config_.output_framerate) << "/1";
        }
        ss << " ! queue max-size-buffers=4 leaky=downstream";
        if (codec == "h265" || codec == "hevc") {
            ss << " ! vaapih265enc name=enc";
            if (config_.output_bitrate > 0) ss << " bitrate=" << config_.output_bitrate;
            if (config_.key_int_max > 0)    ss << " keyframe-period=" << config_.key_int_max;
            ss << " ! h265parse";
        } else {
            ss << " ! vaapih264enc name=enc";
            if (config_.output_bitrate > 0) ss << " bitrate=" << config_.output_bitrate;
            if (config_.key_int_max > 0)    ss << " keyframe-period=" << config_.key_int_max;
            ss << " ! h264parse";
        }
    } else {
        // Desktop NVENC path: videoconvert + nvh264enc/nvh265enc
        ss << " ! videoconvert";
        if (has_scale) {
            ss << " ! videoscale"
               << " ! video/x-raw,width=" << config_.output_width
               << ",height=" << config_.output_height;
        }
        if (has_fps) {
            ss << " ! videorate"
               << " ! video/x-raw,framerate=" << static_cast<int>(config_.output_framerate) << "/1";
        }
        ss << " ! queue max-size-buffers=4 leaky=downstream";
        if (codec == "h265" || codec == "hevc") {
            ss << " ! nvh265enc name=enc";
            if (config_.output_bitrate > 0) ss << " bitrate=" << config_.output_bitrate;
            if (config_.key_int_max > 0)    ss << " gop-size=" << config_.key_int_max;
            ss << " ! h265parse";
        } else {
            ss << " ! nvh264enc name=enc";
            if (config_.output_bitrate > 0) ss << " bitrate=" << config_.output_bitrate;
            if (config_.key_int_max > 0)    ss << " gop-size=" << config_.key_int_max;
            ss << " ! h264parse";
        }
    }

    ss << " ! queue max-size-buffers=4 leaky=downstream"
       << " ! appsink name=ingest_sink sync=false max-buffers=4 drop=true";
    return ss.str();
}

bool RtspBridge::startIngestPipeline() {
    std::string desc = buildIngestString();
    std::cout << "[RtspBridge] Ingest pipeline:\n  " << desc << "\n";
    std::cout.flush();

    GError* err = nullptr;
    ingest_pipeline_ = gst_parse_launch(desc.c_str(), &err);
    if (!ingest_pipeline_ || err) {
        std::cerr << "[RtspBridge] Failed to parse ingest pipeline: "
                  << (err ? err->message : "unknown") << "\n";
        if (err) g_error_free(err);
        return false;
    }

    appsink_ = gst_bin_get_by_name(GST_BIN(ingest_pipeline_), "ingest_sink");
    if (!appsink_) {
        std::cerr << "[RtspBridge] appsink not found in ingest pipeline.\n";
        gst_element_set_state(ingest_pipeline_, GST_STATE_NULL);
        gst_element_get_state(ingest_pipeline_, nullptr, nullptr, 3 * GST_SECOND);
        gst_object_unref(ingest_pipeline_); ingest_pipeline_ = nullptr;
        return false;
    }

    ingest_buf_count_ = 0;
    comp_stats_.reset();

    // Grab encoder element reference for ABR
    const std::string codec = codec_lower();
    if (config_.strategy == PipelineStrategy::JetsonHW ||
        config_.strategy == PipelineStrategy::VAAPI ||
        config_.strategy == PipelineStrategy::DesktopNVENC) {
        encoder_ = gst_bin_get_by_name(GST_BIN(ingest_pipeline_), "enc");
    }

    GstAppSinkCallbacks sink_cbs{};
    sink_cbs.eos = nullptr;
    sink_cbs.new_preroll = nullptr;
    sink_cbs.new_sample = +[](GstAppSink* sink, gpointer ud) -> GstFlowReturn {
        RtspBridge* self = static_cast<RtspBridge*>(ud);
        GstSample* sample = gst_app_sink_pull_sample(sink);
        if (!sample) return GST_FLOW_OK;
        GstBuffer* buf = gst_sample_get_buffer(sample);

        {
            std::lock_guard<std::mutex> lk(self->keyframe_mutex_);
            if (!self->cached_keyframe_) {
                self->cached_keyframe_ = gst_buffer_copy(buf);
                self->keyframe_cv_.notify_all();
                std::cout << "[Ingest] cached keyframe size="
                          << gst_buffer_get_size(buf) << "\n";
                std::cout.flush();
            }
        }

        {
            std::lock_guard<std::mutex> lk(self->pace_mutex_);
            double fps = self->config_.output_framerate;
            if (fps <= 0.0) fps = 30.0;
            size_t max_q = static_cast<size_t>(fps * 2);
            if (self->pace_queue_.size() < max_q) {
                self->pace_queue_.push_back(gst_buffer_copy(buf));
                self->pace_cv_.notify_one();
            }
        }

        // Accumulate compression stats
        uint64_t bsize = gst_buffer_get_size(buf);
        bool is_kf = !GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT);
        self->comp_stats_.total_encoded_bytes.fetch_add(bsize, std::memory_order_relaxed);
        self->comp_stats_.frame_count.fetch_add(1, std::memory_order_relaxed);
        self->comp_stats_.last_frame_size.store(bsize, std::memory_order_relaxed);
        if (is_kf) {
            self->comp_stats_.keyframe_count.fetch_add(1, std::memory_order_relaxed);
            self->comp_stats_.keyframe_bytes.fetch_add(bsize, std::memory_order_relaxed);
        } else {
            self->comp_stats_.delta_frame_bytes.fetch_add(bsize, std::memory_order_relaxed);
        }

        uint64_t count = ++self->ingest_buf_count_;
        if (count <= 5 || (count % 1000 == 0)) {
            std::cout << "[Ingest] buf #" << count
                      << " size=" << gst_buffer_get_size(buf)
                      << " qlen=" << self->pace_queue_.size() << "\n";
            std::cout.flush();
        }
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    };
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink_), &sink_cbs, this, nullptr);

    GstBus* bus = gst_element_get_bus(ingest_pipeline_);
    gst_bus_set_sync_handler(bus,
        +[](GstBus* b, GstMessage* msg, gpointer ud) -> GstBusSyncReply {
            RtspBridge::onIngestBusMessage(b, msg, ud);
            return GST_BUS_DROP;
        }, this, nullptr);
    gst_object_unref(bus);

    GstStateChangeReturn ret = gst_element_set_state(ingest_pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "[RtspBridge] Ingest pipeline failed to start.\n";
        gst_element_set_state(ingest_pipeline_, GST_STATE_NULL);
        gst_element_get_state(ingest_pipeline_, nullptr, nullptr, 3 * GST_SECOND);
        gst_object_unref(ingest_pipeline_); ingest_pipeline_ = nullptr;
        appsink_ = nullptr;
        return false;
    }
    std::cout << "[RtspBridge] Ingest pipeline PLAYING ("
              << (ret == GST_STATE_CHANGE_ASYNC ? "ASYNC" : "OK") << ")\n";
    std::cout.flush();
    return true;
}

gboolean RtspBridge::onIngestBusMessage(GstBus* /*bus*/, GstMessage* msg,
                                         gpointer user_data) {
    RtspBridge* self = static_cast<RtspBridge*>(user_data);
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr; gchar* dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            std::cerr << "[RtspBridge] Ingest ERROR: " << err->message << "\n";
            if (dbg) { std::cerr << "  " << dbg << "\n"; g_free(dbg); }
            g_error_free(err);
            if (self && self->running_) {
                self->ingest_error_.store(true);
                self->keyframe_cv_.notify_all();
                // Signal the reconnect task instead of spawning a thread
                if (!self->reconnecting_) {
                    std::lock_guard<std::mutex> lk(self->reconnect_signal_mutex_);
                    self->reconnect_needed_ = true;
                    self->reconnect_signal_cv_.notify_one();
                }
            }
            break;
        }
        case GST_MESSAGE_EOS:
            std::cerr << "[RtspBridge] Ingest EOS.\n";
            if (self && self->running_) {
                self->ingest_error_.store(true);
                self->keyframe_cv_.notify_all();
                if (!self->reconnecting_) {
                    std::lock_guard<std::mutex> lk(self->reconnect_signal_mutex_);
                    self->reconnect_needed_ = true;
                    self->reconnect_signal_cv_.notify_one();
                }
            }
            break;
        default: break;
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// Media configure callback
// ---------------------------------------------------------------------------

void RtspBridge::onMediaConfigure(GstRTSPMediaFactory* /*factory*/, GstRTSPMedia* media,
                                   gpointer user_data) {
    RtspBridge* self = static_cast<RtspBridge*>(user_data);

    // In software single-pipeline mode there is no appsrc — but we still
    // want RTCP feedback and source-lost/recovered events.
    if (!self->config_.use_hw_codec) {
        // Defer RTCP hook to "prepared" (same as HW path below)
        g_signal_connect(media, "prepared",
            G_CALLBACK(+[](GstRTSPMedia* m, gpointer ud) {
                auto* s = static_cast<RtspBridge*>(ud);
                guint n = gst_rtsp_media_n_streams(m);
                for (guint i = 0; i < n; i++) {
                    GstRTSPStream* st = gst_rtsp_media_get_stream(m, i);
                    if (!st) continue;
                    GObject* rtp = gst_rtsp_stream_get_rtpsession(st);
                    if (rtp) {
                        g_signal_connect(rtp, "on-receiving-rtcp",
                            G_CALLBACK(RtspBridge::onReceivingRtcp), s);
                        g_object_unref(rtp);
                    }
                }
                if (s->soft_source_lost_.exchange(false)) {
                    s->emitEvent(BridgeEventType::SourceRecovered, "media prepared");
                }
            }), self);
        g_signal_connect(media, "unprepared",
            G_CALLBACK(+[](GstRTSPMedia* /*m*/, gpointer ud) {
                auto* s = static_cast<RtspBridge*>(ud);
                s->soft_source_lost_.store(true);
                s->emitEvent(BridgeEventType::SourceLost, "media unprepared");
            }), self);
        return;
    }

    GstElement* bin    = gst_rtsp_media_get_element(media);
    GstElement* appsrc = gst_bin_get_by_name(GST_BIN(bin), "ingest_src");
    gst_object_unref(bin);

    if (!appsrc)
        return;

    {
        std::lock_guard<std::mutex> lk(self->keyframe_mutex_);
        if (self->cached_keyframe_) {
            GstBuffer* copy = gst_buffer_copy(self->cached_keyframe_);
            GST_BUFFER_PTS(copy) = GST_CLOCK_TIME_NONE;
            GST_BUFFER_DTS(copy) = GST_CLOCK_TIME_NONE;
            GstFlowReturn r;
            g_signal_emit_by_name(appsrc, "push-buffer", copy, &r);
            gst_buffer_unref(copy);
            std::cout << "[RtspBridge] onMediaConfigure: pushed cached keyframe\n";
            std::cout.flush();
        }
    }

    {
        std::lock_guard<std::mutex> lk(self->appsrc_mutex_);
        self->appsrcs_.push_back(appsrc);
    }
    std::cout << "[RtspBridge] media-configure: appsrc registered (total: "
              << self->appsrcs_.size() << ")\n";
    std::cout.flush();
    self->emitEvent(BridgeEventType::ClientConnected,
                    "clients=" + std::to_string(self->appsrcs_.size()));

    // Hook RTCP receiver reports for ABR — deferred to "prepared" because
    // the RTP session object is not available at media-configure time.
    g_signal_connect(media, "prepared",
        G_CALLBACK(+[](GstRTSPMedia* m, gpointer user_data) {
            auto* self = static_cast<RtspBridge*>(user_data);
            guint n = gst_rtsp_media_n_streams(m);
            for (guint i = 0; i < n; i++) {
                GstRTSPStream* stream = gst_rtsp_media_get_stream(m, i);
                if (!stream) continue;
                GObject* rtp = gst_rtsp_stream_get_rtpsession(stream);
                if (rtp) {
                    g_signal_connect(rtp, "on-receiving-rtcp",
                        G_CALLBACK(RtspBridge::onReceivingRtcp), self);
                    g_object_unref(rtp);
                    std::cout << "[RtspBridge] RTCP hook installed on stream " << i << "\n";
                    std::cout.flush();
                }
            }
        }), self);

    g_object_set_data(G_OBJECT(appsrc), "bridge", self);
    g_signal_connect(media, "unprepared",
        G_CALLBACK(+[](GstRTSPMedia* /*m*/, gpointer src_ptr) {
            RtspBridge* bridge = static_cast<RtspBridge*>(
                g_object_get_data(G_OBJECT(static_cast<GstElement*>(src_ptr)), "bridge"));
            if (bridge) {
                std::lock_guard<std::mutex> lk(bridge->appsrc_mutex_);
                auto& v = bridge->appsrcs_;
                v.erase(std::remove(v.begin(), v.end(),
                                    static_cast<GstElement*>(src_ptr)), v.end());
                bridge->emitEvent(BridgeEventType::ClientDisconnected,
                                  "clients=" + std::to_string(v.size()));
            }
            gst_object_unref(static_cast<GstElement*>(src_ptr));
        }), appsrc);
}

// ---------------------------------------------------------------------------
// Factory launch string builder
// ---------------------------------------------------------------------------

std::string RtspBridge::buildLaunchString() const {
    std::string codec = codec_lower();
    bool passthrough = codec.empty() || codec == "copy";

    std::string pt_codec = config_.passthrough_codec;
    std::transform(pt_codec.begin(), pt_codec.end(), pt_codec.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    std::ostringstream ss;

    // HW two-pipeline path (Jetson or NVENC): appsrc -> parse -> pay
    if (config_.use_hw_codec && !passthrough) {
        if (codec == "h265" || codec == "hevc") {
            ss << "( appsrc name=ingest_src is-live=true format=time do-timestamp=true"
               << " caps=video/x-h265"
               << " ! h265parse"
               << " ! queue max-size-buffers=4 leaky=downstream"
               << " ! rtph265pay name=pay0 pt=96 config-interval=-1 )";
        } else {
            ss << "( appsrc name=ingest_src is-live=true format=time do-timestamp=true"
               << " caps=video/x-h264"
               << " ! h264parse"
               << " ! queue max-size-buffers=4 leaky=downstream"
               << " ! rtph264pay name=pay0 pt=96 config-interval=-1 )";
        }
        return ss.str();
    }

    // Software single-pipeline path
    int ncores = static_cast<int>(std::thread::hardware_concurrency());
    if (ncores < 1) ncores = 4;
    int enc_threads  = (config_.encoder_threads  > 0) ? config_.encoder_threads  : ncores;
    int proc_threads = (config_.processing_threads > 0) ? config_.processing_threads : ncores;

    ss << "( rtspsrc location=" << config_.input_uri
       << " latency=" << config_.latency;
    if (config_.drop_on_latency) ss << " drop-on-latency=true";
    if (config_.input_use_tcp)   ss << " protocols=tcp";
    ss << " ! ";

    if (passthrough) {
        if (config_.enable_queues) ss << buildQueueElement() << " ! ";
        if (pt_codec == "h265" || pt_codec == "hevc")
            ss << "rtph265depay ! rtph265pay name=pay0 pt=96";
        else
            ss << "rtph264depay ! rtph264pay name=pay0 pt=96";
    } else {
        ss << "decodebin";
        if (config_.enable_queues) ss << " ! " << buildQueueElement();
        ss << " ! videoconvert n-threads=" << proc_threads;

        if (config_.output_width > 0 && config_.output_height > 0)
            ss << " ! videoscale n-threads=" << proc_threads
               << " ! video/x-raw,width=" << config_.output_width
               << ",height=" << config_.output_height;

        if (config_.output_framerate > 0.0)
            ss << " ! videorate ! video/x-raw,framerate="
               << static_cast<int>(config_.output_framerate) << "/1";

        if (config_.enable_queues) ss << " ! " << buildQueueElement();

        if (codec == "h264") {
            ss << " ! x264enc tune=zerolatency speed-preset=" << config_.speed_preset
               << " threads=" << enc_threads;
            if (config_.output_bitrate > 0) ss << " bitrate=" << config_.output_bitrate;
            if (config_.sliced_threads)     ss << " sliced-threads=true";
            if (config_.key_int_max > 0)    ss << " key-int-max=" << config_.key_int_max;
        } else if (codec == "h265" || codec == "hevc") {
            ss << " ! x265enc tune=zerolatency speed-preset=" << config_.speed_preset
               << " option-string=\"pools=" << enc_threads
               << ":frame-threads=" << enc_threads << "\"";
            if (config_.output_bitrate > 0) ss << " bitrate=" << config_.output_bitrate;
            if (config_.key_int_max > 0)    ss << " key-int-max=" << config_.key_int_max;
        } else {
            std::cerr << "[RtspBridge] Unknown codec '" << config_.output_codec
                      << "', falling back to H264.\n";
            ss << " ! x264enc tune=zerolatency speed-preset=" << config_.speed_preset
               << " threads=" << enc_threads;
            if (config_.output_bitrate > 0) ss << " bitrate=" << config_.output_bitrate;
        }

        if (config_.enable_queues) ss << " ! " << buildQueueElement();
        if (codec == "h265" || codec == "hevc")
            ss << " ! rtph265pay name=pay0 pt=96";
        else
            ss << " ! rtph264pay name=pay0 pt=96";
    }

    ss << " )";
    return ss.str();
}

// ---------------------------------------------------------------------------
// Stats / ABR reporter — orchestrator task
// ---------------------------------------------------------------------------

void RtspBridge::statsLoop(orch::CancellationToken token) {
    int abr_tick = 0;
    while (!token.is_cancelled() && running_) {
        for (int i = 0; i < 20 && !token.is_cancelled() && running_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (token.is_cancelled() || !running_) break;

        printCompressionStats();

        // ABR evaluation at configured interval
        if (abr_enabled_ && abr_supported_) {
            abr_tick += 2;
            if (abr_tick >= config_.abr_interval_seconds) {
                abr_tick = 0;
                evaluateABR();
            }
        }
    }
}

void RtspBridge::printCompressionStats() {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - comp_stats_.start_time).count();
    if (elapsed < 0.1) return;

    uint64_t total = comp_stats_.total_encoded_bytes.load(std::memory_order_relaxed);
    uint64_t frames = comp_stats_.frame_count.load(std::memory_order_relaxed);
    uint64_t kf_count = comp_stats_.keyframe_count.load(std::memory_order_relaxed);
    uint64_t kf_bytes = comp_stats_.keyframe_bytes.load(std::memory_order_relaxed);
    uint64_t df_bytes = comp_stats_.delta_frame_bytes.load(std::memory_order_relaxed);

    double bitrate_kbps = (total * 8.0 / 1000.0) / elapsed;
    double fps = frames / elapsed;

    // Compression ratio: raw YUV420 vs encoded
    double raw_bps = config_.output_width * config_.output_height * 1.5
                   * (config_.output_framerate > 0 ? config_.output_framerate : 30.0);
    double ratio = (total > 0) ? (raw_bps * elapsed) / total : 0.0;

    double avg_kf = kf_count > 0 ? (kf_bytes / 1024.0) / kf_count : 0.0;
    uint64_t df_count = frames > kf_count ? frames - kf_count : 0;
    double avg_df = df_count > 0 ? (df_bytes / 1024.0) / df_count : 0.0;

    size_t qlen;
    {
        std::lock_guard<std::mutex> lk(pace_mutex_);
        qlen = pace_queue_.size();
    }
    size_t clients;
    {
        std::lock_guard<std::mutex> lk(appsrc_mutex_);
        clients = appsrcs_.size();
    }
    if (clients == 0) clients = soft_client_count_.load();

    std::cout << "[Stats] bitrate=" << static_cast<int>(bitrate_kbps) << "kbps"
              << " ratio=" << std::fixed << std::setprecision(1) << ratio << ":1"
              << " fps=" << std::setprecision(1) << fps
              << " I=" << kf_count << "(avg " << std::setprecision(1) << avg_kf << "KB)"
              << " P=" << df_count << "(avg " << std::setprecision(1) << avg_df << "KB)"
              << " qlen=" << qlen
              << " clients=" << clients;

    // Print worst client loss if available
    {
        std::lock_guard<std::mutex> lk(feedback_mutex_);
        if (!client_feedback_.empty()) {
            float worst = 0.0f;
            for (auto& [ssrc, fb] : client_feedback_)
                if (fb.fraction_lost > worst) worst = fb.fraction_lost;
            std::cout << " loss=" << std::setprecision(1) << (worst * 100.0f) << "%";
        }
    }

    if (abr_enabled_)
        std::cout << " abr_br=" << current_bitrate_kbps_ << "kbps";

    std::cout << "\n";
    std::cout.flush();
}

// ---------------------------------------------------------------------------
// RTCP Receiver Report callback
// ---------------------------------------------------------------------------

void RtspBridge::onReceivingRtcp(GObject* /*session*/, GstBuffer* buf, gpointer ud) {
    RtspBridge* self = static_cast<RtspBridge*>(ud);
    GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
    if (!gst_rtcp_buffer_map(buf, GST_MAP_READ, &rtcp)) return;

    GstRTCPPacket packet;
    if (!gst_rtcp_buffer_get_first_packet(&rtcp, &packet)) {
        gst_rtcp_buffer_unmap(&rtcp);
        return;
    }

    auto now = std::chrono::steady_clock::now();
    do {
        if (gst_rtcp_packet_get_type(&packet) != GST_RTCP_TYPE_RR) continue;
        guint count = gst_rtcp_packet_get_rb_count(&packet);
        for (guint i = 0; i < count; i++) {
            guint32 ssrc;
            guint8 frac_lost;
            gint32 cum_lost;
            guint32 ext_high, jitter, lsr, dlsr;
            gst_rtcp_packet_get_rb(&packet, i, &ssrc,
                &frac_lost, &cum_lost, &ext_high, &jitter, &lsr, &dlsr);

            float loss_f = frac_lost / 256.0f;
            std::cout << "[RTCP] RR ssrc=" << ssrc
                      << " loss=" << std::fixed << std::setprecision(1) << (loss_f * 100.0f)
                      << "% jitter=" << jitter << "\n";
            std::cout.flush();

            std::lock_guard<std::mutex> lk(self->feedback_mutex_);
            auto& fb = self->client_feedback_[ssrc];
            fb.ssrc = ssrc;
            fb.fraction_lost = loss_f;
            fb.cumulative_lost = cum_lost;
            fb.jitter = jitter;
            fb.last_update = now;
        }
    } while (gst_rtcp_packet_move_to_next(&packet));

    gst_rtcp_buffer_unmap(&rtcp);
}

// ---------------------------------------------------------------------------
// ABR decision logic
// ---------------------------------------------------------------------------

void RtspBridge::evaluateABR() {
    if (current_bitrate_kbps_ <= 0) return;

    // Lab/demo path: emit one down then one up without needing encoder/RTCP loss.
    if (config_.abr_force_cycle && abr_force_phase_ < 2) {
        if (abr_force_phase_ == 0) {
            int new_br = std::max(config_.abr_min_bitrate,
                static_cast<int>(current_bitrate_kbps_ * (1.0 - config_.abr_step_percent / 100.0)));
            std::string detail = std::to_string(current_bitrate_kbps_) + " -> " + std::to_string(new_br) + " kbps (force)";
            std::cout << "[ABR] Step DOWN (force cycle): " << detail << "\n";
            current_bitrate_kbps_ = new_br;
            if (encoder_) applyBitrate(new_br);
            emitEvent(BridgeEventType::AbrStepDown, detail);
            abr_force_phase_ = 1;
            stable_timer_ = 0;
            return;
        }
        if (abr_force_phase_ == 1) {
            stable_timer_ += config_.abr_interval_seconds;
            if (stable_timer_ >= config_.abr_stable_seconds) {
                int new_br = std::min(config_.abr_max_bitrate > 0 ? config_.abr_max_bitrate : config_.output_bitrate,
                    static_cast<int>(current_bitrate_kbps_ * (1.0 + config_.abr_step_percent / 100.0)));
                std::string detail = std::to_string(current_bitrate_kbps_) + " -> " + std::to_string(new_br) + " kbps (force)";
                std::cout << "[ABR] Step UP (force cycle): " << detail << "\n";
                current_bitrate_kbps_ = new_br;
                if (encoder_) applyBitrate(new_br);
                emitEvent(BridgeEventType::AbrStepUp, detail);
                abr_force_phase_ = 2;
            }
            return;
        }
    }

    if (!encoder_) return;

    float worst_loss = 0.0f;
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(feedback_mutex_);
        // Prune stale entries and find worst loss
        for (auto it = client_feedback_.begin(); it != client_feedback_.end(); ) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_update).count();
            if (age > 30) { it = client_feedback_.erase(it); continue; }
            if (it->second.fraction_lost > worst_loss)
                worst_loss = it->second.fraction_lost;
            ++it;
        }
    }

    float loss_pct = worst_loss * 100.0f;

    if (loss_pct > config_.abr_loss_threshold_down) {
        // Step down
        int new_br = static_cast<int>(current_bitrate_kbps_ * (1.0 - config_.abr_step_percent / 100.0));
        new_br = std::max(new_br, config_.abr_min_bitrate);
        if (new_br != current_bitrate_kbps_) {
            std::cout << "[ABR] Step DOWN: " << current_bitrate_kbps_ << " -> " << new_br
                      << " kbps (loss=" << std::fixed << std::setprecision(1) << loss_pct << "%)\n";
            {
                std::string detail = std::to_string(current_bitrate_kbps_) + " -> " + std::to_string(new_br) + " kbps";
                applyBitrate(new_br);
                emitEvent(BridgeEventType::AbrStepDown, detail);
            }
        }
        stable_timer_ = 0;
    } else if (loss_pct < config_.abr_loss_threshold_up) {
        stable_timer_ += config_.abr_interval_seconds;
        if (stable_timer_ >= config_.abr_stable_seconds) {
            int new_br = static_cast<int>(current_bitrate_kbps_ * (1.0 + config_.abr_step_percent / 100.0));
            new_br = std::min(new_br, config_.abr_max_bitrate);
            if (new_br != current_bitrate_kbps_) {
                std::cout << "[ABR] Step UP: " << current_bitrate_kbps_ << " -> " << new_br
                          << " kbps (stable " << stable_timer_ << "s)\n";
                {
                    std::string detail = std::to_string(current_bitrate_kbps_) + " -> " + std::to_string(new_br) + " kbps";
                    applyBitrate(new_br);
                    emitEvent(BridgeEventType::AbrStepUp, detail);
                }
            }
            stable_timer_ = 0;
        }
    } else {
        stable_timer_ = 0;
    }
}

void RtspBridge::applyBitrate(int new_kbps) {
    if (!encoder_) return;

    // Jetson nvv4l2 encoders use bps, everything else uses kbps
    if (config_.strategy == PipelineStrategy::JetsonHW)
        g_object_set(encoder_, "bitrate", new_kbps * 1000, nullptr);
    else
        g_object_set(encoder_, "bitrate", new_kbps, nullptr);

    current_bitrate_kbps_ = new_kbps;

    // Force keyframe for fast convergence
    GstEvent* event = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM,
        gst_structure_new("GstForceKeyUnit",
            "all-headers", G_TYPE_BOOLEAN, TRUE, nullptr));
    gst_element_send_event(encoder_, event);
}

// ---------------------------------------------------------------------------
// Public API for external consumers
// ---------------------------------------------------------------------------

CompressionTelemetrySnapshot RtspBridge::getCompressionTelemetry() const {
    CompressionTelemetrySnapshot snap;
    if (!running_) return snap;

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - comp_stats_.start_time).count();
    if (elapsed < 0.1) return snap;

    uint64_t total   = comp_stats_.total_encoded_bytes.load(std::memory_order_relaxed);
    uint64_t frames  = comp_stats_.frame_count.load(std::memory_order_relaxed);
    uint64_t kf_cnt  = comp_stats_.keyframe_count.load(std::memory_order_relaxed);
    uint64_t kf_bytes = comp_stats_.keyframe_bytes.load(std::memory_order_relaxed);
    uint64_t df_bytes = comp_stats_.delta_frame_bytes.load(std::memory_order_relaxed);

    snap.bitrate_kbps = (total * 8.0 / 1000.0) / elapsed;
    snap.fps = frames / elapsed;

    double raw_bps = config_.output_width * config_.output_height * 1.5
                   * (config_.output_framerate > 0 ? config_.output_framerate : 30.0);
    snap.compression_ratio = (total > 0) ? (raw_bps * elapsed) / total : 0.0;

    snap.keyframe_count = kf_cnt;
    snap.avg_keyframe_kb = kf_cnt > 0 ? (kf_bytes / 1024.0) / kf_cnt : 0.0;
    snap.delta_frame_count = frames > kf_cnt ? frames - kf_cnt : 0;
    snap.avg_delta_kb = snap.delta_frame_count > 0
        ? (df_bytes / 1024.0) / snap.delta_frame_count : 0.0;

    {
        std::lock_guard<std::mutex> lk(pace_mutex_);
        snap.pace_queue_length = static_cast<uint32_t>(pace_queue_.size());
    }
    {
        std::lock_guard<std::mutex> lk(appsrc_mutex_);
        snap.connected_clients = static_cast<uint32_t>(appsrcs_.size());
    }
    if (snap.connected_clients == 0)
        snap.connected_clients = soft_client_count_.load();
    {
        std::lock_guard<std::mutex> lk(feedback_mutex_);
        float worst = 0.0f;
        for (const auto& [ssrc, fb] : client_feedback_)
            if (fb.fraction_lost > worst) worst = fb.fraction_lost;
        snap.worst_client_loss_pct = worst * 100.0;
    }

    snap.abr_bitrate_kbps = current_bitrate_kbps_;
    snap.uptime_seconds = elapsed;
    snap.total_encoded_bytes = total;
    snap.ingest_frame_count = ingest_buf_count_;
    snap.abr_enabled = abr_enabled_.load(std::memory_order_relaxed);
    snap.reconnecting = reconnecting_.load(std::memory_order_relaxed);
    return snap;
}

SourceAvailabilitySnapshot RtspBridge::getSourceAvailability() const {
    SourceAvailabilitySnapshot snap;
    if (!running_) {
        snap.error_message = "Bridge not running";
        return snap;
    }

    bool has_error = ingest_error_.load(std::memory_order_acquire);
    // In software single-pipeline mode, ingest_pipeline_ is null — source
    // availability is inferred from the running state and error flag.
    GstElement* pipeline = ingest_pipeline_;  // snapshot the pointer once
    snap.reachable = !has_error && (pipeline != nullptr || !config_.use_hw_codec);

    if (pipeline && appsink_) {
        GstPad* pad = gst_element_get_static_pad(appsink_, "sink");
        if (pad) {
            GstCaps* caps = gst_pad_get_current_caps(pad);
            if (caps && gst_caps_get_size(caps) > 0) {
                GstStructure* s = gst_caps_get_structure(caps, 0);
                const gchar* name = gst_structure_get_name(s);
                if (name) snap.codec = name;

                gst_structure_get_int(s, "width", &snap.width);
                gst_structure_get_int(s, "height", &snap.height);

                gint num = 0, den = 1;
                if (gst_structure_get_fraction(s, "framerate", &num, &den) && den > 0)
                    snap.framerate = static_cast<double>(num) / den;
            }
            if (caps) gst_caps_unref(caps);
            gst_object_unref(pad);
        }
    }

    // Fill from config as fallback (software mode or caps not yet negotiated)
    if (snap.codec.empty()) snap.codec = config_.output_codec;
    if (snap.width == 0)    snap.width = config_.output_width;
    if (snap.height == 0)   snap.height = config_.output_height;
    if (snap.framerate == 0.0) snap.framerate = config_.output_framerate;

    if (has_error)
        snap.error_message = "Ingest pipeline error (reconnecting)";

    snap.bitrate_bps = config_.output_bitrate * 1000;
    return snap;
}

void RtspBridge::applyAbrOverride(bool enable, int bitrate_kbps) {
    abr_enabled_ = enable;
    if (bitrate_kbps > 0 && encoder_)
        applyBitrate(bitrate_kbps);
    std::cout << "[ABR] Override: enabled=" << (enable ? "true" : "false")
              << " bitrate=" << bitrate_kbps << "kbps\n";
}

bool RtspBridge::triggerReconnect() {
    if (!config_.use_hw_codec) {
        std::cerr << "[RtspBridge] Reconnect not supported in software single-pipeline mode.\n";
        return false;
    }
    std::lock_guard<std::mutex> lk(reconnect_signal_mutex_);
    reconnect_needed_ = true;
    reconnect_signal_cv_.notify_one();
    std::cout << "[RtspBridge] Reconnect triggered.\n";
    return true;
}
