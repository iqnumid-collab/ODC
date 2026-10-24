#include "VideoSource.h"

#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

struct MediaConstructedData {
    VideoSource* self;
    GstRTSPMediaFactory* factory;
};

static void onMediaConstructed(GstRTSPMediaFactory* /*factory*/, GstRTSPMedia* media, gpointer user_data) {
    auto* data = static_cast<MediaConstructedData*>(user_data);

    BandwidthThrottler* throttler = data->self->getThrottler();
    if (!throttler) {
        return;
    }

    GstElement* pipeline = gst_rtsp_media_get_element(media);
    if (!pipeline) {
        spdlog::warn("[VideoSource] media-constructed: no pipeline");
        return;
    }

    GstElement* throttleIdentity = gst_bin_get_by_name(GST_BIN(pipeline), "throttle");
    if (!throttleIdentity) {
        throttleIdentity = gst_bin_get_by_name(GST_BIN(pipeline), "throttle2");
    }

    if (throttleIdentity) {
        GstPad* sinkPad = gst_element_get_static_pad(throttleIdentity, "sink");
        if (sinkPad) {
            spdlog::info("[VideoSource] Installing throttle probe on identity element");
            throttler->installOnPad(sinkPad);
            throttler->startMonitoring();
            gst_object_unref(sinkPad);
        }
        gst_object_unref(throttleIdentity);
    } else {
        spdlog::warn("[VideoSource] media-constructed: identity element not found");
    }

    gst_object_unref(pipeline);
}

VideoSource::VideoSource() {
    gst_init(nullptr, nullptr);
}

VideoSource::~VideoSource() {
    stop();
}

bool VideoSource::start(const VideoSourceConfig& config) {
    if (running_) {
        spdlog::warn("[VideoSource] Already running. Call stop() first.");
        return false;
    }

    config_ = config;

    std::string launch = buildLaunchString();
    spdlog::info("[VideoSource] Pipeline launch string:\n  {}", launch);

    server_ = gst_rtsp_server_new();
    if (!server_) {
        spdlog::error("[VideoSource] Failed to create RTSP server.");
        return false;
    }

    gst_rtsp_server_set_address(server_, config_.rtspAddress.c_str());

    gchar portStr[16];
    g_snprintf(portStr, sizeof(portStr), "%d", config_.rtspPort);
    gst_rtsp_server_set_service(server_, portStr);

    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, launch.c_str());
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    if (config_.throttle.maxBandwidthKbps > 0) {
        throttler_ = std::make_unique<BandwidthThrottler>(config_.throttle);

        auto* mediaData = new MediaConstructedData{this, factory};
        g_signal_connect(factory, "media-constructed", G_CALLBACK(onMediaConstructed), mediaData);
    }

    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(server_);
    gst_rtsp_mount_points_add_factory(mounts, config_.mountPath.c_str(), factory);
    g_object_unref(mounts);

    if (gst_rtsp_server_attach(server_, nullptr) == 0) {
        spdlog::error("[VideoSource] Failed to attach RTSP server.");
        g_object_unref(server_);
        server_ = nullptr;
        return false;
    }

    running_ = true;

    spdlog::info("[VideoSource] RTSP server started.");
    spdlog::info("[VideoSource]   Input  : {}", config_.videoFile);
    spdlog::info("[VideoSource]   Output : rtsp://{}:{}{}",
                 config_.rtspAddress, config_.rtspPort, config_.mountPath);
    spdlog::info("[VideoSource]   Loop   : {}", config_.loop ? "yes" : "no");
    spdlog::info("[VideoSource]   Throttle: {} (max: {} kbps, mode: {})",
                 config_.throttle.maxBandwidthKbps > 0 ? "enabled" : "disabled",
                 config_.throttle.maxBandwidthKbps,
                 config_.throttle.mode == ThrottleConfig::Delay ? "delay" : "drop");

    return true;
}

void VideoSource::stop() {
    if (!running_) return;

    running_ = false;

    if (throttler_) {
        throttler_->stopMonitoring();
        throttler_->remove();
        throttler_.reset();
    }

    if (server_) {
        g_object_unref(server_);
        server_ = nullptr;
    }

    spdlog::info("[VideoSource] Bridge stopped.");
}

bool VideoSource::isRunning() const {
    return running_;
}

BandwidthStats VideoSource::getBandwidthStats() const {
    if (throttler_) {
        return throttler_->getStats();
    }
    return BandwidthStats{};
}

