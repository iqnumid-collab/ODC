#include "HardwareInfo.h"
#include "RtspAnalyzer.h"
#include "RtspBridge.h"

#include <thread_orchestrator/thread_orchestrator.hpp>
#include <CLI/CLI11.hpp>

#include <iostream>
#include <string>
#include <csignal>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <memory>

static std::atomic<bool> g_quit{false};
static GMainLoop* g_main_loop = nullptr;

static void signalHandler(int /*sig*/) {
    g_quit = true;
    if (g_main_loop)
        g_main_loop_quit(g_main_loop);
}

// ---------------------------------------------------------------------------
// Orchestrator factory (shared between modes)
// ---------------------------------------------------------------------------
static orch::ThreadOrchestrator makeOrchestrator() {
    orch::Config cfg;
    cfg.initial_threads = 4;
    cfg.min_threads = 2;
    cfg.max_threads = std::thread::hardware_concurrency() * 2;
    if (cfg.max_threads < 4) cfg.max_threads = 8;
    cfg.on_error = [](std::exception_ptr ep) {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) {
            std::cerr << "[Orchestrator] Task error: " << e.what() << "\n";
        }
    };
    return orch::ThreadOrchestrator(cfg);
}

// ---------------------------------------------------------------------------
// Analyse mode
// ---------------------------------------------------------------------------
static int runAnalyse(const std::string& uri, bool use_tcp, bool view, int duration_sec) {
    auto pool = makeOrchestrator();

    // HW detection via orchestrator
    auto hw_future = pool.submit_simple([]{ return detectHardware(); });
    HardwareInfo hw_info = hw_future.get();
    printHardwareInfo(hw_info);

    // Build & start analyser via orchestrator
    auto analyzer = std::make_shared<RtspAnalyzer>();

    analyzer->onBandwidthUpdate([](const BandwidthStats& stats) {
        std::cout << std::fixed << std::setprecision(1)
                  << "[BW] current: " << stats.current_kbps << " kbps"
                  << "  avg: " << stats.average_kbps << " kbps"
                  << "  peak: " << stats.peak_kbps << " kbps"
                  << "  total: " << (stats.total_bytes / 1024) << " KB"
                  << "  frames: " << stats.frame_count
                  << "  elapsed: " << stats.elapsed_seconds << " s"
                  << "\r" << std::flush;
    });

    std::cout << "Connecting to: " << uri << "\n"
              << "Transport    : " << (use_tcp ? "TCP" : "UDP (default)") << "\n"
              << "Video Window : " << (view ? "enabled" : "disabled") << "\n"
              << "Duration     : " << (duration_sec > 0 ? std::to_string(duration_sec) + " seconds" : "until Ctrl-C") << "\n"
              << std::string(40, '-') << "\n";

    // Start the pipeline on the orchestrator so the calling thread stays free
    auto start_ok = pool.submit_simple([&]{ return analyzer->start(uri, use_tcp, view); });
    if (!start_ok.get()) {
        std::cerr << "Failed to start RTSP analyzer.\n";
        return EXIT_FAILURE;
    }

    // GLib main-context pump as an orchestrator task
    auto pump_future = pool.submit([&analyzer, duration_sec](orch::CancellationToken token) {
        GMainContext* ctx = g_main_context_default();
        auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::seconds(duration_sec > 0 ? duration_sec : 3600 * 24);

        while (analyzer->isRunning() && !token.is_cancelled() && !g_quit) {
            g_main_context_iteration(ctx, FALSE);

            if (duration_sec > 0 && std::chrono::steady_clock::now() >= deadline) {
                std::cout << "\n[RtspAnalyzer] Duration reached. Stopping.\n";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    // Block until the pump task finishes (signal, duration, or EOS)
    try { pump_future.get(); } catch (...) {}

    analyzer->stop();

    // Print final summary
    VideoInfo info = analyzer->getVideoInfo();
    BandwidthStats bw = analyzer->getBandwidthStats();

    std::cout << "\n\n========== Final Report ==========\n"
              << "  Stream URI   : " << uri << "\n"
              << "  Codec        : " << info.codec << "\n"
              << "  Resolution   : " << info.width << "x" << info.height << "\n"
              << "  Framerate    : " << std::fixed << std::setprecision(2) << info.framerate << " fps\n"
              << "  Pixel Format : " << info.format << "\n"
              << "  Transport    : " << info.transport << "\n"
              << "  --------------------------------\n"
              << std::fixed << std::setprecision(1)
              << "  Avg Bandwidth: " << bw.average_kbps << " kbps ("
              << (bw.average_kbps / 1000.0) << " Mbps)\n"
              << "  Peak Bandwidth: " << bw.peak_kbps << " kbps ("
              << (bw.peak_kbps / 1000.0) << " Mbps)\n"
              << "  Total Data   : " << (bw.total_bytes / 1024) << " KB ("
              << (bw.total_bytes / (1024 * 1024)) << " MB)\n"
              << "  Total Frames : " << bw.frame_count << "\n"
              << "  Duration     : " << bw.elapsed_seconds << " s\n"
              << "==================================\n";

    auto m = pool.get_metrics();
    std::cout << "[Orchestrator] Completed tasks: " << m.completed_tasks
              << ", Workers: " << m.worker_count << "\n";

    return EXIT_SUCCESS;
}

// ---------------------------------------------------------------------------
// Bridge mode
// ---------------------------------------------------------------------------
static int runBridge(const std::string& bridge_config_path) {
    auto pool = makeOrchestrator();

    // HW detection via orchestrator
    auto hw_future = pool.submit_simple([]{ return detectHardware(); });
    HardwareInfo hw_info = hw_future.get();
    printHardwareInfo(hw_info);

    BridgeConfig bcfg;
    if (!parseBridgeConfig(bridge_config_path, bcfg)) {
        std::cerr << "Failed to parse bridge config: " << bridge_config_path << "\n";
        return EXIT_FAILURE;
    }

    validateBridgeConfig(bcfg, hw_info);

    RtspBridge bridge(pool);
    if (!bridge.start(bcfg)) {
        std::cerr << "Failed to start RTSP bridge.\n";
        return EXIT_FAILURE;
    }

    // GMainLoop on the main thread — the RTSP server's GSource was
    // attached to the default GMainContext here, so iteration must
    // happen on the same thread to avoid context-ownership issues.
    g_main_loop = g_main_loop_new(nullptr, FALSE);

    // Use an orchestrator task just to watch for the quit signal
    // and break the main loop from another thread.
    auto quit_future = pool.submit([](orch::CancellationToken token) {
        while (!token.is_cancelled() && !g_quit)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (g_main_loop)
            g_main_loop_quit(g_main_loop);
    });

    // Block the main thread in the GLib event loop
    g_main_loop_run(g_main_loop);

    bridge.stop();

    quit_future.cancel();
    try { quit_future.get(); } catch (...) {}
    g_main_loop_unref(g_main_loop);
    g_main_loop = nullptr;

    auto m = pool.get_metrics();
    std::cout << "\n[Orchestrator] Completed tasks: " << m.completed_tasks
              << ", Workers: " << m.worker_count << "\n";
    std::cout << "[RtspBridge] Shut down.\n";
    return EXIT_SUCCESS;
}

// ---------------------------------------------------------------------------
// main — CLI11 entry point
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Initialize GStreamer before anything else
    gst_init(&argc, &argv);

    CLI::App app{"HexaRtspCompressor — RTSP stream analyser & bridge"};
    app.require_subcommand(1);

    // --- analyse subcommand ---
    std::string analyse_uri;
    bool analyse_tcp = false;
    bool analyse_view = false;
    int  analyse_duration = 0;

    auto* analyse_cmd = app.add_subcommand("analyse", "Analyse an RTSP stream (bandwidth, codec, resolution)");
    analyse_cmd->add_option("uri", analyse_uri, "RTSP URI to analyse")->required();
    analyse_cmd->add_flag("--tcp", analyse_tcp, "Force RTP-over-TCP transport");
    analyse_cmd->add_flag("--view", analyse_view, "Open a video preview window");
    analyse_cmd->add_option("--duration", analyse_duration, "Run for N seconds then exit");

    // --- bridge subcommand ---
    std::string bridge_config;

    auto* bridge_cmd = app.add_subcommand("bridge", "Start an RTSP re-encoding bridge");
    bridge_cmd->add_option("config", bridge_config, "Path to bridge JSON config")->required()->check(CLI::ExistingFile);

    CLI11_PARSE(app, argc, argv);

    if (analyse_cmd->parsed())
        return runAnalyse(analyse_uri, analyse_tcp, analyse_view, analyse_duration);

    if (bridge_cmd->parsed())
        return runBridge(bridge_config);

    return EXIT_FAILURE;
}
