#include "RtspAnalyzer.h"

#include <gst/video/video-info.h>
#include <iostream>
#include <cmath>
#include <sstream>

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

RtspAnalyzer::RtspAnalyzer() {
    gst_init(nullptr, nullptr);
}

RtspAnalyzer::~RtspAnalyzer() {
    stop();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool RtspAnalyzer::start(const std::string& uri, bool use_tcp, bool view) {
    if (running_) {
        std::cerr << "[RtspAnalyzer] Already running. Call stop() first.\n";
        return false;
    }

    uri_ = uri;
    info_collected_ = false;

    {
        std::lock_guard<std::mutex> lk(bw_mutex_);
        bw_stats_ = BandwidthStats{};
        last_sample_bytes_ = 0;
    }

    view_enabled_ = view;

    if (!buildPipeline(use_tcp, view)) {
        return false;
    }

    start_time_ = std::chrono::steady_clock::now();
    last_sample_time_ = start_time_;

    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "[RtspAnalyzer] Failed to set pipeline to PLAYING.\n";
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    running_ = true;
    std::cout << "[RtspAnalyzer] Pipeline started for: " << uri_ << "\n";
    return true;
}

void RtspAnalyzer::stop() {
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
    depay_ = nullptr;
    parse_ = nullptr;
    decode_ = nullptr;
    sink_ = nullptr;
    videoconvert_ = nullptr;
    identity_ = nullptr;

    std::cout << "[RtspAnalyzer] Pipeline stopped.\n";
}

bool RtspAnalyzer::isRunning() const {
    return running_;
}

VideoInfo RtspAnalyzer::getVideoInfo() const {
    std::lock_guard<std::mutex> lk(info_mutex_);
    return video_info_;
}

BandwidthStats RtspAnalyzer::getBandwidthStats() const {
    std::lock_guard<std::mutex> lk(bw_mutex_);
    return bw_stats_;
}

void RtspAnalyzer::onBandwidthUpdate(BandwidthCallback cb) {
    bw_callback_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Pipeline Construction
// ---------------------------------------------------------------------------

bool RtspAnalyzer::buildPipeline(bool use_tcp, bool view) {
    pipeline_ = gst_pipeline_new("rtsp-analyzer");

    // rtspsrc handles RTSP negotiation, RTCP, and RTP reception.
    source_ = gst_element_factory_make("rtspsrc", "source");
    if (!source_) {
        std::cerr << "[RtspAnalyzer] Failed to create rtspsrc element.\n";
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    g_object_set(source_, "location", uri_.c_str(), nullptr);
    g_object_set(source_, "latency", 200, nullptr);

    if (use_tcp) {
        // 0x4 = GST_RTSP_LOWER_TRANS_TCP
        g_object_set(source_, "protocols", 0x4, nullptr);
    }

    // identity element is used as a probe point to measure raw RTP byte throughput.
    identity_ = gst_element_factory_make("identity", "identity");

    // decodebin will auto-negotiate depayloading + decoding.
    decode_ = gst_element_factory_make("decodebin", "decoder");

    if (view) {
        // autovideosink opens a window to display the video.
        videoconvert_ = gst_element_factory_make("videoconvert", "videoconvert");
        sink_ = gst_element_factory_make("autovideosink", "sink");
    } else {
        // fakesink — we don't render, we just analyse.
        sink_ = gst_element_factory_make("fakesink", "sink");
    }

    if (!identity_ || !decode_ || !sink_) {
        std::cerr << "[RtspAnalyzer] Failed to create pipeline elements.\n";
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }
    if (view && !videoconvert_) {
        std::cerr << "[RtspAnalyzer] Failed to create videoconvert element.\n";
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    if (view) {
        g_object_set(sink_, "sync", TRUE, nullptr);
        gst_bin_add_many(GST_BIN(pipeline_), source_, identity_, decode_, videoconvert_, sink_, nullptr);
        if (!gst_element_link(videoconvert_, sink_)) {
            std::cerr << "[RtspAnalyzer] Failed to link videoconvert -> autovideosink.\n";
            gst_object_unref(pipeline_);
            pipeline_ = nullptr;
            return false;
        }
    } else {
        g_object_set(sink_, "sync", FALSE, nullptr);
        gst_bin_add_many(GST_BIN(pipeline_), source_, identity_, decode_, sink_, nullptr);
    }

    // identity -> decodebin  (linked statically)
    if (!gst_element_link(identity_, decode_)) {
        std::cerr << "[RtspAnalyzer] Failed to link identity -> decodebin.\n";
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    // rtspsrc pads are created dynamically — connect via signal.
    g_signal_connect(source_, "pad-added", G_CALLBACK(onPadAdded), this);

    // decodebin pads are also dynamic — link to sink (or videoconvert->sink) when a src pad appears.
    g_signal_connect(decode_, "pad-added",
        G_CALLBACK(+[](GstElement* /*src*/, GstPad* new_pad, gpointer user_data) {
            auto* self = static_cast<RtspAnalyzer*>(user_data);

            // When view is enabled, link to videoconvert; otherwise link directly to sink.
            GstElement* link_target = self->view_enabled_ ? self->videoconvert_ : self->sink_;
            GstPad* sink_pad = gst_element_get_static_pad(link_target, "sink");
            if (gst_pad_is_linked(sink_pad)) {
                gst_object_unref(sink_pad);
                return;
            }

            GstCaps* caps = gst_pad_get_current_caps(new_pad);
            if (!caps) caps = gst_pad_query_caps(new_pad, nullptr);

            if (caps) {
                const gchar* media_type = gst_structure_get_name(gst_caps_get_structure(caps, 0));
                if (g_str_has_prefix(media_type, "video/")) {
                    // Collect video info from the decoded caps.
                    self->collectVideoInfo(new_pad);

                    GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
                    if (GST_PAD_LINK_FAILED(ret)) {
                        std::cerr << "[RtspAnalyzer] Failed to link decodebin -> sink.\n";
                    }
                }
                gst_caps_unref(caps);
            }
            gst_object_unref(sink_pad);
        }),
        this
    );

    // Install a buffer probe on the identity sink pad to measure bandwidth.
    GstPad* identity_sink_pad = gst_element_get_static_pad(identity_, "sink");
    gst_pad_add_probe(identity_sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
                       onBufferProbe, this, nullptr);
    gst_object_unref(identity_sink_pad);

    // Bus for EOS / error messages.
    bus_ = gst_element_get_bus(pipeline_);
    gst_bus_add_watch(bus_, onBusMessage, this);

    return true;
}

// ---------------------------------------------------------------------------
// Video Info Collection
// ---------------------------------------------------------------------------

void RtspAnalyzer::collectVideoInfo(GstPad* pad) {
    if (info_collected_) return;

    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) return;

    GstStructure* s = gst_caps_get_structure(caps, 0);

    std::lock_guard<std::mutex> lk(info_mutex_);

    // Resolution
    gst_structure_get_int(s, "width", &video_info_.width);
    gst_structure_get_int(s, "height", &video_info_.height);

    // Framerate
    gint fps_n = 0, fps_d = 1;
    if (gst_structure_get_fraction(s, "framerate", &fps_n, &fps_d) && fps_d != 0) {
        video_info_.framerate = static_cast<double>(fps_n) / fps_d;
    }

    // Pixel format
    const gchar* fmt = gst_structure_get_string(s, "format");
    if (fmt) video_info_.format = fmt;

    // Codec — derive from the structure name (e.g. "video/x-raw", "video/x-h264")
    video_info_.codec = gst_structure_get_name(s);

    // Transport protocol
    video_info_.transport = "RTP/UDP";
    // If TCP was forced, note it.
    if (source_) {
        gint protocols = 0;
        g_object_get(source_, "protocols", &protocols, nullptr);
        if (protocols & 0x4) {
            video_info_.transport = "RTP/TCP";
        }
    }

    info_collected_ = true;

    std::cout << "\n===== Video Stream Info =====\n"
              << "  Codec      : " << video_info_.codec << "\n"
              << "  Resolution : " << video_info_.width << "x" << video_info_.height << "\n"
              << "  Framerate  : " << video_info_.framerate << " fps\n"
              << "  Format     : " << video_info_.format << "\n"
              << "  Transport  : " << video_info_.transport << "\n"
              << "=============================\n\n";

    gst_caps_unref(caps);
}

// ---------------------------------------------------------------------------
// Bandwidth Measurement
// ---------------------------------------------------------------------------

void RtspAnalyzer::updateBandwidth(uint64_t buffer_size) {
    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lk(bw_mutex_);

    bw_stats_.total_bytes += buffer_size;
    bw_stats_.frame_count++;

    double total_elapsed = std::chrono::duration<double>(now - start_time_).count();
    bw_stats_.elapsed_seconds = total_elapsed;

    // Average bandwidth over the entire session.
    if (total_elapsed > 0.0) {
        bw_stats_.average_kbps = (bw_stats_.total_bytes * 8.0 / 1000.0) / total_elapsed;
    }

    // Instantaneous bandwidth: bytes since last sample / time since last sample.
    double dt = std::chrono::duration<double>(now - last_sample_time_).count();
    if (dt >= 0.5) {  // update instantaneous reading every 0.5 s
        uint64_t delta_bytes = bw_stats_.total_bytes - last_sample_bytes_;
        bw_stats_.current_kbps = (delta_bytes * 8.0 / 1000.0) / dt;

        if (bw_stats_.current_kbps > bw_stats_.peak_kbps) {
            bw_stats_.peak_kbps = bw_stats_.current_kbps;
        }

        last_sample_bytes_ = bw_stats_.total_bytes;
        last_sample_time_ = now;

        // Fire callback (outside the lock would be better, but kept simple here).
        if (bw_callback_) {
            bw_callback_(bw_stats_);
        }
    }
}

// ---------------------------------------------------------------------------
// GStreamer Callbacks
// ---------------------------------------------------------------------------

void RtspAnalyzer::onPadAdded(GstElement* /*src*/, GstPad* new_pad, gpointer user_data) {
    auto* self = static_cast<RtspAnalyzer*>(user_data);

    GstCaps* caps = gst_pad_get_current_caps(new_pad);
    if (!caps) caps = gst_pad_query_caps(new_pad, nullptr);

    const GstStructure* s = gst_caps_get_structure(caps, 0);
    const gchar* media_type = gst_structure_get_name(s);

    // We only care about video (or application/x-rtp with media=video).
    bool is_video = g_str_has_prefix(media_type, "video/");
    if (!is_video && g_str_has_prefix(media_type, "application/x-rtp")) {
        const gchar* media = gst_structure_get_string(s, "media");
        is_video = media && g_strcmp0(media, "video") == 0;

        // Try to extract codec from encoding-name
        const gchar* encoding = gst_structure_get_string(s, "encoding-name");
        if (encoding) {
            std::lock_guard<std::mutex> lk(self->info_mutex_);
            self->video_info_.codec = encoding;  // e.g. "H264", "H265"
        }
    }

    if (is_video) {
        GstPad* sink_pad = gst_element_get_static_pad(self->identity_, "sink");
        if (!gst_pad_is_linked(sink_pad)) {
            GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
            if (GST_PAD_LINK_FAILED(ret)) {
                std::cerr << "[RtspAnalyzer] Failed to link rtspsrc -> identity.\n";
            } else {
                std::cout << "[RtspAnalyzer] Linked video pad (" << media_type << ").\n";
            }
        }
        gst_object_unref(sink_pad);
    }

    gst_caps_unref(caps);
}

GstPadProbeReturn RtspAnalyzer::onBufferProbe(GstPad* /*pad*/, GstPadProbeInfo* info, gpointer user_data) {
    auto* self = static_cast<RtspAnalyzer*>(user_data);

    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buffer) {
        self->updateBandwidth(gst_buffer_get_size(buffer));
    }

    return GST_PAD_PROBE_OK;
}

gboolean RtspAnalyzer::onBusMessage(GstBus* /*bus*/, GstMessage* msg, gpointer user_data) {
    auto* self = static_cast<RtspAnalyzer*>(user_data);

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            std::cerr << "[RtspAnalyzer] ERROR: " << err->message << "\n";
            if (debug) {
                std::cerr << "[RtspAnalyzer] Debug : " << debug << "\n";
                g_free(debug);
            }
            g_error_free(err);
            self->running_ = false;
            break;
        }
        case GST_MESSAGE_EOS:
            std::cout << "[RtspAnalyzer] End of stream.\n";
            self->running_ = false;
            break;
        case GST_MESSAGE_STATE_CHANGED:
            // Optionally log state transitions for the top-level pipeline.
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->pipeline_)) {
                GstState old_st, new_st, pending;
                gst_message_parse_state_changed(msg, &old_st, &new_st, &pending);
                std::cout << "[RtspAnalyzer] Pipeline state: "
                          << gst_element_state_get_name(old_st) << " -> "
                          << gst_element_state_get_name(new_st) << "\n";
            }
            break;
        default:
            break;
    }

    return TRUE;
}
