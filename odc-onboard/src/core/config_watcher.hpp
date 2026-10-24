#pragma once

#include <sys/inotify.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace odc {

class ConfigWatcher {
public:
    using ChangeCallback = std::function<void(const std::filesystem::path&)>;

    ConfigWatcher() = default;

    ~ConfigWatcher() {
        stop();
    }

    ConfigWatcher(const ConfigWatcher&) = delete;
    ConfigWatcher& operator=(const ConfigWatcher&) = delete;

    bool start(const std::filesystem::path& config_path, ChangeCallback callback) {
        if (running_.load()) {
            return false;
        }

        config_path_ = config_path;
        callback_ = std::move(callback);

        inotify_fd_ = inotify_init();
        if (inotify_fd_ < 0) {
            SPDLOG_ERROR("Failed to initialize inotify: {}", strerror(errno));
            return false;
        }

        std::string dir_path = config_path_.parent_path().string();
        watch_fd_ = inotify_add_watch(inotify_fd_, dir_path.c_str(),
                                       IN_MODIFY | IN_CLOSE_WRITE | IN_MOVE | IN_DELETE);
        if (watch_fd_ < 0) {
            SPDLOG_ERROR("Failed to add inotify watch for {}: {}", dir_path, strerror(errno));
            close(inotify_fd_);
            inotify_fd_ = -1;
            return false;
        }

        running_.store(true);
        watch_thread_ = std::thread([this]() {
            watch_loop();
        });

        SPDLOG_INFO("Config watcher started for: {}", config_path_.string());
        return true;
    }

    void stop() {
        if (!running_.load()) {
            return;
        }

        running_.store(false);

        if (watch_fd_ >= 0) {
            inotify_rm_watch(inotify_fd_, watch_fd_);
            watch_fd_ = -1;
        }

        if (inotify_fd_ >= 0) {
            close(inotify_fd_);
            inotify_fd_ = -1;
        }

        if (watch_thread_.joinable()) {
            watch_thread_.join();
        }

        SPDLOG_INFO("Config watcher stopped");
    }

    bool is_running() const { return running_.load(); }

private:
    std::filesystem::path config_path_;
    ChangeCallback callback_;
    int inotify_fd_{-1};
    int watch_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread watch_thread_;

    void watch_loop() {
        char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
        const struct inotify_event* event;

        while (running_.load()) {
            int len = read(inotify_fd_, buf, sizeof(buf));
            if (len < 0) {
                if (errno == EINTR) {
                    continue;
                }
                SPDLOG_ERROR("inotify read error: {}", strerror(errno));
                break;
            }

            for (char* ptr = buf; ptr < buf + len; ) {
                event = reinterpret_cast<const struct inotify_event*>(ptr);

                std::string event_name(event->name);
                std::filesystem::path event_path = config_path_.parent_path() / event_name;

                if (event_path == config_path_) {
                    if (event->mask & (IN_MODIFY | IN_CLOSE_WRITE)) {
                        SPDLOG_INFO("Config file modified: {}", config_path_.string());
                        if (callback_) {
                            callback_(config_path_);
                        }
                    } else if (event->mask & IN_DELETE) {
                        SPDLOG_WARN("Config file deleted: {}", config_path_.string());
                    }
                }

                ptr += sizeof(struct inotify_event) + event->len;
            }
        }
    }
};

}
