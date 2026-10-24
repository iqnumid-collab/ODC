#pragma once

#include <gst/gst.h>

#include <string>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <cstdint>

struct BandwidthStats {
    double current_kbps = 0.0;
    double average_kbps = 0.0;
    double peak_kbps = 0.0;
    uint64_t total_bytes = 0;
    double elapsed_seconds = 0.0;
    uint64_t frame_count = 0;
    uint64_t dropped_frames = 0;
};

struct ThrottleConfig {
    uint32_t maxBandwidthKbps = 0;
    enum Mode { Delay, Drop } mode = Delay;
    double updateIntervalMs = 500;
    bool randThrottle = false;
    uint32_t randIntervalSec = 5;
    uint32_t randMinPercent = 20;
    uint32_t randMaxPercent = 100;
};

class BandwidthThrottler {
public:
    explicit BandwidthThrottler(ThrottleConfig config);
    ~BandwidthThrottler();

    BandwidthThrottler(const BandwidthThrottler&) = delete;
    BandwidthThrottler& operator=(const BandwidthThrottler&) = delete;

    void installOnPad(GstPad* pad);
    void remove();

    BandwidthStats getStats() const;
    void startMonitoring();
    void stopMonitoring();

    bool isActive() const;
    uint32_t getEffectiveBandwidth() const;

private:
    ThrottleConfig config_;

    double tokens_;
    double maxTokens_;
    std::mutex tokenMutex_;
    std::chrono::steady_clock::time_point lastReplenish_;
    std::chrono::steady_clock::time_point lastBufferTime_;

    mutable std::mutex statsMutex_;
    BandwidthStats stats_;
    std::chrono::steady_clock::time_point startTime_;
    std::chrono::steady_clock::time_point lastSampleTime_;
    uint64_t lastSampleBytes_ = 0;
    uint64_t windowBytes_ = 0;

    std::atomic<bool> monitoring_{false};
    std::thread monitorThread_;

    std::atomic<bool> randRunning_{false};
    std::thread randThread_;
    std::atomic<uint32_t> effectiveBandwidthKbps_{0};

    GstPad* probedPad_ = nullptr;
    guint probeId_ = 0;

    static GstPadProbeReturn onBufferProbe(GstPad*, GstPadProbeInfo*, gpointer);
    void updateStats(uint64_t bufferSize, bool isKeyframe);
    bool shouldDelay(uint64_t bufferSize);
    bool shouldDrop(bool isKeyframe);
    void replenishTokens();
    void monitorLoop();
    void randThrottleLoop();
    void setEffectiveBandwidth(uint32_t kbps);
};