BandwidthThrottler* VideoSource::getThrottler() const {
    return throttler_.get();
}

std::string VideoSource::buildLaunchString() const {
    std::ostringstream ss;

    std::string codec = config_.codec;
    std::transform(codec.begin(), codec.end(), codec.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    bool passthrough = codec.empty() || codec == "copy";

    ss << "( filesrc location=" << config_.videoFile << " ! qtdemux name=demux ";

    if (passthrough) {
        std::string detectedCodec = detectCodecFromFile();
        spdlog::info("[VideoSource] Detected codec: {}", detectedCodec);

        ss << "demux.video_0 ! " << detectedCodec << "parse ";

        if (config_.throttle.maxBandwidthKbps > 0) {
            ss << "! identity name=throttle ";
        }

        if (detectedCodec == "h265" || detectedCodec == "hevc") {
            ss << "! rtph265pay name=pay0 pt=96";
        } else {
            ss << "! rtph264pay name=pay0 pt=96";
        }
    } else {
        std::string detectedCodec = detectCodecFromFile();
        spdlog::info("[VideoSource] Detected codec: {} (transcoding to {})", detectedCodec, codec);

        ss << "demux.video_0 ! " << detectedCodec << "parse ! " << detectedCodec << "dec ";

        if (config_.throttle.maxBandwidthKbps > 0) {
            ss << "! identity name=throttle ";
        }

        ss << "! videoconvert";

        if (config_.width > 0 && config_.height > 0) {
            ss << " ! videoscale ! video/x-raw,width="
               << config_.width << ",height=" << config_.height;
        }

        if (config_.framerate > 0.0) {
            int fpsN = static_cast<int>(config_.framerate);
            ss << " ! videorate ! video/x-raw,framerate=" << fpsN << "/1";
        }

        ss << " ! queue max-size-buffers=3 ";

        if (codec == "h264") {
            ss << "! x264enc tune=zerolatency speed-preset=" << config_.preset;
            if (config_.bitrate > 0) {
                ss << " bitrate=" << config_.bitrate;
            }
        } else if (codec == "h265" || codec == "hevc") {
            ss << "! x265enc tune=zerolatency speed-preset=" << config_.preset;
            if (config_.bitrate > 0) {
                ss << " bitrate=" << config_.bitrate;
            }
        } else {
            spdlog::warn("[VideoSource] Unknown codec '{}', falling back to H264.", codec);
            ss << "! x264enc tune=zerolatency speed-preset=" << config_.preset;
            if (config_.bitrate > 0) {
                ss << " bitrate=" << config_.bitrate;
            }
        }

        if (config_.throttle.maxBandwidthKbps > 0) {
            ss << "! identity name=throttle2 ";
        }

        if (codec == "h265" || codec == "hevc") {
            ss << "! rtph265pay name=pay0 pt=96";
        } else {
            ss << "! rtph264pay name=pay0 pt=96";
        }
    }

    ss << " )";
    return ss.str();
}

std::string VideoSource::detectCodecFromFile() const {
    std::ifstream file(config_.videoFile, std::ios::binary);
    if (!file.is_open()) {
        spdlog::warn("[VideoSource] Cannot open video file: {}", config_.videoFile);
        return "h264";
    }

    char header[32];
    file.read(header, sizeof(header));
    std::streamsize bytesRead = file.gcount();

    for (std::streamsize i = 0; i < bytesRead - 4; ++i) {
        if (static_cast<unsigned char>(header[i]) == 0x00 &&
            static_cast<unsigned char>(header[i + 1]) == 0x00 &&
            static_cast<unsigned char>(header[i + 2]) == 0x00 &&
            static_cast<unsigned char>(header[i + 3]) == 0x01) {

            if (i + 4 < bytesRead) {
                unsigned char nalType = header[i + 4] & 0x1F;
                if (nalType == 7) {
                    return "h264";
                } else if (nalType == 32 || nalType == 33 || nalType == 34) {
                    return "h265";
                }
            }
        }
    }

    file.seekg(0);
    std::string content(header, bytesRead);
    if (content.find("hvcC") != std::string::npos ||
        content.find("hev1") != std::string::npos ||
        content.find("hvc1") != std::string::npos) {
        return "h265";
    }
    if (content.find("avcC") != std::string::npos ||
        content.find("avc1") != std::string::npos) {
        return "h264";
    }

    spdlog::warn("[VideoSource] Could not detect codec, defaulting to h264");
    return "h264";
}
