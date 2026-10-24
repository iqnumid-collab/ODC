#pragma once

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <string>
#include <atomic>
#include <mutex>
#include <chrono>
#include <functional>
#include <cstdint>

struct BandwidthStats {
    double current_kbps = 0.0;
    double average_kbps = 0.0;
    double peak_kbps = 0.0;
    uint64_t total_bytes = 0;
    double elapsed_seconds = 0.0;
    uint64_t frame_count = 0;
};

struct VideoInfo {
    std::string codec;
    int width = 0;
    int height = 0;
    double framerate = 0.0;
    std::string format;
    std::string transport;
};

class RtspConsumer {
public:
    RtspConsumer();
    ~RtspConsumer();

    RtspConsumer(const RtspConsumer&) = delete;
    RtspConsumer& operator=(const RtspConsumer&) = delete;

    bool start(const std::string& uri, bool use_tcp = false);
    void stop();
    bool isRunning() const;

    VideoInfo getVideoInfo() const;
    BandwidthStats getBandwidthStats() const;

    using BandwidthCallback = std::function<void(const BandwidthStats&)>;
    void onBandwidthUpdate(BandwidthCallback cb);

private:
    GstElement* pipeline_ = nullptr;
    GstElement* source_ = nullptr;
    GstElement* identity_ = nullptr;
    GstElement* decode_ = nullptr;
    GstElement* sink_ = nullptr;
    GstBus* bus_ = nullptr;

    std::atomic<bool> running_{false};
    std::string uri_;

    mutable std::mutex info_mutex_;
    VideoInfo video_info_;
    bool info_collected_ = false;

    mutable std::mutex bw_mutex_;
    BandwidthStats bw_stats_;
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_sample_time_;
    uint64_t last_sample_bytes_ = 0;

    BandwidthCallback bw_callback_;

    bool buildPipeline(bool use_tcp);
    void collectVideoInfo(GstPad* pad);
    void updateBandwidth(uint64_t buffer_size);

    static void onPadAdded(GstElement* src, GstPad* new_pad, gpointer user_data);
    static GstPadProbeReturn onBufferProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
    static gboolean onBusMessage(GstBus* bus, GstMessage* msg, gpointer user_data);
};
