#include "VideoSource.h"
#include "BandwidthThrottler.h"

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
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

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
    std::cerr << "Usage: " << prog << " --video <file> [options]\n"
              << "       " << prog << " --config <config.json>\n"
              << "\n"
              << "Options:\n"
              << "  --video <file>           MP4 video file path (required unless --config)\n"
              << "  --port <port>            RTSP server port (default: 8554)\n"
              << "  --address <addr>         RTSP bind address (default: 0.0.0.0)\n"
              << "  --path <path>            RTSP mount path (default: /stream)\n"
              << "  --loop                   Loop video infinitely (default: true)\n"
              << "  --no-loop                Disable looping\n"
              << "  --max-bandwidth <kbps>   Max bandwidth in kbps (0 = disabled)\n"
              << "  --throttle-mode <mode>   Throttle mode: delay|drop (default: delay)\n"
              << "  --rand-throttle          Enable random throttle (fluctuates BW every 5s)\n"
              << "  --rand-interval <sec>    Random throttle update interval (default: 5)\n"
              << "  --rand-min <percent>     Min throttle percentage (default: 20)\n"
              << "  --rand-max <percent>     Max throttle percentage (default: 100)\n"
              << "  --codec <codec>          Output codec: copy|h264|h265 (default: copy)\n"
              << "  --bitrate <kbps>         Target bitrate for transcoding (kbps)\n"
              << "  --width <px>             Output width for transcoding\n"
              << "  --height <px>            Output height for transcoding\n"
              << "  --framerate <fps>        Output framerate for transcoding\n"
              << "  --preset <preset>        Encoder preset (default: ultrafast)\n"
              << "  --config <file>          Load configuration from JSON file\n"
              << "  --help, -h               Show this help message\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " --video mercedes.mp4 --port 8554 --path /stream --loop\n"
              << "  " << prog << " --video mercedes.mp4 --max-bandwidth 2000 --throttle-mode delay\n"
              << "  " << prog << " --video mercedes.mp4 --max-bandwidth 2000 --rand-throttle\n"
              << "  " << prog << " --video mercedes.mp4 --codec h264 --bitrate 1500 --width 1280 --height 720\n"
              << "  " << prog << " --config config/videosrc.json\n";
}

static bool parseJsonConfig(const std::string& jsonPath, VideoSourceConfig& config) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        spdlog::error("[Config] Cannot open config file: {}", jsonPath);
        return false;
    }

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        spdlog::error("[Config] JSON parse error: {}", e.what());
        return false;
    }

    if (j.contains("video_file")) j.at("video_file").get_to(config.videoFile);
    if (j.contains("rtsp_address")) j.at("rtsp_address").get_to(config.rtspAddress);
    if (j.contains("rtsp_port")) j.at("rtsp_port").get_to(config.rtspPort);
    if (j.contains("mount_path")) j.at("mount_path").get_to(config.mountPath);
    if (j.contains("loop")) j.at("loop").get_to(config.loop);
    if (j.contains("max_bandwidth_kbps")) j.at("max_bandwidth_kbps").get_to(config.throttle.maxBandwidthKbps);
    if (j.contains("codec")) j.at("codec").get_to(config.codec);
    if (j.contains("bitrate")) j.at("bitrate").get_to(config.bitrate);
    if (j.contains("width")) j.at("width").get_to(config.width);
    if (j.contains("height")) j.at("height").get_to(config.height);
    if (j.contains("framerate")) j.at("framerate").get_to(config.framerate);
    if (j.contains("preset")) j.at("preset").get_to(config.preset);

    if (j.contains("throttle_mode")) {
        std::string mode;
        j.at("throttle_mode").get_to(mode);
        if (mode == "drop") {
            config.throttle.mode = ThrottleConfig::Drop;
        } else {
            config.throttle.mode = ThrottleConfig::Delay;
        }
    }

    if (j.contains("rand_throttle")) j.at("rand_throttle").get_to(config.throttle.randThrottle);
    if (j.contains("rand_interval_sec")) j.at("rand_interval_sec").get_to(config.throttle.randIntervalSec);
    if (j.contains("rand_min_percent")) j.at("rand_min_percent").get_to(config.throttle.randMinPercent);
    if (j.contains("rand_max_percent")) j.at("rand_max_percent").get_to(config.throttle.randMaxPercent);

    if (config.videoFile.empty()) {
        spdlog::error("[Config] 'video_file' is required in config.");
        return false;
    }

    return true;
}

