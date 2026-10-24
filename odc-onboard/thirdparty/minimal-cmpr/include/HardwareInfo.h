#pragma once

#include <string>

enum class PipelineStrategy { JetsonHW, DesktopNVENC, VAAPI, Software };

struct HardwareInfo {
    // Architecture
    std::string arch;           // "aarch64", "x86_64", etc.
    std::string board_model;    // from /sys/firmware/devicetree/base/model or "unknown"
    int tegra_chip_id = 0;      // 0 if not Jetson

    // Resources
    int cpu_cores = 0;
    int total_ram_mb = 0;

    // GPU / encoder capabilities
    bool has_jetson_hw_encoder = false;  // nvv4l2h264enc present
    bool has_nvenc_encoder     = false;  // nvh264enc present
    bool has_vaapi_encoder     = false;  // vaapih264enc present
    bool has_integrated_gpu_only = false;

    // Software versions
    std::string gstreamer_version;

    // Recommended strategy
    PipelineStrategy strategy = PipelineStrategy::Software;
};

/// Probe the system and return a populated HardwareInfo.
/// Requires gst_init() to have been called.
HardwareInfo detectHardware();

/// Print hardware summary to stdout.
void printHardwareInfo(const HardwareInfo& hw);

/// Return a human-readable name for the strategy.
const char* strategyName(PipelineStrategy s);
