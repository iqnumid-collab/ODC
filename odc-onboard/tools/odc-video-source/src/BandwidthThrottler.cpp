#include "BandwidthThrottler.h"

#include <spdlog/spdlog.h>
#include <iomanip>
#include <cmath>
#include <random>

BandwidthThrottler::BandwidthThrottler(ThrottleConfig config)
    : config_(std::move(config)), tokens_(0) {
    if (config_.randThrottle) {
        effectiveBandwidthKbps_ = config_.maxBandwidthKbps;
    } else {
        effectiveBandwidthKbps_ = config_.maxBandwidthKbps;
    }
}

BandwidthThrottler::~BandwidthThrottler() {
    stopMonitoring();
    remove();
    if (randRunning_) {
        randRunning_ = false;
        if (randThread_.joinable()) {
            randThread_.join();
        }
    }
}

void BandwidthThrottler::installOnPad(GstPad* pad) {
    if (!pad || config_.maxBandwidthKbps == 0) {
        spdlog::debug("[Throttler] Not installing probe: pad={} maxBandwidth={}",
                      pad ? "valid" : "null", config_.maxBandwidthKbps);
        return;
    }

    remove();

    probedPad_ = pad;
    probeId_ = gst_pad_add_probe(
        pad,
        GST_PAD_PROBE_TYPE_BUFFER,
        onBufferProbe,
        this,
        nullptr
    );

    if (probeId_ != 0) {
        spdlog::info("[Throttler] Probe installed on pad (id={})", probeId_);
    } else {
        spdlog::warn("[Throttler] Failed to install probe on pad");
    }
}

void BandwidthThrottler::remove() {
    if (probedPad_ && probeId_ != 0) {
        if (GST_IS_PAD(probedPad_)) {
            gst_pad_remove_probe(probedPad_, probeId_);
            spdlog::debug("[Throttler] Probe removed");
        }
        probedPad_ = nullptr;
        probeId_ = 0;
    }
}

BandwidthStats BandwidthThrottler::getStats() const {
    std::lock_guard<std::mutex> lk(statsMutex_);
    return stats_;
}

void BandwidthThrottler::startMonitoring() {
    if (monitoring_ || config_.maxBandwidthKbps == 0) return;

    monitoring_ = true;
    startTime_ = std::chrono::steady_clock::now();
    lastSampleTime_ = startTime_;
    lastReplenish_ = startTime_;
    lastBufferTime_ = startTime_;

    {
        std::lock_guard<std::mutex> lk(statsMutex_);
        stats_ = BandwidthStats{};
        lastSampleBytes_ = 0;
        windowBytes_ = 0;
    }

    {
        std::lock_guard<std::mutex> lk(tokenMutex_);
        double bytesPerSecond = (getEffectiveBandwidth() * 1000.0) / 8.0;
        tokens_ = 0;
        maxTokens_ = bytesPerSecond * 0.1;
    }

    monitorThread_ = std::thread(&BandwidthThrottler::monitorLoop, this);
    spdlog::info("[Throttler] Monitoring started (interval={}ms, maxBW={}kbps)",
                 config_.updateIntervalMs, config_.maxBandwidthKbps);

    if (config_.randThrottle) {
        randRunning_ = true;
        randThread_ = std::thread(&BandwidthThrottler::randThrottleLoop, this);
        spdlog::info("[Throttler] Random throttle enabled (interval={}s, range={}-{}%)",
                     config_.randIntervalSec, config_.randMinPercent, config_.randMaxPercent);
    }
}

void BandwidthThrottler::stopMonitoring() {
    if (!monitoring_) return;

    monitoring_ = false;
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
    spdlog::info("[Throttler] Monitoring stopped");

    if (randRunning_) {
        randRunning_ = false;
        if (randThread_.joinable()) {
            randThread_.join();
        }
        spdlog::info("[Throttler] Random throttle stopped");
    }
}

bool BandwidthThrottler::isActive() const {
    return monitoring_ && config_.maxBandwidthKbps > 0;
}

uint32_t BandwidthThrottler::getEffectiveBandwidth() const {
    return effectiveBandwidthKbps_.load();
}

void BandwidthThrottler::setEffectiveBandwidth(uint32_t kbps) {
    effectiveBandwidthKbps_.store(kbps);
}

