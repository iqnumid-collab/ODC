#include "RtspConsumer.h"

#include <spdlog/spdlog.h>
#include <gst/video/video-info.h>
#include <cmath>

RtspConsumer::RtspConsumer() {
    gst_init(nullptr, nullptr);
}

RtspConsumer::~RtspConsumer() {
    stop();
}

bool RtspConsumer::start(const std::string& uri, bool use_tcp) {
    if (running_) {
        spdlog::warn("[RtspConsumer] Already running. Call stop() first.");
        return false;
    }

    uri_ = uri;
    info_collected_ = false;

    {
        std::lock_guard<std::mutex> lk(bw_mutex_);
        bw_stats_ = BandwidthStats{};
        last_sample_bytes_ = 0;
    }

    if (!buildPipeline(use_tcp)) {
        return false;
    }

    start_time_ = std::chrono::steady_clock::now();
    last_sample_time_ = start_time_;

    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        spdlog::error("[RtspConsumer] Failed to set pipeline to PLAYING.");
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    running_ = true;
    spdlog::info("[RtspConsumer] Pipeline started for: {}", uri_);
    return true;
}

void RtspConsumer::stop() {
    if (!running_) return;

    running_ = false;

    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    if (bus_) {
        gst_object_unref(bus_);
        bus_ = nullptr;
    }

    source_ = nullptr;
    identity_ = nullptr;
    decode_ = nullptr;
    sink_ = nullptr;

    spdlog::info("[RtspConsumer] Pipeline stopped.");
}

bool RtspConsumer::isRunning() const {
    return running_;
}

VideoInfo RtspConsumer::getVideoInfo() const {
    std::lock_guard<std::mutex> lk(info_mutex_);
    return video_info_;
}

BandwidthStats RtspConsumer::getBandwidthStats() const {
    std::lock_guard<std::mutex> lk(bw_mutex_);
    return bw_stats_;
}

void RtspConsumer::onBandwidthUpdate(BandwidthCallback cb) {
    bw_callback_ = std::move(cb);
}

