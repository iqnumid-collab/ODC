#pragma once

// Platform detection
#if defined(__linux__)
  #define ODC_PLATFORM_LINUX 1
#elif defined(_WIN32) || defined(_WIN64)
  #define ODC_PLATFORM_WINDOWS 1
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
#elif defined(__APPLE__)
  #define ODC_PLATFORM_MACOS 1
#else
  #error "Unsupported platform"
#endif

#include <filesystem>
#include <functional>
#include <string>

#include <asio.hpp>

namespace odc::platform {

std::filesystem::path default_config_path();
std::filesystem::path default_data_path();
std::filesystem::path default_runtime_path();

bool daemonize();

void setup_signal_handler(asio::signal_set& signals,
                          std::function<void()> on_terminate);

} // namespace odc::platform
