#pragma once

#include "core/event_bus.hpp"
#include "core/log_control.hpp"
#include "core/config_watcher.hpp"
#include "core/privilege_check.hpp"
#include "core/vpn_monitor.hpp"
#include "vpn/vpn_manager.hpp"
#include "mavlink/mav_router.hpp"
#include "mavlink/mav_config.hpp"
#include "mavlink/mav_ipc_handlers.hpp"
#include "mavlink/mav_extender_manager.hpp"
#include "thread_orchestrator/orchestrator.hpp"
#include "ipc/ipc_server.hpp"
#include "ipc/method_registry.hpp"
#include "qmi/qmi_modem_controller.hpp"
#include "rtsp/rtsp_compressor_manager.hpp"
#include "rtsp/compression_capabilities.hpp"
#include "graph/graph_runtime.hpp"
#include "ipc/graph_handlers.hpp"

#include <memory>
#include <atomic>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>

namespace odc {

struct AppContext {
    orch::ThreadOrchestrator* orchestrator{nullptr};
    EventBus* event_bus{nullptr};
    LogControl* log_control{nullptr};
    VpnManager* vpn_manager{nullptr};
    VpnInterfaceMonitor* vpn_monitor{nullptr};
    MavRouter* mav_router{nullptr};
    MavExtenderManager* mav_extender_mgr{nullptr};
    std::vector<MavRouter*> mav_extenders;
    ipc::MethodRegistry* method_registry{nullptr};
    qmi::QmiModemController* qmi_controller{nullptr};
    RtspCompressorManager* rtsp_compressor{nullptr};
    graph::GraphRuntime* graph_runtime{nullptr};
};

class AppLifecycle {
public:
    AppLifecycle() = default;
    ~AppLifecycle() { shutdown(); }

    AppLifecycle(const AppLifecycle&) = delete;
    AppLifecycle& operator=(const AppLifecycle&) = delete;

