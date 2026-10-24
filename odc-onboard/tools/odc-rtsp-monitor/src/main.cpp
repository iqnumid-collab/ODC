#include "RtspConsumer.h"

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <iostream>
#include <string>
#include <csignal>
#include <thread>
#include <chrono>
#include <iomanip>
#include <cstdlib>

static std::atomic<bool> g_quit{false};

static void signalHandler(int /*sig*/) {
    g_quit = true;
}

static void initLogging() {
    auto console = spdlog::stdout_color_mt("console");
    console->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(console);
    spdlog::cfg::load_env_levels();
    if (spdlog::default_logger()->level() == spdlog::level::off) {
        spdlog::set_level(spdlog::level::info);
    }
}

static void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <rtsp_uri> [--tcp] [--duration <seconds>]\n"
              << "\n"
              << "Options:\n"
              << "  <rtsp_uri>           RTSP URI to consume (required)\n"
              << "  --tcp                Force RTP-over-TCP transport\n"
              << "  --duration <sec>     Run for <sec> seconds then exit (default: 30)\n"
              << "  --help, -h           Show this help message\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " rtsp://localhost:8554/stream\n"
              << "  " << prog << " rtsp://localhost:8554/stream --tcp --duration 20\n";
}

int main(int argc, char* argv[]) {
    initLogging();

    if (argc < 2) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    std::string uri;
    bool use_tcp = false;
    int duration_sec = 30;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tcp") {
            use_tcp = true;
        } else if (arg == "--duration" && i + 1 < argc) {
            duration_sec = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (uri.empty()) {
            uri = arg;
        } else {
            spdlog::error("Unknown argument: {}", arg);
            printUsage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (uri.empty()) {
        spdlog::error("RTSP URI is required.");
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    spdlog::info("odc-rtsp-monitor v1.0.0 starting");
    spdlog::info("  Connecting to: {}", uri);
    spdlog::info("  Transport    : {}", use_tcp ? "TCP" : "UDP (default)");
    spdlog::info("  Duration     : {} seconds", duration_sec);
    spdlog::info(std::string(50, '-'));

    RtspConsumer consumer;

    consumer.onBandwidthUpdate([](const BandwidthStats& stats) {
        spdlog::info("[CLIENT BW] current: {:.1f} kbps  avg: {:.1f} kbps  peak: {:.1f} kbps  "
                     "total: {:.1f} MB  frames: {}  elapsed: {:.1f} s",
                     stats.current_kbps,
                     stats.average_kbps,
                     stats.peak_kbps,
                     static_cast<double>(stats.total_bytes) / (1024 * 1024),
                     stats.frame_count,
                     stats.elapsed_seconds);
    });

    if (!consumer.start(uri, use_tcp)) {
        spdlog::error("Failed to start RTSP consumer.");
        return EXIT_FAILURE;
    }

    GMainContext* ctx = g_main_context_default();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(duration_sec);

    while (consumer.isRunning() && !g_quit) {
        g_main_context_iteration(ctx, FALSE);

        if (std::chrono::steady_clock::now() >= deadline) {
            spdlog::info("[RtspConsumer] Duration reached. Stopping.");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    consumer.stop();

    VideoInfo info = consumer.getVideoInfo();
    BandwidthStats bw = consumer.getBandwidthStats();

    spdlog::info("");
    spdlog::info("========== Client Final Report ==========");
    spdlog::info("  Stream URI   : {}", uri);
    spdlog::info("  Codec        : {}", info.codec);
    spdlog::info("  Resolution   : {}x{}", info.width, info.height);
    spdlog::info("  Framerate    : {:.2f} fps", info.framerate);
    spdlog::info("  Pixel Format : {}", info.format);
    spdlog::info("  Transport    : {}", info.transport);
    spdlog::info("  ----------------------------------------");
    spdlog::info("  Avg Bandwidth: {:.1f} kbps ({:.2f} Mbps)",
                 bw.average_kbps, bw.average_kbps / 1000.0);
    spdlog::info("  Peak Bandwidth: {:.1f} kbps ({:.2f} Mbps)",
                 bw.peak_kbps, bw.peak_kbps / 1000.0);
    spdlog::info("  Total Data   : {:.1f} MB",
                 static_cast<double>(bw.total_bytes) / (1024 * 1024));
    spdlog::info("  Total Frames : {}", bw.frame_count);
    spdlog::info("  Duration     : {:.1f} s", bw.elapsed_seconds);
    spdlog::info("===========================================");

    return EXIT_SUCCESS;
}
