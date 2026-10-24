#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <memory>
#include <string>

namespace odc {

using LoggerPtr = std::shared_ptr<spdlog::logger>;

LoggerPtr get_logger(const std::string& name);

void set_log_level(spdlog::level::level_enum level);

void init_logging(spdlog::level::level_enum level, bool console_only);

} // namespace odc
