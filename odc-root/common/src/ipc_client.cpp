#include "odc/ipc_client.hpp"
#include "odc/logging.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <cstring>
#include <chrono>

namespace odc {

IpcClient::IpcClient(const std::string& socket_path)
    : socket_path_(socket_path) {
}

IpcClient::~IpcClient() {
    disconnect();
}

bool IpcClient::connect() {
    if (connected_) return true;
    return do_connect();
}

void IpcClient::disconnect() {
    reconnect_stop_ = true;
    if (reconnect_thread_ && reconnect_thread_->joinable()) {
        reconnect_thread_->join();
    }
    reconnect_thread_.reset();

    std::lock_guard lock(io_mutex_);
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    connected_ = false;
}

bool IpcClient::is_connected() const {
    return connected_;
}

nlohmann::json IpcClient::call(const std::string& method,
                                const nlohmann::json& params,
                                int timeout_ms) {
    if (!connected_) {
        if (!do_connect()) {
            return {{"error", "not connected"}};
        }
    }

    static std::atomic<int64_t> next_id{1};
    int64_t req_id = next_id++;

    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params},
        {"id", req_id}
    };

    std::string line;
    {
        std::lock_guard lock(io_mutex_);
        if (!write_line(fd_, request.dump())) {
            connected_ = false;
            return {{"error", "write failed"}};
        }
        line = read_line(fd_, timeout_ms);
    }

    if (line.empty()) {
        connected_ = false;
        return {{"error", "read timeout or disconnect"}};
    }

    try {
        nlohmann::json response = nlohmann::json::parse(line);
        if (response.contains("error")) {
            return response;
        }
        return response.value("result", nlohmann::json());
    } catch (...) {
        return {{"error", "parse error"}};
    }
}

void IpcClient::notify(const std::string& method, const nlohmann::json& params) {
    if (!connected_) {
        if (!do_connect()) return;
    }

    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params}
    };

    std::lock_guard lock(io_mutex_);
    write_line(fd_, request.dump());
}

void IpcClient::set_reconnect_interval(int seconds) {
    reconnect_interval_sec_ = seconds;
}

void IpcClient::set_on_disconnect(std::function<void()> callback) {
    on_disconnect_ = std::move(callback);
}

// ── Private ──────────────────────────────────────────────────────────────────

bool IpcClient::do_connect() {
    std::lock_guard lock(io_mutex_);

    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }

    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) return false;

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr),
                  sizeof(addr)) < 0) {
        close(fd_);
        fd_ = -1;
        return false;
    }

    connected_ = true;

    // Start reconnect thread
    if (!reconnect_running_) {
        reconnect_stop_ = false;
        reconnect_running_ = true;
        reconnect_thread_ = std::make_unique<std::thread>([this]() {
            auto logger = get_logger("ipc_client");
            while (!reconnect_stop_) {
                std::this_thread::sleep_for(std::chrono::seconds(reconnect_interval_sec_));
                if (reconnect_stop_) break;
                if (!connected_) {
                    SPDLOG_LOGGER_DEBUG(logger, "Attempting reconnect to {}",
                                        socket_path_);
                    do_connect();
                }
            }
            reconnect_running_ = false;
        });
    }

    return true;
}

std::string IpcClient::read_line(int fd, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    std::string line;
    while (std::chrono::steady_clock::now() < deadline) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;

        int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count());
        if (remaining < 0) remaining = 0;

        int ret = poll(&pfd, 1, remaining);
        if (ret < 0) {
            if (errno == EINTR) continue;
            return {};
        }
        if (ret == 0) return {};

        char ch;
        if (read(fd, &ch, 1) != 1) return {};
        if (ch == '\n') break;
        line += ch;
    }
    return line;
}

bool IpcClient::write_line(int fd, const std::string& line) {
    std::string data = line + "\n";
    size_t total = 0;
    while (total < data.size()) {
        ssize_t n = write(fd, data.data() + total, data.size() - total);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

} // namespace odc
