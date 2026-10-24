#pragma once
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <mutex>

namespace uqmi {

enum class LogLevel { DBG, INFO, WARN, ERR };

inline const char* level_str(LogLevel l) {
    switch (l) {
        case LogLevel::DBG:  return "DBG";
        case LogLevel::INFO: return "INF";
        case LogLevel::WARN: return "WRN";
        case LogLevel::ERR:  return "ERR";
    }
    return "???";
}

inline void log(LogLevel level, const char* fmt, ...) {
    static std::mutex mu;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    std::lock_guard lk(mu);
    std::fprintf(stderr, "[%6ld.%03ld] [%s] %.*s\n",
                 ts.tv_sec, ts.tv_nsec / 1000000, level_str(level),
                 (n > 0 ? n : 0), buf);
}

#define LOG_D(...) ::uqmi::log(::uqmi::LogLevel::DBG,  __VA_ARGS__)
#define LOG_I(...) ::uqmi::log(::uqmi::LogLevel::INFO, __VA_ARGS__)
#define LOG_W(...) ::uqmi::log(::uqmi::LogLevel::WARN, __VA_ARGS__)
#define LOG_E(...) ::uqmi::log(::uqmi::LogLevel::ERR,  __VA_ARGS__)

} // namespace uqmi
