#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "hook_service.hpp"
#include "odc/logging.hpp"
#include "odc/platform.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    CLI::App app{"odc-hook — mTLS WebSocket event hook client"};

    std::string config_path = odc::platform::default_config_path().string() +
                              "/odc-hook.yaml";
    bool daemon_mode = false;
    bool generate_cert = false;

    app.add_option("-c,--config", config_path, "Path to YAML config file")
       ->capture_default_str();
    app.add_flag("-d,--daemon", daemon_mode,
                 "Run as daemon (fork to background on Linux/macOS)");
    app.add_flag("--generate-cert", generate_cert,
                 "Generate mTLS client certificate and exit");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    odc::init_logging(spdlog::level::info, false);
    auto logger = odc::get_logger("main");

    SPDLOG_LOGGER_INFO(logger, "odc-hook v{}", "0.1.0");

    if (generate_cert) {
        SPDLOG_LOGGER_INFO(logger, "Generating mTLS client certificate...");
        odc::CertManager mgr;
        if (mgr.create_ca({"/tmp/odc-hook-ca.crt", "/tmp/odc-hook-ca.key"})) {
            auto cert = mgr.issue_client_cert("odc-hook-client", 2048, 365);
            if (!cert.cert_pem.empty()) {
                SPDLOG_LOGGER_INFO(logger, "Client cert generated: CN={}, fingerprint={}",
                                   cert.common_name, cert.fingerprint);
                SPDLOG_LOGGER_INFO(logger, "Cert PEM saved to /tmp/odc-hook-client.crt");
                SPDLOG_LOGGER_INFO(logger, "Key PEM saved to /tmp/odc-hook-client.key");
                std::ofstream cert_file("/tmp/odc-hook-client.crt");
                cert_file << cert.cert_pem;
                std::ofstream key_file("/tmp/odc-hook-client.key");
                key_file << cert.key_pem;
                return 0;
            }
        }
        SPDLOG_LOGGER_ERROR(logger, "Failed to generate client certificate");
        return 1;
    }

    if (daemon_mode) {
        if (!odc::platform::daemonize()) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to daemonize");
            return 1;
        }
    }

    odc::hook::HookService svc(config_path);

    if (!svc.initialize()) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to initialize hook service");
        return 1;
    }

    if (!svc.start()) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to start hook service");
        return 1;
    }

    SPDLOG_LOGGER_INFO(logger, "odc-hook running — press Ctrl+C to stop");

    svc.run();

    svc.stop();
    SPDLOG_LOGGER_INFO(logger, "odc-hook shut down");

    return 0;
}
