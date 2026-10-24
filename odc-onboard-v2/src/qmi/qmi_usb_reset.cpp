#include "qmi_usb_reset.hpp"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace odc {
namespace qmi {

static std::string basename_of(const std::string& path) {
    auto pos = path.find_last_of('/');
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

static bool write_sysfs(const fs::path& path, const std::string& value) {
    std::ofstream f(path);
    if (!f) {
        return false;
    }
    f << value;
    return static_cast<bool>(f);
}

bool usb_reset_device(const std::string& cdc_wdm_path) {
    if (cdc_wdm_path.empty()) {
        return false;
    }

    const std::string node = basename_of(cdc_wdm_path);
    const fs::path usbmisc = fs::path("/sys/class/usbmisc") / node / "device";

    std::error_code ec;
    if (!fs::exists(usbmisc, ec)) {
        SPDLOG_WARN("qmi: usb reset: {} missing", usbmisc.string());
        return false;
    }

    fs::path usb_dev = fs::canonical(usbmisc, ec);
    if (ec) {
        SPDLOG_WARN("qmi: usb reset: cannot resolve {}: {}", usbmisc.string(), ec.message());
        return false;
    }

    // Walk up to the USB device that exposes authorized/reset.
    fs::path cur = usb_dev;
    for (int i = 0; i < 6; ++i) {
        const fs::path authorized = cur / "authorized";
        const fs::path reset = cur / "reset";
        if (fs::exists(authorized, ec)) {
            SPDLOG_INFO("qmi: USB authorized cycle on {}", cur.string());
            bool ok = write_sysfs(authorized, "0");
            if (ok) {
                ok = write_sysfs(authorized, "1");
            }
            if (ok) {
                return true;
            }
        }
        if (fs::exists(reset, ec)) {
            SPDLOG_INFO("qmi: USB reset on {}", cur.string());
            if (write_sysfs(reset, "1")) {
                return true;
            }
        }
        cur = cur.parent_path();
        if (cur.empty() || cur == cur.root_path()) {
            break;
        }
    }

    SPDLOG_WARN("qmi: usb reset: no authorized/reset found for {}", cdc_wdm_path);
    return false;
}

} // namespace qmi
} // namespace odc
