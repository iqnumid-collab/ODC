#include "HardwareInfo.h"

#include <gst/gst.h>
#include <sys/utsname.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

// Read entire file content, trimming trailing null/newline. Returns empty on failure.
static std::string readSysFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::string content;
    std::getline(f, content, '\0');
    while (!content.empty() && (content.back() == '\n' || content.back() == '\0'))
        content.pop_back();
    return content;
}

static int readIntFile(const char* path) {
    std::ifstream f(path);
    int val = 0;
    if (f.is_open()) f >> val;
    return val;
}

static int parseMemTotalMB() {
    std::ifstream f("/proc/meminfo");
    if (!f.is_open()) return 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            std::istringstream ss(line);
            std::string label;
            long kb = 0;
            ss >> label >> kb;
            return static_cast<int>(kb / 1024);
        }
    }
    return 0;
}

static bool gstElementExists(const char* name) {
    // gst_element_factory_find only checks the registry — the plugin may
    // fail to load at instantiation time (e.g. CUDA version mismatch).
    // Actually create the element to confirm it works.
    GstElement* el = gst_element_factory_make(name, nullptr);
    if (el) {
        gst_object_unref(el);
        return true;
    }
    return false;
}

static void probeEncoder(HardwareInfo& hw, const char* name, bool* flag = nullptr) {
    if (!gstElementExists(name)) return;
    hw.available_encoders.emplace_back(name);
    if (flag) *flag = true;
}

const char* strategyName(PipelineStrategy s) {
    switch (s) {
        case PipelineStrategy::JetsonHW:     return "Jetson HW (nvv4l2)";
        case PipelineStrategy::DesktopNVENC: return "Desktop NVENC";
        case PipelineStrategy::VAAPI:        return "VA-API";
        case PipelineStrategy::Software:     return "Software";
    }
    return "Unknown";
}

bool hardwareSupportsCodec(const HardwareInfo& hw, const std::string& codec_in) {
    std::string codec = codec_in;
    std::transform(codec.begin(), codec.end(), codec.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (codec.empty() || codec == "copy") return true;
    if (codec == "h264" || codec == "avc") {
        return hw.has_h264_hw_encoder || hw.has_x264enc || hw.has_openh264enc || hw.has_v4l2h264enc;
    }
    if (codec == "h265" || codec == "hevc") {
        return hw.has_h265_hw_encoder || hw.has_x265enc;
    }
    return false;
}

HardwareInfo detectHardware() {
    HardwareInfo hw;

    // Architecture
    struct utsname uts{};
    if (uname(&uts) == 0)
        hw.arch = uts.machine;

    // Board model (Jetson / device-tree)
    hw.board_model = readSysFile("/sys/firmware/devicetree/base/model");
    if (hw.board_model.empty())
        hw.board_model = "unknown";

    // Tegra chip ID
    hw.tegra_chip_id = readIntFile("/sys/module/tegra_fuse/parameters/tegra_chip_id");
    if (hw.tegra_chip_id == 0)
        hw.tegra_chip_id = readIntFile("/sys/devices/soc0/soc_id");

    // CPU / RAM
    hw.cpu_cores = static_cast<int>(std::thread::hardware_concurrency());
    if (hw.cpu_cores < 1) hw.cpu_cores = 1;
    hw.total_ram_mb = parseMemTotalMB();

    // GStreamer version
    guint major, minor, micro, nano;
    gst_version(&major, &minor, &micro, &nano);
    hw.gstreamer_version = std::to_string(major) + "." + std::to_string(minor)
                         + "." + std::to_string(micro);

    // Hardware encoder availability (create-probe)
    const bool jetson_h264 = gstElementExists("nvv4l2h264enc");
    const bool jetson_h265 = gstElementExists("nvv4l2h265enc");
    const bool nvenc_h264  = gstElementExists("nvh264enc");
    const bool nvenc_h265  = gstElementExists("nvh265enc");
    const bool vaapi_h264  = gstElementExists("vaapih264enc");
    const bool vaapi_h265  = gstElementExists("vaapih265enc");

    if (jetson_h264) hw.available_encoders.emplace_back("nvv4l2h264enc");
    if (jetson_h265) hw.available_encoders.emplace_back("nvv4l2h265enc");
    if (nvenc_h264)  hw.available_encoders.emplace_back("nvh264enc");
    if (nvenc_h265)  hw.available_encoders.emplace_back("nvh265enc");
    if (vaapi_h264)  hw.available_encoders.emplace_back("vaapih264enc");
    if (vaapi_h265)  hw.available_encoders.emplace_back("vaapih265enc");

    hw.has_jetson_hw_encoder = jetson_h264 || jetson_h265;
    hw.has_nvenc_encoder     = nvenc_h264 || nvenc_h265;
    hw.has_vaapi_encoder     = vaapi_h264 || vaapi_h265;
    hw.has_h264_hw_encoder   = jetson_h264 || nvenc_h264 || vaapi_h264;
    hw.has_h265_hw_encoder   = jetson_h265 || nvenc_h265 || vaapi_h265;
    hw.has_integrated_gpu_only = !hardwareHasGpuEncoder(hw);

    // Software / platform encoders
    probeEncoder(hw, "x264enc", &hw.has_x264enc);
    probeEncoder(hw, "x265enc", &hw.has_x265enc);
    probeEncoder(hw, "openh264enc", &hw.has_openh264enc);
    probeEncoder(hw, "v4l2h264enc", &hw.has_v4l2h264enc);

    // Strategy: prefer Jetson > NVENC > VAAPI > Software
    if (hw.has_jetson_hw_encoder)
        hw.strategy = PipelineStrategy::JetsonHW;
    else if (hw.has_nvenc_encoder)
        hw.strategy = PipelineStrategy::DesktopNVENC;
    else if (hw.has_vaapi_encoder)
        hw.strategy = PipelineStrategy::VAAPI;
    else
        hw.strategy = PipelineStrategy::Software;

    return hw;
}

void printHardwareInfo(const HardwareInfo& hw) {
    std::cout << "\n===== Hardware Info =====\n"
              << "  Arch         : " << hw.arch << "\n"
              << "  Board        : " << hw.board_model << "\n"
              << "  Tegra ChipID : " << hw.tegra_chip_id << "\n"
              << "  CPU cores    : " << hw.cpu_cores << "\n"
              << "  RAM          : " << hw.total_ram_mb << " MB\n"
              << "  GStreamer    : " << hw.gstreamer_version << "\n"
              << "  Jetson HW enc: " << (hw.has_jetson_hw_encoder ? "yes" : "no") << "\n"
              << "  NVENC enc    : " << (hw.has_nvenc_encoder ? "yes" : "no") << "\n"
              << "  VAAPI enc    : " << (hw.has_vaapi_encoder ? "yes" : "no") << "\n"
              << "  H264 HW      : " << (hw.has_h264_hw_encoder ? "yes" : "no") << "\n"
              << "  H265 HW      : " << (hw.has_h265_hw_encoder ? "yes" : "no") << "\n"
              << "  x264enc      : " << (hw.has_x264enc ? "yes" : "no") << "\n"
              << "  x265enc      : " << (hw.has_x265enc ? "yes" : "no") << "\n"
              << "  openh264enc  : " << (hw.has_openh264enc ? "yes" : "no") << "\n"
              << "  v4l2h264enc  : " << (hw.has_v4l2h264enc ? "yes" : "no") << "\n"
              << "  Strategy     : " << strategyName(hw.strategy) << "\n"
              << "========================\n\n";
}
