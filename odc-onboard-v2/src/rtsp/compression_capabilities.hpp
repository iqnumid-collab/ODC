#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

#ifdef ODC_HAS_RTSP_COMPRESSOR
#include "HardwareInfo.h"
#include "RtspBridge.h"
#endif

namespace odc {

/// Owns one-shot (refreshable) compression HW inventory, persists it to disk,
/// and applies compressor pipeline rules from that inventory.
class CompressionCapabilities {
public:
    enum class HwPolicy {
        Auto,      // prefer HW when available for the codec
        ForceHw,   // request HW; validate may fall back to SW
        ForceSw,   // always software
    };

    explicit CompressionCapabilities(
        std::filesystem::path cache_path = "/var/lib/odc-onboard/compression-capabilities.json");

    /// Detect (or reload cache + optional force re-probe), then autosave.
    bool ensure(bool force_redetect = false);

    bool refresh();  // force_redetect = true
    bool save() const;
    bool load();

    bool ready() const;
    nlohmann::json to_json() const;

    static HwPolicy parse_hw_policy(const std::string& value);
    static const char* hw_policy_name(HwPolicy policy);

#ifdef ODC_HAS_RTSP_COMPRESSOR
    HardwareInfo hardware() const;

    /// Apply inventory + policy to a BridgeConfig before start().
    /// Returns false if the codec cannot be encoded on this host.
    bool apply_rules(BridgeConfig& cfg, HwPolicy policy, std::string* error_out = nullptr) const;
#endif

private:
    mutable std::mutex mutex_;
    std::filesystem::path cache_path_;
    bool ready_{false};
    std::string detected_at_;
#ifdef ODC_HAS_RTSP_COMPRESSOR
    HardwareInfo hw_{};
#endif

    static std::string now_iso8601();
};

} // namespace odc
