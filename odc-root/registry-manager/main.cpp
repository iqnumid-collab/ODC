#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "registry_manager.hpp"
#include "odc/logging.hpp"

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
    CLI::App app{"odc-registry-manager — Service registry with WebSocket engine"};

    std::string config_path = "/etc/odc-root/registry-manager.yaml";
    bool daemon_mode = false;

    app.add_option("-c,--config", config_path,
                   "Path to YAML config file")
       ->capture_default_str();

    app.add_flag("-d,--daemon", daemon_mode,
                 "Run as daemon (fork to background)");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    odc::init_logging(spdlog::level::info, false);
    auto logger = odc::get_logger("main");

    SPDLOG_LOGGER_INFO(logger, "odc-registry-manager v{}", "0.1.0");

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

    odc::RegistryManager mgr(config_path);

    if (!mgr.initialize()) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to initialize registry manager");
        return 1;
    }

    if (!mgr.start()) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to start registry manager");
        return 1;
    }

    SPDLOG_LOGGER_INFO(logger, "Registry manager running — press Ctrl+C to stop");

    std::thread run_thread([&]() { mgr.run(); });

    while (!g_shutdown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    mgr.stop();
    if (run_thread.joinable()) run_thread.join();

    SPDLOG_LOGGER_INFO(logger, "Registry manager shut down");

    return 0;
}