static void printFinalReport(const VideoSourceConfig& config, const BandwidthStats& stats) {
    spdlog::info("");
    spdlog::info("========== Final Bandwidth Report ==========");
    spdlog::info("  Video File   : {}", config.videoFile);
    spdlog::info("  Stream       : rtsp://{}:{}{}",
                 config.rtspAddress, config.rtspPort, config.mountPath);
    spdlog::info("  Codec        : {}", config.codec);
    spdlog::info("  ----------------------------------------");
    spdlog::info("  Avg Bandwidth: {:.1f} kbps ({:.2f} Mbps)",
                 stats.average_kbps, stats.average_kbps / 1000.0);
    spdlog::info("  Peak Bandwidth: {:.1f} kbps ({:.2f} Mbps)",
                 stats.peak_kbps, stats.peak_kbps / 1000.0);
    spdlog::info("  Total Data   : {:.1f} MB",
                 static_cast<double>(stats.total_bytes) / (1024 * 1024));
    spdlog::info("  Total Frames : {}", stats.frame_count);
    spdlog::info("  Dropped      : {}", stats.dropped_frames);
    spdlog::info("  Duration     : {:.1f} s", stats.elapsed_seconds);
    spdlog::info("  Throttle     : {} (max: {} kbps, mode: {})",
                 config.throttle.maxBandwidthKbps > 0 ? "enabled" : "disabled",
                 config.throttle.maxBandwidthKbps,
                 config.throttle.mode == ThrottleConfig::Delay ? "delay" : "drop");
    if (config.throttle.randThrottle) {
        spdlog::info("  RandThrottle : {}s interval ({}-{}% of max)",
                     config.throttle.randIntervalSec,
                     config.throttle.randMinPercent,
                     config.throttle.randMaxPercent);
    }
    spdlog::info("============================================");
}

int main(int argc, char* argv[]) {
    initLogging();

    if (argc < 2) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    VideoSourceConfig config;
    std::string configPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--video" && i + 1 < argc) {
            config.videoFile = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.rtspPort = std::atoi(argv[++i]);
        } else if (arg == "--address" && i + 1 < argc) {
            config.rtspAddress = argv[++i];
        } else if (arg == "--path" && i + 1 < argc) {
            config.mountPath = argv[++i];
        } else if (arg == "--loop") {
            config.loop = true;
        } else if (arg == "--no-loop") {
            config.loop = false;
        } else if (arg == "--max-bandwidth" && i + 1 < argc) {
            config.throttle.maxBandwidthKbps = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--throttle-mode" && i + 1 < argc) {
            std::string mode = argv[++i];
            if (mode == "drop") {
                config.throttle.mode = ThrottleConfig::Drop;
            } else {
                config.throttle.mode = ThrottleConfig::Delay;
            }
        } else if (arg == "--rand-throttle") {
            config.throttle.randThrottle = true;
        } else if (arg == "--rand-interval" && i + 1 < argc) {
            config.throttle.randIntervalSec = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--rand-min" && i + 1 < argc) {
            config.throttle.randMinPercent = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--rand-max" && i + 1 < argc) {
            config.throttle.randMaxPercent = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--codec" && i + 1 < argc) {
            config.codec = argv[++i];
        } else if (arg == "--bitrate" && i + 1 < argc) {
            config.bitrate = std::atoi(argv[++i]);
        } else if (arg == "--width" && i + 1 < argc) {
            config.width = std::atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            config.height = std::atoi(argv[++i]);
        } else if (arg == "--framerate" && i + 1 < argc) {
            config.framerate = std::atof(argv[++i]);
        } else if (arg == "--preset" && i + 1 < argc) {
            config.preset = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return EXIT_SUCCESS;
        } else {
            spdlog::error("Unknown argument: {}", arg);
            printUsage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!configPath.empty()) {
        if (!parseJsonConfig(configPath, config)) {
            spdlog::error("Failed to parse config: {}", configPath);
            return EXIT_FAILURE;
        }
    }

    if (config.videoFile.empty()) {
        spdlog::error("Video file is required. Use --video <file> or --config <file>.");
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    spdlog::info("odc-video-source v1.0.0 starting");
    spdlog::info("  Video file : {}", config.videoFile);
    spdlog::info("  RTSP       : rtsp://{}:{}{}",
                 config.rtspAddress, config.rtspPort, config.mountPath);
    spdlog::info("  Loop       : {}", config.loop ? "yes" : "no");
    spdlog::info("  Codec      : {}", config.codec);
    spdlog::info("  Throttle   : {} (max: {} kbps, mode: {})",
                 config.throttle.maxBandwidthKbps > 0 ? "enabled" : "disabled",
                 config.throttle.maxBandwidthKbps,
                 config.throttle.mode == ThrottleConfig::Delay ? "delay" : "drop");
    if (config.throttle.randThrottle) {
        spdlog::info("  RandThrottle: {}s interval ({}-{}% of max)",
                     config.throttle.randIntervalSec,
                     config.throttle.randMinPercent,
                     config.throttle.randMaxPercent);
    }
    spdlog::info(std::string(50, '-'));

    VideoSource source;
    if (!source.start(config)) {
        spdlog::error("Failed to start video source.");
        return EXIT_FAILURE;
    }

    GMainContext* ctx = g_main_context_default();

    while (source.isRunning() && !g_quit) {
        g_main_context_iteration(ctx, FALSE);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    BandwidthStats finalStats = source.getBandwidthStats();
    source.stop();

    printFinalReport(config, finalStats);

    spdlog::info("odc-video-source shut down.");
    return EXIT_SUCCESS;
}
