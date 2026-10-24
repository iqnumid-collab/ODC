#include "odc/platform.hpp"

#ifdef ODC_PLATFORM_WINDOWS

#include "odc/logging.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <asio.hpp>

namespace odc::platform {

std::filesystem::path default_config_path() {
    return "C:\\ProgramData\\odc-root";
}

std::filesystem::path default_data_path() {
    return "C:\\ProgramData\\odc-root\\data";
}

std::filesystem::path default_runtime_path() {
    return "C:\\ProgramData\\odc-root\\run";
}

bool daemonize() {
    auto logger = get_logger("platform");
    SPDLOG_LOGGER_INFO(logger, "Daemon mode not supported on Windows — running in foreground");
    return true;
}

void setup_signal_handler(asio::signal_set& signals,
                           std::function<void()> on_terminate) {
    signals.add(SIGINT);
    signals.add(SIGTERM);
    signals.async_wait([cb = std::move(on_terminate)](const std::error_code&, int) {
        if (cb) cb();
    });
}

} // namespace odc::platform

#endif // ODC_PLATFORM_WINDOWS
