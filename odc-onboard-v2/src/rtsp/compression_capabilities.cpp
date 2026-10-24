#include "rtsp/compression_capabilities.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#ifdef ODC_HAS_RTSP_COMPRESSOR
#include <gst/gst.h>
#endif

namespace odc {
namespace {

#ifdef ODC_HAS_RTSP_COMPRESSOR
nlohmann::json hardware_to_json(const HardwareInfo& hw) {
    return nlohmann::json{
        {"arch", hw.arch},
        {"board_model", hw.board_model},
        {"tegra_chip_id", hw.tegra_chip_id},
        {"cpu_cores", hw.cpu_cores},
        {"total_ram_mb", hw.total_ram_mb},
        {"gstreamer_version", hw.gstreamer_version},
        {"strategy", strategyName(hw.strategy)},
        {"has_jetson_hw_encoder", hw.has_jetson_hw_encoder},
        {"has_nvenc_encoder", hw.has_nvenc_encoder},
        {"has_vaapi_encoder", hw.has_vaapi_encoder},
        {"has_integrated_gpu_only", hw.has_integrated_gpu_only},
        {"has_h264_hw_encoder", hw.has_h264_hw_encoder},
        {"has_h265_hw_encoder", hw.has_h265_hw_encoder},
        {"has_x264enc", hw.has_x264enc},
        {"has_x265enc", hw.has_x265enc},
        {"has_openh264enc", hw.has_openh264enc},
        {"has_v4l2h264enc", hw.has_v4l2h264enc},
        {"available_encoders", hw.available_encoders},
        {"rules", {
            {"default_hw_policy", "auto"},
            {"prefer_hw_when_available", hardwareHasGpuEncoder(hw)},
            {"supported_codecs", nlohmann::json{
                {"copy", true},
                {"h264", hardwareSupportsCodec(hw, "h264")},
                {"h265", hardwareSupportsCodec(hw, "h265")},
            }},
        }},
    };
}

HardwareInfo hardware_from_json(const nlohmann::json& j) {
    HardwareInfo hw;
    hw.arch = j.value("arch", "");
    hw.board_model = j.value("board_model", "unknown");
    hw.tegra_chip_id = j.value("tegra_chip_id", 0);
    hw.cpu_cores = j.value("cpu_cores", 0);
    hw.total_ram_mb = j.value("total_ram_mb", 0);
    hw.gstreamer_version = j.value("gstreamer_version", "");
    hw.has_jetson_hw_encoder = j.value("has_jetson_hw_encoder", false);
    hw.has_nvenc_encoder = j.value("has_nvenc_encoder", false);
    hw.has_vaapi_encoder = j.value("has_vaapi_encoder", false);
    hw.has_integrated_gpu_only = j.value("has_integrated_gpu_only", true);
    hw.has_h264_hw_encoder = j.value("has_h264_hw_encoder", false);
    hw.has_h265_hw_encoder = j.value("has_h265_hw_encoder", false);
    hw.has_x264enc = j.value("has_x264enc", false);
    hw.has_x265enc = j.value("has_x265enc", false);
    hw.has_openh264enc = j.value("has_openh264enc", false);
    hw.has_v4l2h264enc = j.value("has_v4l2h264enc", false);
    if (j.contains("available_encoders") && j["available_encoders"].is_array()) {
        for (const auto& e : j["available_encoders"]) {
            hw.available_encoders.push_back(e.get<std::string>());
        }
    }
    const auto strategy = j.value("strategy", std::string("Software"));
    if (strategy.find("Jetson") != std::string::npos) hw.strategy = PipelineStrategy::JetsonHW;
    else if (strategy.find("NVENC") != std::string::npos) hw.strategy = PipelineStrategy::DesktopNVENC;
    else if (strategy.find("VA-API") != std::string::npos || strategy.find("VAAPI") != std::string::npos)
        hw.strategy = PipelineStrategy::VAAPI;
    else hw.strategy = PipelineStrategy::Software;
    return hw;
}
#endif

} // namespace

CompressionCapabilities::CompressionCapabilities(std::filesystem::path cache_path)
    : cache_path_(std::move(cache_path)) {}

std::string CompressionCapabilities::now_iso8601() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

CompressionCapabilities::HwPolicy CompressionCapabilities::parse_hw_policy(const std::string& value) {
    if (value == "force_hw" || value == "hw") return HwPolicy::ForceHw;
    if (value == "force_sw" || value == "sw" || value == "software") return HwPolicy::ForceSw;
    return HwPolicy::Auto;
}

const char* CompressionCapabilities::hw_policy_name(HwPolicy policy) {
    switch (policy) {
        case HwPolicy::Auto: return "auto";
        case HwPolicy::ForceHw: return "force_hw";
        case HwPolicy::ForceSw: return "force_sw";
    }
    return "auto";
}

bool CompressionCapabilities::ready() const {
    std::lock_guard lock(mutex_);
    return ready_;
}

bool CompressionCapabilities::ensure(bool force_redetect) {
#ifdef ODC_HAS_RTSP_COMPRESSOR
    std::lock_guard lock(mutex_);
    if (ready_ && !force_redetect) {
        return true;
    }

    if (!force_redetect && std::filesystem::exists(cache_path_)) {
        try {
            std::ifstream in(cache_path_);
            nlohmann::json doc;
            in >> doc;
            if (doc.contains("hardware")) {
                hw_ = hardware_from_json(doc["hardware"]);
                detected_at_ = doc.value("detected_at", "");
                ready_ = true;
                return true;
            }
        } catch (...) {
            // fall through to re-detect
        }
    }

    static std::once_flag gst_once;
    std::call_once(gst_once, [] { gst_init(nullptr, nullptr); });

    hw_ = detectHardware();
    detected_at_ = now_iso8601();
    ready_ = true;
    printHardwareInfo(hw_);

    // Autosave outside the "already ready" path
    try {
        std::error_code ec;
        std::filesystem::create_directories(cache_path_.parent_path(), ec);
        const auto tmp = cache_path_.string() + ".tmp";
        {
            std::ofstream out(tmp);
            out << nlohmann::json{
                {"version", 1},
                {"detected_at", detected_at_},
                {"hardware", hardware_to_json(hw_)},
            }.dump(2);
        }
        std::filesystem::rename(tmp, cache_path_, ec);
    } catch (...) {
        // Detection still succeeded even if persist failed.
    }
    return true;
#else
    (void)force_redetect;
    std::lock_guard lock(mutex_);
    ready_ = false;
    return false;
#endif
}

bool CompressionCapabilities::refresh() {
    return ensure(true);
}

bool CompressionCapabilities::save() const {
#ifdef ODC_HAS_RTSP_COMPRESSOR
    std::lock_guard lock(mutex_);
    if (!ready_) return false;
    try {
        std::error_code ec;
        std::filesystem::create_directories(cache_path_.parent_path(), ec);
        const auto tmp = cache_path_.string() + ".tmp";
        {
            std::ofstream out(tmp);
            out << nlohmann::json{
                {"version", 1},
                {"detected_at", detected_at_},
                {"hardware", hardware_to_json(hw_)},
            }.dump(2);
        }
        std::filesystem::rename(tmp, cache_path_, ec);
        return !ec;
    } catch (...) {
        return false;
    }
#else
    return false;
#endif
}

bool CompressionCapabilities::load() {
    return ensure(false);
}

nlohmann::json CompressionCapabilities::to_json() const {
    std::lock_guard lock(mutex_);
#ifdef ODC_HAS_RTSP_COMPRESSOR
    if (!ready_) {
        return nlohmann::json{{"ready", false}, {"error", "not_detected"}};
    }
    return nlohmann::json{
        {"ready", true},
        {"detected_at", detected_at_},
        {"cache_path", cache_path_.string()},
        {"hardware", hardware_to_json(hw_)},
    };
#else
    return nlohmann::json{
        {"ready", false},
        {"error", "rtsp_compressor_not_built"},
        {"cache_path", cache_path_.string()},
    };
#endif
}

#ifdef ODC_HAS_RTSP_COMPRESSOR
HardwareInfo CompressionCapabilities::hardware() const {
    std::lock_guard lock(mutex_);
    return hw_;
}

bool CompressionCapabilities::apply_rules(BridgeConfig& cfg, HwPolicy policy,
                                          std::string* error_out) const {
    std::lock_guard lock(mutex_);
    if (!ready_) {
        if (error_out) *error_out = "compression capabilities not detected";
        return false;
    }

    std::string codec = cfg.output_codec;
    std::transform(codec.begin(), codec.end(), codec.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (!hardwareSupportsCodec(hw_, codec)) {
        if (error_out) {
            *error_out = "codec '" + cfg.output_codec + "' unsupported on this host "
                         "(no matching HW/SW encoder)";
        }
        return false;
    }

    const bool codec_has_hw =
        (codec == "h264" || codec == "avc") ? hw_.has_h264_hw_encoder :
        (codec == "h265" || codec == "hevc") ? hw_.has_h265_hw_encoder :
        hardwareHasGpuEncoder(hw_);

    switch (policy) {
        case HwPolicy::ForceSw:
            cfg.use_hw_codec = false;
            break;
        case HwPolicy::ForceHw:
            cfg.use_hw_codec = true;
            break;
        case HwPolicy::Auto:
        default:
            cfg.use_hw_codec = codec_has_hw;
            break;
    }

    validateBridgeConfig(cfg, hw_);

    // After validate, software path still needs a SW encoder for non-copy codecs.
    if (!cfg.use_hw_codec && codec != "copy" && !codec.empty()) {
        if (!hardwareSupportsCodec(hw_, codec)) {
            if (error_out) *error_out = "no usable encoder after policy apply";
            return false;
        }
        // validate may have cleared use_hw_codec; re-check SW availability
        const bool sw_ok =
            (codec == "h264" || codec == "avc")
                ? (hw_.has_x264enc || hw_.has_openh264enc || hw_.has_v4l2h264enc)
                : (codec == "h265" || codec == "hevc") ? hw_.has_x265enc : true;
        if (!sw_ok && !cfg.use_hw_codec) {
            if (error_out) {
                *error_out = "software encoder missing for codec '" + cfg.output_codec + "'";
            }
            return false;
        }
    }

    return true;
}
#endif

} // namespace odc