    bool startup(const std::filesystem::path& config_path = "/etc/odc-onboard/system.yaml") {
        orchestrator_ = std::make_unique<orch::ThreadOrchestrator>(
            orch::Config{.initial_threads = 4, .min_threads = 2, .max_threads = 8}
        );

        event_bus_ = std::make_unique<EventBus>(*orchestrator_);
        log_control_ = std::make_unique<LogControl>(*event_bus_);
        log_control_->init();

        vpn_monitor_ = std::make_unique<VpnInterfaceMonitor>(*event_bus_);
        vpn_monitor_->start();

        compression_caps_ = std::make_shared<CompressionCapabilities>();

        graph_mode_ = is_graph_config(config_path);

        if (graph_mode_) {
            graph_runtime_ = std::make_unique<graph::GraphRuntime>(
                *event_bus_, *orchestrator_, vpn_monitor_.get(), compression_caps_);
            if (!graph_runtime_->load(config_path)) {
                auto logger = log_control_->get_logger("lifecycle");
                SPDLOG_LOGGER_ERROR(logger, "Failed to load graph config: {}", config_path.string());
                return false;
            }
        } else {
            vpn_manager_ = std::make_unique<VpnManager>(*event_bus_, config_path, vpn_monitor_.get());
            vpn_manager_->load_config();

            auto cfg = load_app_config(config_path);

            nlohmann::json raw_config;
            try {
                std::ifstream ifs(config_path);
                ifs >> raw_config;
            } catch (...) {
            }

            if (!raw_config.empty()) {
                auto mav_cfg = parse_mavlink_config(raw_config);
                if (mav_cfg && mav_cfg->enabled) {
                    mav_router_ = std::make_unique<MavRouter>(*mav_cfg, *event_bus_);

                    if (mav_cfg->extender_auto.enabled) {
                        mav_extender_mgr_ = std::make_unique<MavExtenderManager>(
                            *event_bus_, *orchestrator_, *mav_cfg);
                    }

                    for (const auto& ext_cfg : mav_cfg->extenders) {
                        if (ext_cfg.id.empty()) continue;

                        MavlinkConfig ext_mav_cfg;
                        ext_mav_cfg.enabled = true;
                        ext_mav_cfg.tcp_server_port = 0;
                        ext_mav_cfg.dedup_period_sec = mav_cfg->dedup_period_sec;
                        ext_mav_cfg.sniffer_sysid = mav_cfg->sniffer_sysid;
                        ext_mav_cfg.report_stats = mav_cfg->report_stats;

                        if (!ext_cfg.upstream.type.empty()) {
                            MavEndpointConfig upstream;
                            upstream = ext_cfg.upstream;
                            ext_mav_cfg.endpoints.push_back(std::move(upstream));
                        }

                        for (const auto& ep : ext_cfg.endpoints) {
                            ext_mav_cfg.endpoints.push_back(ep);
                        }

                        auto ext_router = std::make_unique<MavRouter>(ext_mav_cfg, *event_bus_, ext_cfg.id);
                        mav_extenders_.push_back(std::move(ext_router));
                    }
                }
            }

            if (!raw_config.empty() && raw_config.contains("qmi")) {
                auto& qmi_cfg_json = raw_config["qmi"];
                qmi::QmiConfig qmi_cfg;
                if (qmi_cfg_json.contains("device_path"))
                    qmi_cfg.device_path = qmi_cfg_json["device_path"].get<std::string>();
                if (qmi_cfg_json.contains("iface_name"))
                    qmi_cfg.iface_name = qmi_cfg_json["iface_name"].get<std::string>();
                if (qmi_cfg_json.contains("apn"))
                    qmi_cfg.apn = qmi_cfg_json["apn"].get<std::string>();
                if (qmi_cfg_json.contains("ip_type"))
                    qmi_cfg.ip_type = qmi_cfg_json["ip_type"].get<int>();
                if (qmi_cfg_json.contains("auth_type"))
                    qmi_cfg.auth_type = qmi_cfg_json["auth_type"].get<std::string>();
                if (qmi_cfg_json.contains("username"))
                    qmi_cfg.username = qmi_cfg_json["username"].get<std::string>();
                if (qmi_cfg_json.contains("password"))
                    qmi_cfg.password = qmi_cfg_json["password"].get<std::string>();
                if (qmi_cfg_json.contains("pin"))
                    qmi_cfg.pin = qmi_cfg_json["pin"].get<std::string>();
                if (qmi_cfg_json.contains("auto_connect"))
                    qmi_cfg.auto_connect = qmi_cfg_json["auto_connect"].get<bool>();
                if (qmi_cfg_json.contains("allow_roaming"))
                    qmi_cfg.allow_roaming = qmi_cfg_json["allow_roaming"].get<bool>();
                if (qmi_cfg_json.contains("open_timeout_ms"))
                    qmi_cfg.open_timeout_ms = qmi_cfg_json["open_timeout_ms"].get<uint32_t>();
                if (qmi_cfg_json.contains("init_timeout_ms"))
                    qmi_cfg.init_timeout_ms = qmi_cfg_json["init_timeout_ms"].get<uint32_t>();
                if (qmi_cfg_json.contains("sim_timeout_ms"))
                    qmi_cfg.sim_timeout_ms = qmi_cfg_json["sim_timeout_ms"].get<uint32_t>();
                if (qmi_cfg_json.contains("reg_timeout_ms"))
                    qmi_cfg.reg_timeout_ms = qmi_cfg_json["reg_timeout_ms"].get<uint32_t>();
                if (qmi_cfg_json.contains("data_timeout_ms"))
                    qmi_cfg.data_timeout_ms = qmi_cfg_json["data_timeout_ms"].get<uint32_t>();

                const bool auto_connect = qmi_cfg.auto_connect;
                qmi_controller_ = std::make_unique<qmi::QmiModemController>(std::move(qmi_cfg));
                qmi_controller_->start(event_bus_.get());
                if (auto_connect) {
                    qmi_controller_->request_connect();
                }
            }

            if (!raw_config.empty() && raw_config.contains("rtsp")) {
                auto& rtsp_json = raw_config["rtsp"];
                if (compression_caps_) {
                    compression_caps_->ensure(true);
                }
                rtsp_compressor_ = std::make_unique<RtspCompressorManager>(
                    *event_bus_, *orchestrator_, rtsp_json);
            }
        }

        registry_ = std::make_unique<ipc::MethodRegistry>();
        register_methods();

        context_.orchestrator = orchestrator_.get();
        context_.event_bus = event_bus_.get();
        context_.log_control = log_control_.get();
        context_.vpn_manager = vpn_manager_.get();
        context_.vpn_monitor = vpn_monitor_.get();
        context_.mav_router = mav_router_.get();
        context_.mav_extender_mgr = mav_extender_mgr_.get();
        for (const auto& ext : mav_extenders_) {
            context_.mav_extenders.push_back(ext.get());
        }
        context_.method_registry = registry_.get();
        context_.qmi_controller = qmi_controller_.get();
        context_.rtsp_compressor = rtsp_compressor_.get();
        context_.graph_runtime = graph_runtime_.get();

        config_path_ = config_path;
        if (!graph_mode_) {
            start_config_watcher();
        }

        auto logger = log_control_->get_logger("lifecycle");
        SPDLOG_LOGGER_INFO(logger, "Application started ({})", graph_mode_ ? "graph" : "legacy");

        return true;
    }

