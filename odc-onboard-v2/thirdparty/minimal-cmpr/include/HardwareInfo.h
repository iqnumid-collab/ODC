#pragma once

#include <string>
#include <vector>

enum class PipelineStrategy { JetsonHW, DesktopNVENC, VAAPI, Software };

struct HardwareInfo {
    // Architecture
    std::string arch;           // "aarch64", "x86_64", etc.
    std::string board_model;    // from /sys/firmware/devicetree/base/model or "unknown"
    int tegra_chip_id = 0;      // 0 if not Jetson

    // Resources
    int cpu_cores = 0;
    int total_ram_mb = 0;

    // GPU / hardware encoder capabilities
    bool has_jetson_hw_encoder = false;  // nvv4l2h264enc / nvv4l2h265enc
    bool has_nvenc_encoder     = false;  // nvh264enc / nvh265enc
    bool has_vaapi_encoder     = false;  // vaapih264enc / vaapih265enc
    bool has_integrated_gpu_only = false;

    // Per-codec hardware availability
    bool has_h264_hw_encoder = false;
    bool has_h265_hw_encoder = false;

    // Software / platform encoder plugins
    bool has_x264enc     = false;
    bool has_x265enc     = false;
    bool has_openh264enc = false;
    bool has_v4l2h264enc = false;

    // Concrete element names that probed successfully
    std::vector<std::string> available_encoders;

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

/// True if any hardware encoder path is available.
inline bool hardwareHasGpuEncoder(const HardwareInfo& hw) {
    return hw.has_jetson_hw_encoder || hw.has_nvenc_encoder || hw.has_vaapi_encoder;
}

/// True if the given output codec can be encoded somehow (HW or SW).
bool hardwareSupportsCodec(const HardwareInfo& hw, const std::string& codec);