GstPadProbeReturn BandwidthThrottler::onBufferProbe(GstPad*, GstPadProbeInfo* info, gpointer user_data) {
    auto* self = static_cast<BandwidthThrottler*>(user_data);

    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buffer) {
        return GST_PAD_PROBE_OK;
    }

    uint64_t bufferSize = gst_buffer_get_size(buffer);

    bool isKeyframe = !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);

    if (self->config_.maxBandwidthKbps == 0) {
        self->updateStats(bufferSize, isKeyframe);
        return GST_PAD_PROBE_OK;
    }

    uint32_t effectiveBW = self->getEffectiveBandwidth();

    if (self->config_.mode == ThrottleConfig::Drop) {
        if (self->shouldDrop(isKeyframe)) {
            spdlog::debug("[Throttler] Dropping {} frame ({} bytes)",
                          isKeyframe ? "key" : "delta", bufferSize);
            {
                std::lock_guard<std::mutex> lk(self->statsMutex_);
                self->stats_.dropped_frames++;
            }
            return GST_PAD_PROBE_DROP;
        }
    } else {
        double bytesPerSecond = (effectiveBW * 1000.0) / 8.0;

        self->replenishTokens();

        {
            std::lock_guard<std::mutex> lk(self->tokenMutex_);
            if (self->tokens_ < static_cast<double>(bufferSize)) {
                double deficitBytes = static_cast<double>(bufferSize) - self->tokens_;
                double sleepSeconds = deficitBytes / bytesPerSecond;
                if (sleepSeconds > 0.0005 && sleepSeconds < 2.0) {
                    g_usleep(static_cast<guint64>(sleepSeconds * 1000000));
                }
                self->tokens_ = 0;
            } else {
                self->tokens_ -= static_cast<double>(bufferSize);
            }
        }
    }

    self->updateStats(bufferSize, isKeyframe);
    return GST_PAD_PROBE_OK;
}

void BandwidthThrottler::updateStats(uint64_t bufferSize, bool isKeyframe) {
    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lk(statsMutex_);

    stats_.total_bytes += bufferSize;
    stats_.frame_count++;
    windowBytes_ += bufferSize;

    double totalElapsed = std::chrono::duration<double>(now - startTime_).count();
    stats_.elapsed_seconds = totalElapsed;

    if (totalElapsed > 0.0) {
        stats_.average_kbps = (stats_.total_bytes * 8.0 / 1000.0) / totalElapsed;
    }

    double dt = std::chrono::duration<double>(now - lastSampleTime_).count();
    if (dt >= (config_.updateIntervalMs / 1000.0)) {
        uint64_t deltaBytes = stats_.total_bytes - lastSampleBytes_;
        stats_.current_kbps = (deltaBytes * 8.0 / 1000.0) / dt;

        if (stats_.current_kbps > stats_.peak_kbps) {
            stats_.peak_kbps = stats_.current_kbps;
        }

        lastSampleBytes_ = stats_.total_bytes;
        lastSampleTime_ = now;
        windowBytes_ = 0;
    }

    (void)isKeyframe;
}

bool BandwidthThrottler::shouldDrop(bool isKeyframe) {
    if (isKeyframe) return false;

    uint32_t effectiveBW = getEffectiveBandwidth();

    std::lock_guard<std::mutex> lk(statsMutex_);
    double bytesPerSecond = (effectiveBW * 1000.0) / 8.0;
    double windowLimitBytes = bytesPerSecond * (config_.updateIntervalMs / 1000.0);

    return static_cast<double>(windowBytes_) > windowLimitBytes;
}

void BandwidthThrottler::replenishTokens() {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - lastReplenish_).count();

    uint32_t effectiveBW = getEffectiveBandwidth();
    double bytesPerSecond = (effectiveBW * 1000.0) / 8.0;
    double newTokens = bytesPerSecond * elapsed;

    std::lock_guard<std::mutex> lk(tokenMutex_);

    double updated = tokens_ + newTokens;
    if (updated > maxTokens_) {
        updated = maxTokens_;
    }
    tokens_ = updated;

    lastReplenish_ = now;
}

void BandwidthThrottler::monitorLoop() {
    auto lastDataTime = std::chrono::steady_clock::now();

    while (monitoring_) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int>(config_.updateIntervalMs)));

        if (!monitoring_) break;

        BandwidthStats stats;
        {
            std::lock_guard<std::mutex> lk(statsMutex_);
            stats = stats_;
        }

        if (stats.frame_count > 0) {
            lastDataTime = std::chrono::steady_clock::now();
        }

        auto noDataDuration = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - lastDataTime).count();

        if (noDataDuration > 5.0 && stats.elapsed_seconds > 1.0) {
            spdlog::info("[Throttler] No data for {}s, stopping monitor", noDataDuration);
            break;
        }

        if (stats.elapsed_seconds > 0.1) {
            spdlog::info("[BW] current: {:.1f} kbps  avg: {:.1f} kbps  peak: {:.1f} kbps  "
                         "total: {:.1f} MB  frames: {}  elapsed: {:.1f} s",
                         stats.current_kbps,
                         stats.average_kbps,
                         stats.peak_kbps,
                         static_cast<double>(stats.total_bytes) / (1024 * 1024),
                         stats.frame_count,
                         stats.elapsed_seconds);
        }

        replenishTokens();
    }
}

void BandwidthThrottler::randThrottleLoop() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(config_.randMinPercent, config_.randMaxPercent);

    while (randRunning_) {
        std::this_thread::sleep_for(
            std::chrono::seconds(config_.randIntervalSec));

        if (!randRunning_) break;

        uint32_t percent = dist(gen);
        uint32_t newBW = (config_.maxBandwidthKbps * percent) / 100;

        if (newBW < 100) newBW = 100;

        setEffectiveBandwidth(newBW);

        spdlog::info("[RandThrottle] Updated bandwidth: {}kbps ({}% of {}kbps max)",
                     newBW, percent, config_.maxBandwidthKbps);
    }
}
