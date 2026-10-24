#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace odc {

class IpcClient {
public:
    explicit IpcClient(const std::string& socket_path);
    ~IpcClient();

    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    bool connect();
    void disconnect();
    bool is_connected() const;

    nlohmann::json call(const std::string& method,
                        const nlohmann::json& params = nlohmann::json::object(),
                        int timeout_ms = 5000);

    void notify(const std::string& method,
                const nlohmann::json& params = nlohmann::json::object());

    void set_reconnect_interval(int seconds);
    void set_on_disconnect(std::function<void()> callback);

    const std::string& socket_path() const { return socket_path_; }

private:
    bool do_connect();
    std::string read_line(int fd, int timeout_ms);
    bool write_line(int fd, const std::string& line);

    std::string socket_path_;
    int fd_{-1};
    int reconnect_interval_sec_{5};
    std::atomic<bool> connected_{false};

    std::function<void()> on_disconnect_;
    std::unique_ptr<std::thread> reconnect_thread_;
    std::atomic<bool> reconnect_running_{false};
    std::atomic<bool> reconnect_stop_{false};

    mutable std::mutex io_mutex_;
};

} // namespace odc