bool RtspConsumer::buildPipeline(bool use_tcp) {
    pipeline_ = gst_pipeline_new("rtsp-consumer");

    source_ = gst_element_factory_make("rtspsrc", "source");
    if (!source_) {
        spdlog::error("[RtspConsumer] Failed to create rtspsrc element.");
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    g_object_set(source_, "location", uri_.c_str(), nullptr);
    g_object_set(source_, "latency", 200, nullptr);

    if (use_tcp) {
        g_object_set(source_, "protocols", 0x4, nullptr);
    }

    identity_ = gst_element_factory_make("identity", "identity");
    decode_ = gst_element_factory_make("decodebin", "decoder");
    sink_ = gst_element_factory_make("fakesink", "sink");

    if (!identity_ || !decode_ || !sink_) {
        spdlog::error("[RtspConsumer] Failed to create pipeline elements.");
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    g_object_set(sink_, "sync", FALSE, nullptr);
    gst_bin_add_many(GST_BIN(pipeline_), source_, identity_, decode_, sink_, nullptr);

    if (!gst_element_link(identity_, decode_)) {
        spdlog::error("[RtspConsumer] Failed to link identity -> decodebin.");
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    g_signal_connect(source_, "pad-added", G_CALLBACK(onPadAdded), this);

    g_signal_connect(decode_, "pad-added",
        G_CALLBACK(+[](GstElement* /*src*/, GstPad* new_pad, gpointer user_data) {
            auto* self = static_cast<RtspConsumer*>(user_data);

            GstPad* sink_pad = gst_element_get_static_pad(self->sink_, "sink");
            if (gst_pad_is_linked(sink_pad)) {
                gst_object_unref(sink_pad);
                return;
            }

            GstCaps* caps = gst_pad_get_current_caps(new_pad);
            if (!caps) caps = gst_pad_query_caps(new_pad, nullptr);

            if (caps) {
                const gchar* media_type = gst_structure_get_name(gst_caps_get_structure(caps, 0));
                if (g_str_has_prefix(media_type, "video/")) {
                    self->collectVideoInfo(new_pad);

                    GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
                    if (GST_PAD_LINK_FAILED(ret)) {
                        spdlog::error("[RtspConsumer] Failed to link decodebin -> sink.");
                    }
                }
                gst_caps_unref(caps);
            }
            gst_object_unref(sink_pad);
        }),
        this
    );

    GstPad* identity_sink_pad = gst_element_get_static_pad(identity_, "sink");
    gst_pad_add_probe(identity_sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
                       onBufferProbe, this, nullptr);
    gst_object_unref(identity_sink_pad);

    bus_ = gst_element_get_bus(pipeline_);
    gst_bus_add_watch(bus_, onBusMessage, this);

    return true;
}

void RtspConsumer::collectVideoInfo(GstPad* pad) {
    if (info_collected_) return;

    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) return;

    GstStructure* s = gst_caps_get_structure(caps, 0);

    std::lock_guard<std::mutex> lk(info_mutex_);

    gst_structure_get_int(s, "width", &video_info_.width);
    gst_structure_get_int(s, "height", &video_info_.height);

    gint fps_n = 0, fps_d = 1;
    if (gst_structure_get_fraction(s, "framerate", &fps_n, &fps_d) && fps_d != 0) {
        video_info_.framerate = static_cast<double>(fps_n) / fps_d;
    }

    const gchar* fmt = gst_structure_get_string(s, "format");
    if (fmt) video_info_.format = fmt;

    video_info_.codec = gst_structure_get_name(s);

    video_info_.transport = "RTP/UDP";
    if (source_) {
        gint protocols = 0;
        g_object_get(source_, "protocols", &protocols, nullptr);
        if (protocols & 0x4) {
            video_info_.transport = "RTP/TCP";
        }
    }

    info_collected_ = true;

    spdlog::info("===== Video Stream Info =====");
    spdlog::info("  Codec      : {}", video_info_.codec);
    spdlog::info("  Resolution : {}x{}", video_info_.width, video_info_.height);
    spdlog::info("  Framerate  : {:.1f} fps", video_info_.framerate);
    spdlog::info("  Format     : {}", video_info_.format);
    spdlog::info("  Transport  : {}", video_info_.transport);
    spdlog::info("=============================");

    gst_caps_unref(caps);
}

void RtspConsumer::updateBandwidth(uint64_t buffer_size) {
    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lk(bw_mutex_);

    bw_stats_.total_bytes += buffer_size;
    bw_stats_.frame_count++;

    double total_elapsed = std::chrono::duration<double>(now - start_time_).count();
    bw_stats_.elapsed_seconds = total_elapsed;

    if (total_elapsed > 0.0) {
        bw_stats_.average_kbps = (bw_stats_.total_bytes * 8.0 / 1000.0) / total_elapsed;
    }

    double dt = std::chrono::duration<double>(now - last_sample_time_).count();
    if (dt >= 0.5) {
        uint64_t delta_bytes = bw_stats_.total_bytes - last_sample_bytes_;
        bw_stats_.current_kbps = (delta_bytes * 8.0 / 1000.0) / dt;

        if (bw_stats_.current_kbps > bw_stats_.peak_kbps) {
            bw_stats_.peak_kbps = bw_stats_.current_kbps;
        }

        last_sample_bytes_ = bw_stats_.total_bytes;
        last_sample_time_ = now;

        if (bw_callback_) {
            bw_callback_(bw_stats_);
        }
    }
}

void RtspConsumer::onPadAdded(GstElement* /*src*/, GstPad* new_pad, gpointer user_data) {
    auto* self = static_cast<RtspConsumer*>(user_data);

    GstCaps* caps = gst_pad_get_current_caps(new_pad);
    if (!caps) caps = gst_pad_query_caps(new_pad, nullptr);

    const GstStructure* s = gst_caps_get_structure(caps, 0);
    const gchar* media_type = gst_structure_get_name(s);

    bool is_video = g_str_has_prefix(media_type, "video/");
    if (!is_video && g_str_has_prefix(media_type, "application/x-rtp")) {
        const gchar* media = gst_structure_get_string(s, "media");
        is_video = media && g_strcmp0(media, "video") == 0;

        const gchar* encoding = gst_structure_get_string(s, "encoding-name");
        if (encoding) {
            std::lock_guard<std::mutex> lk(self->info_mutex_);
            self->video_info_.codec = encoding;
        }
    }

    if (is_video) {
        GstPad* sink_pad = gst_element_get_static_pad(self->identity_, "sink");
        if (!gst_pad_is_linked(sink_pad)) {
            GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
            if (GST_PAD_LINK_FAILED(ret)) {
                spdlog::error("[RtspConsumer] Failed to link rtspsrc -> identity.");
            } else {
                spdlog::info("[RtspConsumer] Linked video pad ({}).", media_type);
            }
        }
        gst_object_unref(sink_pad);
    }

    gst_caps_unref(caps);
}

GstPadProbeReturn RtspConsumer::onBufferProbe(GstPad* /*pad*/, GstPadProbeInfo* info, gpointer user_data) {
    auto* self = static_cast<RtspConsumer*>(user_data);

    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buffer) {
        self->updateBandwidth(gst_buffer_get_size(buffer));
    }

    return GST_PAD_PROBE_OK;
}

gboolean RtspConsumer::onBusMessage(GstBus* /*bus*/, GstMessage* msg, gpointer user_data) {
    auto* self = static_cast<RtspConsumer*>(user_data);

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            spdlog::error("[RtspConsumer] ERROR: {}", err->message);
            if (debug) {
                spdlog::debug("[RtspConsumer] Debug: {}", debug);
                g_free(debug);
            }
            g_error_free(err);
            self->running_ = false;
            break;
        }
        case GST_MESSAGE_EOS:
            spdlog::info("[RtspConsumer] End of stream.");
            self->running_ = false;
            break;
        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->pipeline_)) {
                GstState old_st, new_st, pending;
                gst_message_parse_state_changed(msg, &old_st, &new_st, &pending);
                spdlog::debug("[RtspConsumer] Pipeline state: {} -> {}",
                              gst_element_state_get_name(old_st),
                              gst_element_state_get_name(new_st));
            }
            break;
        default:
            break;
    }

    return TRUE;
}
