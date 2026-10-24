#pragma once

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <string>
#include <atomic>
#include <memory>

#include "BandwidthThrottler.h"

struct VideoSourceConfig {
    std::string videoFile;
    std::string rtspAddress = "0.0.0.0";
    int rtspPort = 8554;
    std::string mountPath = "/stream";
    bool loop = true;
    ThrottleConfig throttle;

    std::string codec = "copy";
    int bitrate = 0;
    int width = 0;
    int height = 0;
    double framerate = 0.0;
    std::string preset = "ultrafast";
};

class VideoSource {
public:
    VideoSource();
    ~VideoSource();

    VideoSource(const VideoSource&) = delete;
    VideoSource& operator=(const VideoSource&) = delete;

    bool start(const VideoSourceConfig& config);
    void stop();
    bool isRunning() const;

    BandwidthStats getBandwidthStats() const;
    BandwidthThrottler* getThrottler() const;

private:
    std::atomic<bool> running_{false};
    GstRTSPServer* server_ = nullptr;
    VideoSourceConfig config_;
    std::unique_ptr<BandwidthThrottler> throttler_;

    std::string buildLaunchString() const;
    std::string detectCodecFromFile() const;
};