    void start_ipc_server(const std::string& socket_path = "/run/odc-onboard/odc-onboard.sock") {
        ipc_server_ = std::make_unique<ipc::IpcServer>(
            socket_path, *registry_, *event_bus_, *vpn_manager_, *log_control_, *orchestrator_);

        if (!ipc_server_->start()) {
            auto logger = log_control_->get_logger("lifecycle");
            SPDLOG_LOGGER_ERROR(logger, "Failed to start IPC server on {}", socket_path);
            return;
        }

        auto logger = log_control_->get_logger("lifecycle");
        SPDLOG_LOGGER_INFO(logger, "IPC server started on {}", socket_path);

        orch::CancellationSource cancel_src;
        ipc_server_cancel_ = std::make_unique<orch::CancellationSource>();
        auto local_token = ipc_server_cancel_->token();

        orchestrator_->submit_simple([this, local_token]() mutable {
            if (ipc_server_) {
                ipc_server_->run(local_token);
            }
        });

        if (mav_router_) {
            mav_cancel_ = std::make_unique<orch::CancellationSource>();
            auto mav_token = mav_cancel_->token();
            orchestrator_->submit_simple([this, mav_token]() mutable {
                if (mav_router_) {
                    mav_router_->run(mav_token);
                }
            });
        }

        for (auto& ext : mav_extenders_) {
            auto ext_cancel = std::make_unique<orch::CancellationSource>();
            auto ext_token = ext_cancel->token();
            auto* router = ext.get();
            ext_cancels_.push_back(std::move(ext_cancel));
            orchestrator_->submit_simple([router, ext_token]() mutable {
                if (router) {
                    router->run(ext_token);
                }
            });
        }

        if (mav_extender_mgr_) {
            mav_extender_mgr_->start();
        }

        if (rtsp_compressor_) {
            rtsp_compressor_->start();
        }

        if (graph_runtime_) {
            if (!graph_runtime_->start()) {
                auto logger = log_control_->get_logger("lifecycle");
                SPDLOG_LOGGER_ERROR(logger, "Failed to start graph runtime");
                return;
            }
            graph_tick_cancel_ = std::make_unique<orch::CancellationSource>();
            auto tick_token = graph_tick_cancel_->token();
            orchestrator_->submit_simple([this, tick_token]() mutable {
                while (!tick_token.is_cancelled()) {
                    if (graph_runtime_) {
                        graph_runtime_->tick();
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
        }
    }

    void shutdown() {
        if (graph_runtime_) {
            graph_runtime_->stop();
            graph_runtime_.reset();
        }

        if (graph_tick_cancel_) {
            graph_tick_cancel_->cancel();
            graph_tick_cancel_.reset();
        }

        if (qmi_controller_) {
            qmi_controller_->stop();
            qmi_controller_.reset();
        }

        if (mav_extender_mgr_) {
            mav_extender_mgr_->stop_all();
        }

        if (rtsp_compressor_) {
            rtsp_compressor_->stop_all();
        }

        for (auto& cancel : ext_cancels_) {
            if (cancel) {
                cancel->cancel();
            }
        }
        ext_cancels_.clear();

        if (mav_cancel_) {
            mav_cancel_->cancel();
        }

        if (ipc_server_cancel_) {
            ipc_server_cancel_->cancel();
        }

        if (ipc_server_) {
            ipc_server_->stop();
            ipc_server_.reset();
        }

        if (vpn_manager_) {
            auto logger = context_.log_control ? context_.log_control->get_logger("lifecycle") : nullptr;
            if (logger) {
                SPDLOG_LOGGER_INFO(logger, "Starting graceful shutdown with {}s drain timeout", drain_timeout_sec_);
            }

            auto drain_start = std::chrono::steady_clock::now();

            vpn_manager_->stop_all();

            while (std::chrono::steady_clock::now() - drain_start < std::chrono::seconds(drain_timeout_sec_)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        stop_config_watcher();

        if (context_.log_control) {
            auto logger = context_.log_control->get_logger("lifecycle");
            SPDLOG_LOGGER_INFO(logger, "Shutting down...");
        }

        (void)orchestrator_.release();
        (void)vpn_manager_.release();
        (void)event_bus_.release();
        (void)registry_.release();
    }

    void set_drain_timeout(uint32_t seconds) { drain_timeout_sec_ = seconds; }
    uint32_t drain_timeout_sec() const { return drain_timeout_sec_; }

    const AppContext& context() const { return context_; }
    AppContext& context() { return context_; }

    orch::ThreadOrchestrator& orchestrator() const { return *orchestrator_; }
    EventBus& event_bus() const { return *event_bus_; }
    LogControl& log_control() const { return *log_control_; }
    VpnManager& vpn_manager() const {
        if (!vpn_manager_) {
            throw std::runtime_error("VpnManager not available in graph mode");
        }
        return *vpn_manager_;
    }
    VpnInterfaceMonitor* vpn_monitor() const { return vpn_monitor_.get(); }
    MavRouter* mav_router() const { return mav_router_.get(); }
    MavExtenderManager* mav_extender_manager() const { return mav_extender_mgr_.get(); }
    RtspCompressorManager* rtsp_compressor() const { return rtsp_compressor_.get(); }
    graph::GraphRuntime* graph_runtime() const { return graph_runtime_.get(); }
    bool graph_mode() const { return graph_mode_; }
    const std::vector<MavRouter*> mav_extenders() const {
        std::vector<MavRouter*> result;
        for (const auto& ext : mav_extenders_) {
            result.push_back(ext.get());
        }
        return result;
    }
    ipc::MethodRegistry& method_registry() const { return *registry_; }

private:
    std::unique_ptr<orch::ThreadOrchestrator> orchestrator_;
    std::unique_ptr<EventBus> event_bus_;
    std::unique_ptr<LogControl> log_control_;
    std::unique_ptr<VpnInterfaceMonitor> vpn_monitor_;
    std::unique_ptr<VpnManager> vpn_manager_;
    std::unique_ptr<MavRouter> mav_router_;
    std::unique_ptr<MavExtenderManager> mav_extender_mgr_;
    std::vector<std::unique_ptr<MavRouter>> mav_extenders_;
    std::unique_ptr<ipc::MethodRegistry> registry_;
    std::unique_ptr<ipc::IpcServer> ipc_server_;
    std::unique_ptr<orch::CancellationSource> ipc_server_cancel_;
    std::unique_ptr<orch::CancellationSource> mav_cancel_;
    std::vector<std::unique_ptr<orch::CancellationSource>> ext_cancels_;
    std::unique_ptr<orch::CancellationSource> qmi_cancel_;
    std::unique_ptr<qmi::QmiModemController> qmi_controller_;
    std::unique_ptr<RtspCompressorManager> rtsp_compressor_;
    std::shared_ptr<CompressionCapabilities> compression_caps_;
    std::unique_ptr<graph::GraphRuntime> graph_runtime_;
    std::unique_ptr<orch::CancellationSource> graph_tick_cancel_;
    bool graph_mode_{false};
    AppContext context_;
    std::filesystem::path config_path_;
    ConfigWatcher config_watcher_;
    uint32_t drain_timeout_sec_{10};

    void register_methods() {
        if (graph_runtime_) {
            graph::register_graph_methods(*registry_, *graph_runtime_);

            // Bridge legacy mavlink.* IPC onto the graph primary router.
            registry_->register_method("mavlink.status", [this](const nlohmann::json&) {
                auto* router = graph_runtime_->primary_mav_router();
                if (!router) {
                    throw std::runtime_error("No mavlink.router component running");
                }
                auto s = router->get_status();
                return nlohmann::json{
                    {"running", s.running},
                    {"tcp_server_port", s.tcp_server_port},
                    {"dedup_enabled", s.dedup_enabled},
                    {"sniffer_sysid", s.sniffer_sysid},
                    {"endpoint_count", s.endpoint_count},
                    {"connected_count", s.connected_count},
                };
            });

            registry_->register_method("mavlink.endpoints", [this](const nlohmann::json&) {
                auto* router = graph_runtime_->primary_mav_router();
                if (!router) {
                    throw std::runtime_error("No mavlink.router component running");
                }
                auto eps = router->get_endpoints();
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& ep : eps) {
                    arr.push_back({
                        {"name", ep.name},
                        {"type", ep.type},
                        {"device_or_address", ep.device_or_address},
                        {"port", ep.port},
                        {"connected", ep.connected},
                        {"messages_in", ep.messages_in},
                        {"messages_out", ep.messages_out},
                        {"messages_lost", ep.messages_lost},
                        {"bytes_in", ep.bytes_in},
                        {"bytes_out", ep.bytes_out},
                    });
                }
                return arr;
            });

            registry_->register_method("mavlink.stats", [this](const nlohmann::json&) {
                auto* router = graph_runtime_->primary_mav_router();
                if (!router) {
                    throw std::runtime_error("No mavlink.router component running");
                }
                auto s = router->get_stats();
                return nlohmann::json{
                    {"total_routed", s.total_routed},
                    {"total_filtered", s.total_filtered},
                    {"total_dedup_dropped", s.total_dedup_dropped},
                    {"total_errors", s.total_errors},
                };
            });
        }

        registry_->register_method("metrics", [this](const nlohmann::json& params) {
            (void)params;
            auto m = orchestrator_->get_metrics();
            nlohmann::json data;
            data["active_tasks"] = m.active_tasks;
            data["pending_tasks"] = m.pending_tasks;
            data["completed_tasks"] = m.completed_tasks;
            data["worker_count"] = m.worker_count;
            return data;
        });

        registry_->register_method("log.level", [this](const nlohmann::json& params) {
            std::string level = params.value("level", "");
            LogLevel lvl;
            std::string l = level;
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);

            if (l == "trace") lvl = LogLevel::Trace;
            else if (l == "debug") lvl = LogLevel::Debug;
            else if (l == "info") lvl = LogLevel::Info;
            else if (l == "warn") lvl = LogLevel::Warn;
            else if (l == "error") lvl = LogLevel::Error;
            else throw std::runtime_error("Invalid log level: " + level);

            log_control_->set_default_level(lvl);
            return nlohmann::json{{"level", to_string_view(lvl)}};
        });

        registry_->register_method("privileges.check", [this](const nlohmann::json& params) {
            (void)params;
            auto result = odc::check_privileges();
            nlohmann::json data;
            data["is_root"] = result.is_root;
            data["has_net_admin"] = result.has_net_admin;
            data["missing_capabilities"] = result.missing_capabilities;
            if (!result.error_message.empty()) data["error"] = result.error_message;
            return data;
        });

        if (!graph_mode_) {
        registry_->register_method("vpn.list", [this](const nlohmann::json& params) {
            (void)params;
            auto connections = vpn_manager_->list_connections();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& conn : connections) {
                nlohmann::json c;
                c["id"] = conn.id;
                c["type"] = to_string_view(conn.type);
                c["state"] = to_string_view(conn.state);
                c["enabled"] = conn.enabled;
                c["name"] = conn.name;
                c["config_path"] = conn.config_path;
                c["stats"] = {
                    {"bytes_in", conn.stats.bytes_in},
                    {"bytes_out", conn.stats.bytes_out},
                    {"packets_in", conn.stats.packets_in},
                    {"packets_out", conn.stats.packets_out},
                    {"duration_sec", conn.stats.duration_sec},
                    {"reconnect_count", conn.stats.reconnect_count}
                };
                c["health"] = {
                    {"healthy", conn.health.healthy},
                    {"latency_ms", conn.health.latency_ms},
                    {"consecutive_failures", conn.health.consecutive_failures}
                };
                arr.push_back(c);
            }
            return arr;
        });

        registry_->register_method("vpn.status", [this](const nlohmann::json& params) {
            std::string id = params.value("id", "");
            auto info = vpn_manager_->get_connection(id);
            if (!info) {
                throw std::runtime_error("Connection not found: " + id);
            }
            nlohmann::json data;
            data["id"] = info->id;
            data["type"] = to_string_view(info->type);
            data["state"] = to_string_view(info->state);
            data["enabled"] = info->enabled;
            data["name"] = info->name;
            data["config_path"] = info->config_path;
            data["stats"] = {
                {"bytes_in", info->stats.bytes_in},
                {"bytes_out", info->stats.bytes_out},
                {"packets_in", info->stats.packets_in},
                {"packets_out", info->stats.packets_out},
                {"duration_sec", info->stats.duration_sec},
                {"reconnect_count", info->stats.reconnect_count},
                {"last_error", info->stats.last_error}
            };
            data["health"] = {
                {"healthy", info->health.healthy},
                {"latency_ms", info->health.latency_ms},
                {"consecutive_failures", info->health.consecutive_failures}
            };
            return data;
        });

        registry_->register_method("vpn.start", [this](const nlohmann::json& params) {
            std::string id = params.value("id", "");
            bool ok = vpn_manager_->start_connection(id);
            return nlohmann::json{{"success", ok}, {"message", ok ? "Starting connection: " + id : "Failed to start connection: " + id}};
        });

        registry_->register_method("vpn.stop", [this](const nlohmann::json& params) {
            std::string id = params.value("id", "");
            if (mav_extender_mgr_) {
                mav_extender_mgr_->stop_extender(id);
            }
            bool ok = vpn_manager_->stop_connection(id);
            return nlohmann::json{{"success", ok}, {"message", ok ? "Stopping connection: " + id : "Failed to stop connection: " + id}};
        });

        registry_->register_method("vpn.restart", [this](const nlohmann::json& params) {
            std::string id = params.value("id", "");
            bool ok = vpn_manager_->restart_connection(id);
            return nlohmann::json{{"success", ok}, {"message", ok ? "Restarting connection: " + id : "Failed to restart connection: " + id}};
        });

        registry_->register_method("vpn.remove", [this](const nlohmann::json& params) {
            std::string id = params.value("id", "");
            if (mav_extender_mgr_) {
                mav_extender_mgr_->remove_extender(id);
            }
            bool ok = vpn_manager_->remove_connection(id);
            return nlohmann::json{{"success", ok}, {"message", ok ? "Removed connection: " + id : "Failed to remove connection: " + id}};
        });

        registry_->register_method("vpn.add", [this](const nlohmann::json& params) {
            std::string type = params.value("type", "");
            std::string id = params.value("id", "");
            std::string config_path = params.value("config_path", "");
            std::string name = params.value("name", "");
            bool auto_start = params.value("auto_start", false);

            if (type == "wireguard") {
                WireGuardConnectionConfig cfg;
                cfg.id = id;
                cfg.config_path = config_path;
                cfg.name = name;
                cfg.auto_start = auto_start;
                bool ok = vpn_manager_->add_wireguard(cfg);
                return nlohmann::json{{"success", ok}, {"message", ok ? "Added WireGuard connection: " + id : "Failed to add WireGuard connection: " + id}};
            } else if (type == "openvpn") {
                OpenVPNConnectionConfig cfg;
                cfg.id = id;
                cfg.config_path = config_path;
                cfg.credentials_path = params.value("creds_path", "");
                cfg.name = name;
                cfg.auto_start = auto_start;
                bool ok = vpn_manager_->add_openvpn(cfg);
                return nlohmann::json{{"success", ok}, {"message", ok ? "Added OpenVPN connection: " + id : "Failed to add OpenVPN connection: " + id}};
            }
            throw std::runtime_error("Unknown VPN type: " + type);
        });

        registry_->register_method("vpn.config.validate", [this](const nlohmann::json& params) {
            std::string path = params.value("path", "");
            std::filesystem::path config_path(path);
            bool valid = true;
            std::vector<std::string> errors;
            std::vector<std::string> warnings;

            if (!std::filesystem::exists(config_path)) {
                errors.push_back("File does not exist: " + path);
                valid = false;
            } else {
                try {
                    std::ifstream ifs(config_path);
                    std::string content((std::istreambuf_iterator<char>(ifs)),
                                        std::istreambuf_iterator<char>());

                    if (content.empty()) {
                        errors.push_back("Config file is empty");
                        valid = false;
                    }

                    bool is_wg = content.find("[Interface]") != std::string::npos;
                    bool is_ovpn = content.find("dev ") != std::string::npos ||
                                   content.find("remote ") != std::string::npos;

                    if (!is_wg && !is_ovpn) {
                        warnings.push_back("Config file does not appear to be WireGuard or OpenVPN format");
                    }

                    if (is_wg) {
                        if (content.find("[Interface]") == std::string::npos) {
                            errors.push_back("Missing [Interface] section");
                            valid = false;
                        }
                        if (content.find("[Peer]") == std::string::npos) {
                            errors.push_back("Missing [Peer] section");
                            valid = false;
                        }
                        if (content.find("PrivateKey") == std::string::npos) {
                            errors.push_back("Missing PrivateKey in [Interface]");
                            valid = false;
                        }
                    }

                    auto perm_result = odc::check_config_file_permissions(config_path);
                    if (!perm_result.valid) {
                        warnings.push_back("Permission issue: " + perm_result.error_message);
                    }
                } catch (const std::exception& e) {
                    errors.push_back(std::string("Error reading file: ") + e.what());
                    valid = false;
                }
            }

            nlohmann::json data;
            data["valid"] = valid;
            data["path"] = path;
            data["errors"] = errors;
            data["warnings"] = warnings;
            return data;
        });

        register_qmi_methods();

        if (mav_router_) {
            register_mavlink_methods(*registry_, *mav_router_);
        }

        if (mav_extender_mgr_) {
            registry_->register_method("mav.extender.list", [this](const nlohmann::json& params) {
                (void)params;
                auto extenders = mav_extender_mgr_->list_extenders();
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& ext : extenders) {
                    nlohmann::json j;
                    j["extender_id"] = ext.extender_id;
                    j["vpn_connection_id"] = ext.vpn_connection_id;
                    j["vpn_interface"] = ext.vpn_interface;
                    j["target_ip"] = ext.target_ip;
                    j["target_port"] = ext.target_port;
                    j["active"] = ext.active;
                    j["messages_in"] = ext.messages_in;
                    j["messages_out"] = ext.messages_out;
                    arr.push_back(j);
                }
                return arr;
            });

            registry_->register_method("mav.extender.status", [this](const nlohmann::json& params) {
                std::string conn_id = params.value("connection_id", "");
                auto extenders = mav_extender_mgr_->list_extenders();
                for (const auto& ext : extenders) {
                    if (ext.vpn_connection_id == conn_id) {
                        nlohmann::json j;
                        j["extender_id"] = ext.extender_id;
                        j["vpn_connection_id"] = ext.vpn_connection_id;
                        j["vpn_interface"] = ext.vpn_interface;
                        j["target_ip"] = ext.target_ip;
                        j["target_port"] = ext.target_port;
                        j["active"] = ext.active;
                        j["messages_in"] = ext.messages_in;
                        j["messages_out"] = ext.messages_out;
                        return j;
                    }
                }
                throw std::runtime_error("No extender found for connection: " + conn_id);
            });

            registry_->register_method("mav.extender.start", [this](const nlohmann::json& params) {
                std::string conn_id = params.value("connection_id", "");
                std::string iface = params.value("interface", "");
                std::string ip = params.value("ip", "");
                bool ok = mav_extender_mgr_->start_extender(conn_id, iface, ip);
                return nlohmann::json{{"success", ok}, {"message", ok ? "Started extender: " + conn_id : "Failed"}};
            });

            registry_->register_method("mav.extender.stop", [this](const nlohmann::json& params) {
                std::string conn_id = params.value("connection_id", "");
                bool ok = mav_extender_mgr_->stop_extender(conn_id);
                return nlohmann::json{{"success", ok}, {"message", ok ? "Stopped extender: " + conn_id : "Failed"}};
            });

            registry_->register_method("mav.extender.remove", [this](const nlohmann::json& params) {
                std::string conn_id = params.value("connection_id", "");
                bool ok = mav_extender_mgr_->remove_extender(conn_id);
                return nlohmann::json{{"success", ok}, {"message", ok ? "Removed extender: " + conn_id : "Failed"}};
            });

            registry_->register_method("mav.extender.auto", [this](const nlohmann::json& params) {
                std::string mode = params.value("mode", "");
                if (mode == "on") {
                    mav_extender_mgr_->set_auto_enabled(true);
                    return nlohmann::json{{"success", true}, {"message", "Auto extender spawning enabled"}, {"enabled", true}};
                } else if (mode == "off") {
                    mav_extender_mgr_->set_auto_enabled(false);
                    return nlohmann::json{{"success", true}, {"message", "Auto extender spawning disabled"}, {"enabled", false}};
                }
                throw std::runtime_error("Invalid mode: " + mode);
            });

            registry_->register_method("mav.extender.port", [this](const nlohmann::json& params) {
                uint16_t port = params.value("port", 0);
                mav_extender_mgr_->set_udp_port(port);
                return nlohmann::json{{"message", "Extender UDP port set to: " + std::to_string(port)}, {"port", port}};
            });
        }

        for (auto& ext : mav_extenders_) {
            register_mavlink_methods(*registry_, *ext);
        }

        if (rtsp_compressor_) {
            registry_->register_method("rtsp.status", [this](const nlohmann::json&) {
                auto compressors = rtsp_compressor_->list_compressors();
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& c : compressors) {
                    nlohmann::json j;
                    j["connection_id"] = c.connection_id;
                    j["interface_name"] = c.interface_name;
                    j["target_ip"] = c.target_ip;
                    j["output_port"] = c.output_port;
                    j["running"] = c.running;
                    arr.push_back(j);
                }
                return arr;
            });

            registry_->register_method("rtsp.start", [this](const nlohmann::json& params) {
                std::string conn_id = params.value("connection_id", "");
                std::string iface = params.value("interface", "");
                std::string ip = params.value("ip", "");
                bool ok = rtsp_compressor_->start_compressor(conn_id, iface, ip);
                return nlohmann::json{{"success", ok}, {"message", ok ? "Started compressor: " + conn_id : "Failed"}};
            });

            registry_->register_method("rtsp.stop", [this](const nlohmann::json& params) {
                std::string conn_id = params.value("connection_id", "");
                bool ok = rtsp_compressor_->stop_compressor(conn_id);
                return nlohmann::json{{"success", ok}, {"message", ok ? "Stopped compressor: " + conn_id : "Failed"}};
            });
        }
        } // !graph_mode_
    }

    enum class ConfigFormat { GraphYaml, LegacyJson, Unknown };

    static ConfigFormat detect_config_format(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            return ConfigFormat::Unknown;
        }

        const auto ext = path.extension().string();
        if (ext != ".yaml" && ext != ".yml") {
            return ConfigFormat::LegacyJson;
        }

        std::ifstream ifs(path);
        if (!ifs) return ConfigFormat::Unknown;

        std::string line;
        for (int i = 0; i < 5 && std::getline(ifs, line); ++i) {
            auto pos = line.find_first_not_of(" \t\r\n");
            if (pos == std::string::npos) continue;
            if (line[pos] == '#') continue;
            if (line.find("version:") != std::string::npos) {
                return ConfigFormat::GraphYaml;
            }
        }
        return ConfigFormat::LegacyJson;
    }

