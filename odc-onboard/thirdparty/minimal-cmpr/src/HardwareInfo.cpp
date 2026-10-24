#include "HardwareInfo.h"

#include <gst/gst.h>
#include <sys/utsname.h>

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

const char* strategyName(PipelineStrategy s) {
    switch (s) {
        case PipelineStrategy::JetsonHW:     return "Jetson HW (nvv4l2)";
        case PipelineStrategy::DesktopNVENC: return "Desktop NVENC";
        case PipelineStrategy::VAAPI:        return "VA-API";
        case PipelineStrategy::Software:     return "Software";
    }
    return "Unknown";
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

    // Encoder availability
    hw.has_jetson_hw_encoder = gstElementExists("nvv4l2h264enc")
                            || gstElementExists("nvv4l2h265enc");
    hw.has_nvenc_encoder     = gstElementExists("nvh264enc")
                            || gstElementExists("nvh265enc");
    hw.has_vaapi_encoder     = gstElementExists("vaapih264enc")
                            || gstElementExists("vaapih265enc");
    hw.has_integrated_gpu_only = !hw.has_jetson_hw_encoder
                              && !hw.has_nvenc_encoder
                              && !hw.has_vaapi_encoder;

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
              << "  Strategy     : " << strategyName(hw.strategy) << "\n"
              << "========================\n\n";
}
