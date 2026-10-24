#include "odc/platform.hpp"

#ifdef ODC_PLATFORM_MACOS

#include "odc/logging.hpp"

#include <asio.hpp>
#include <csignal>
#include <unistd.h>

namespace odc::platform {

std::filesystem::path default_config_path() {
    return "/usr/local/etc/odc-root";
}

std::filesystem::path default_data_path() {
    return "/usr/local/var/lib/odc-root";
}

std::filesystem::path default_runtime_path() {
    return "/usr/local/var/run/odc-root";
}

bool daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        auto logger = get_logger("platform");
        SPDLOG_LOGGER_ERROR(logger, "fork() failed: {}", strerror(errno));
        return false;
    }
    if (pid > 0) {
        _exit(0);
    }

    if (setsid() < 0) {
        auto logger = get_logger("platform");
        SPDLOG_LOGGER_ERROR(logger, "setsid() failed: {}", strerror(errno));
        return false;
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

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

#endif // ODC_PLATFORM_MACOS