    static bool is_graph_config(const std::filesystem::path& path) {
        return detect_config_format(path) == ConfigFormat::GraphYaml;
    }

    void register_qmi_methods() {
        if (!qmi_controller_) return;

        registry_->register_method("qmi.status", [this](const nlohmann::json&) {
            auto state = qmi_controller_->current_qmi_state();
            nlohmann::json data;
            data["device"] = qmi_controller_->cfg.device_path;
            data["iface"] = qmi_controller_->cfg.iface_name;
            data["state"] = qmi::to_string_view(state);
            return data;
        });

        registry_->register_method("qmi.connect", [this](const nlohmann::json&) {
            bool ok = qmi_controller_->connect();
            return nlohmann::json{{"success", ok}, {"message", ok ? "QMI connect initiated" : "QMI connect failed"}};
        });

        registry_->register_method("qmi.disconnect", [this](const nlohmann::json&) {
            bool ok = qmi_controller_->disconnect();
            return nlohmann::json{{"success", ok}, {"message", ok ? "QMI disconnect initiated" : "QMI disconnect failed"}};
        });

        registry_->register_method("qmi.telemetry", [this](const nlohmann::json&) {
            auto telem = qmi_controller_->get_telemetry();
            nlohmann::json data;
            data["device"] = qmi_controller_->cfg.device_path;
            data["rssi_dbm"]   = telem.rssi_dbm;
            data["lte_rsrp"]   = telem.lte_rsrp;
            data["lte_snr"]    = telem.lte_snr;
            data["operator"]   = telem.operator_name;
            data["plmn"]       = std::to_string(telem.plmn_mcc) + "-" + std::to_string(telem.plmn_mnc);
            data["reg_state"]  = telem.registration_state;
            data["roaming"]    = telem.roaming;
            data["conn_status"] = telem.connection_status;
            data["internet"]   = telem.internet_reachable;
            data["ping_ms"]    = telem.ping_latency_ms;
            data["ip"]         = telem.ip_settings.ipv4_addr;
            data["gateway"]    = telem.ip_settings.gateway;
            data["dns1"]       = telem.ip_settings.dns1;
            data["dns2"]       = telem.ip_settings.dns2;
            data["mtu"]        = telem.ip_settings.mtu;
            return data;
        });
    }

    void start_config_watcher() {
        config_watcher_.start(config_path_, [this](const std::filesystem::path& path) {
            handle_config_reload(path);
        });
    }

    void stop_config_watcher() {
        config_watcher_.stop();
    }

    void handle_config_reload(const std::filesystem::path& path) {
        auto logger = log_control_->get_logger("lifecycle");
        SPDLOG_LOGGER_INFO(logger, "Config file changed, reloading: {}", path.string());

        auto cfg = load_app_config(path);
        if (!cfg) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to reload config from {}", path.string());
            return;
        }

        auto old_config = vpn_manager_->config();
        if (old_config && cfg->version <= old_config->version) {
            SPDLOG_LOGGER_INFO(logger, "Config version not incremented, skipping reload");
            return;
        }

        if (vpn_manager_->load_config()) {
            SPDLOG_LOGGER_INFO(logger, "Config reloaded successfully");
        } else {
            SPDLOG_LOGGER_ERROR(logger, "Failed to apply reloaded config");
        }
    }
};

}
