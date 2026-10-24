#include "odc/logging.hpp"

#include <mutex>
#include <unordered_map>

namespace odc {

namespace {
    std::mutex g_log_mutex;
    std::unordered_map<std::string, LoggerPtr> g_loggers;
    spdlog::level::level_enum g_level{spdlog::level::info};
    bool g_initialized{false};
} // namespace

LoggerPtr get_logger(const std::string& name) {
    std::lock_guard lock(g_log_mutex);
    auto it = g_loggers.find(name);
    if (it != g_loggers.end()) {
        return it->second;
    }

    if (!g_initialized) {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("fallback", sink);
        logger->set_level(g_level);
        spdlog::set_default_logger(logger);
        g_initialized = true;
    }

    auto logger = spdlog::default_logger()->clone(name);
    logger->set_level(g_level);
    g_loggers[name] = logger;
    return logger;
}

void set_log_level(spdlog::level::level_enum level) {
    std::lock_guard lock(g_log_mutex);
    g_level = level;
    spdlog::set_level(level);
    for (auto& [name, logger] : g_loggers) {
        logger->set_level(level);
    }
    if (spdlog::default_logger()) {
        spdlog::default_logger()->set_level(level);
    }
}

void init_logging(spdlog::level::level_enum level, bool console_only) {
    std::lock_guard lock(g_log_mutex);

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    if (!console_only) {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            "odc-root.log", true);
        sinks.push_back(file_sink);
    }

    auto logger = std::make_shared<spdlog::logger>("odc-root", sinks.begin(), sinks.end());
    logger->set_level(level);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
    spdlog::set_default_logger(logger);

    g_level = level;
    g_initialized = true;
}

} // namespace odc
