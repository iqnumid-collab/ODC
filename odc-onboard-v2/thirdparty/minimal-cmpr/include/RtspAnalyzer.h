#pragma once

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/pbutils/pbutils.h>

#include <string>
#include <atomic>
#include <mutex>
#include <chrono>
#include <functional>

struct VideoInfo {
    std::string codec;
    int width = 0;
    int height = 0;
    double framerate = 0.0;
    int bitrate = 0;          // bits per second (from SDP/caps if available)
    std::string format;        // pixel format or encoding profile
    std::string transport;     // RTP/UDP/TCP
};

struct BandwidthStats {
    double current_kbps = 0.0;     // instantaneous bandwidth (kbps)
    double average_kbps = 0.0;     // running average bandwidth (kbps)
    double peak_kbps = 0.0;        // peak bandwidth observed (kbps)
    uint64_t total_bytes = 0;      // total bytes received
    double elapsed_seconds = 0.0;  // time since monitoring started
    uint64_t frame_count = 0;      // total frames received
};

class RtspAnalyzer {
public:
    RtspAnalyzer();
    ~RtspAnalyzer();

    RtspAnalyzer(const RtspAnalyzer&) = delete;
    RtspAnalyzer& operator=(const RtspAnalyzer&) = delete;

    /// Connect to an RTSP stream and begin analysis.
    /// @param uri  Full RTSP URI, e.g. "rtsp://user:pass@192.168.1.10:554/stream"
    /// @param use_tcp  If true, force TCP interleaved transport (useful behind NAT).
    /// @param view     If true, open a window displaying the video stream.
    /// @return true if pipeline started successfully.
    bool start(const std::string& uri, bool use_tcp = false, bool view = false);

    /// Stop the pipeline and release resources.
    void stop();

    /// Returns true while the pipeline is running.
    bool isRunning() const;

    /// Retrieve the video metadata collected at stream start.
    VideoInfo getVideoInfo() const;

    /// Retrieve the latest bandwidth statistics.
    BandwidthStats getBandwidthStats() const;

    /// Register a callback invoked every time bandwidth stats are updated.
    using BandwidthCallback = std::function<void(const BandwidthStats&)>;
    void onBandwidthUpdate(BandwidthCallback cb);

private:
    // GStreamer elements
    GstElement* pipeline_ = nullptr;
    GstElement* source_ = nullptr;
    GstElement* depay_ = nullptr;
    GstElement* parse_ = nullptr;
    GstElement* decode_ = nullptr;
    GstElement* sink_ = nullptr;
    GstElement* videoconvert_ = nullptr;
    GstElement* identity_ = nullptr;
    GstBus* bus_ = nullptr;

    // State
    std::atomic<bool> running_{false};
    bool view_enabled_ = false;
    std::string uri_;

    // Video info
    mutable std::mutex info_mutex_;
    VideoInfo video_info_;
    bool info_collected_ = false;

    // Bandwidth tracking
    mutable std::mutex bw_mutex_;
    BandwidthStats bw_stats_;
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_sample_time_;
    uint64_t last_sample_bytes_ = 0;

    BandwidthCallback bw_callback_;

    // Internal helpers
    bool buildPipeline(bool use_tcp, bool view);
    void collectVideoInfo(GstPad* pad);
    void updateBandwidth(uint64_t buffer_size);

    // GStreamer callbacks (static trampolines)
    static void onPadAdded(GstElement* src, GstPad* new_pad, gpointer user_data);
    static GstPadProbeReturn onBufferProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
    static gboolean onBusMessage(GstBus* bus, GstMessage* msg, gpointer user_data);
};
