#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "tunnel_manager.hpp"
#include "odc/logging.hpp"

#include <sys/capability.h>
#include <csignal>
#include <iostream>
#include <unistd.h>

namespace {

std::atomic<bool> g_shutdown{false};

void signal_handler(int /*sig*/) {
    g_shutdown = true;
}

} // namespace

int main(int argc, char* argv[]) {
    CLI::App app{"odc-tunnel-manager — VPN server management daemon"};

    std::string config_path = "/etc/odc-root/tunnel-manager.yaml";
    bool daemon_mode = false;
    bool check_privileges = false;

    app.add_option("-c,--config", config_path,
                   "Path to YAML config file")
       ->capture_default_str();

    app.add_flag("-d,--daemon", daemon_mode,
                 "Run as daemon (fork to background)");

    app.add_flag("--check-privileges", check_privileges,
                 "Verify CAP_NET_ADMIN capability");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    if (check_privileges) {
#ifdef __linux__
        cap_t caps = cap_get_proc();
        if (caps) {
            cap_flag_value_t net_admin, net_raw;
            cap_get_flag(caps, CAP_NET_ADMIN, CAP_EFFECTIVE, &net_admin);
            cap_get_flag(caps, CAP_NET_RAW, CAP_EFFECTIVE, &net_raw);
            cap_free(caps);

            if (net_admin == CAP_SET && net_raw == CAP_SET) {
                std::cout << "CAP_NET_ADMIN: yes\nCAP_NET_RAW: yes" << std::endl;
                return 0;
            }
        }
        std::cout << "CAP_NET_ADMIN: no\nCAP_NET_RAW: no" << std::endl;
        return 1;
#else
        std::cout << "Privilege check not supported on this platform" << std::endl;
        return 1;
#endif
    }

    odc::init_logging(spdlog::level::info, false);
    auto logger = odc::get_logger("main");

    SPDLOG_LOGGER_INFO(logger, "odc-tunnel-manager v{}", "0.1.0");

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to fork: {}", strerror(errno));
            return 1;
        }
        if (pid > 0) {
            std::cout << "Daemon started with PID " << pid << std::endl;
            return 0;
        }
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    odc::TunnelManager mgr(config_path);

    if (!mgr.initialize()) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to initialize tunnel manager");
        return 1;
    }

    if (!mgr.start()) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to start tunnel manager");
        return 1;
    }

    SPDLOG_LOGGER_INFO(logger, "Tunnel manager running — press Ctrl+C to stop");

    while (!g_shutdown) {
        mgr.run();
    }

    mgr.stop();
    SPDLOG_LOGGER_INFO(logger, "Tunnel manager shut down");

    return 0;
}
