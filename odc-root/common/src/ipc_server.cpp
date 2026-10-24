#include "odc/ipc_server.hpp"
#include "odc/logging.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <thread>
#include <vector>

namespace odc {

IpcServer::IpcServer(const std::string& socket_path)
    : socket_path_(socket_path) {
}

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start() {
    if (running_) return true;

    std::filesystem::path parent = std::filesystem::path(socket_path_).parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            auto logger = get_logger("ipc_server");
            SPDLOG_LOGGER_ERROR(logger, "Failed to create directory {}: {}",
                                parent.string(), ec.message());
            return false;
        }
    }

    std::filesystem::remove(socket_path_);

    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const auto& path = socket_path_;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
             sizeof(addr)) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 16) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    fcntl(listen_fd_, F_SETFL, O_NONBLOCK);

    running_ = true;
    stop_requested_ = false;

    auto logger = get_logger("ipc_server");
    SPDLOG_LOGGER_INFO(logger, "IPC server listening on {}", socket_path_);
    return true;
}

void IpcServer::run() {
    if (!running_) return;

    auto logger = get_logger("ipc_server");

    while (!stop_requested_) {
        struct pollfd pfd;
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 500);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        if (pfd.revents & POLLIN) {
            int client_fd = accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0) continue;

            fcntl(client_fd, F_SETFL, O_NONBLOCK);

            if (on_connect_) {
                on_connect_(client_fd);
            }

            SPDLOG_LOGGER_DEBUG(logger, "IPC client connected (fd={})", client_fd);

            std::thread([this, client_fd]() {
                handle_client(client_fd);
            }).detach();
        }
    }

    SPDLOG_LOGGER_INFO(logger, "IPC server stopped");
}

void IpcServer::stop() {
    if (!running_) return;
    stop_requested_ = true;
    running_ = false;

    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }

    std::filesystem::remove(socket_path_);
}

void IpcServer::register_method(const std::string& method, MethodHandler handler) {
    methods_[method] = std::move(handler);
}

// ── Private ──────────────────────────────────────────────────────────────────

void IpcServer::handle_client(int client_fd) {
    auto logger = get_logger("ipc_server");
    std::string buffer;

    while (running_ && !stop_requested_) {
        struct pollfd pfd;
        pfd.fd = client_fd;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        char buf[4096];
        ssize_t n = read(client_fd, buf, sizeof(buf));
        if (n <= 0) {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) break;
            continue;
        }

        buffer.append(buf, static_cast<size_t>(n));

        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            if (line.empty()) continue;

            nlohmann::json request;
            try {
                request = nlohmann::json::parse(line);
            } catch (...) {
                nlohmann::json error_resp = {
                    {"jsonrpc", "2.0"},
                    {"error", {{"code", -32700}, {"message", "Parse error"}}},
                    {"id", nullptr}
                };
                write_line(client_fd, error_resp.dump());
                continue;
            }

            std::string method = request.value("method", "");
            nlohmann::json params = request.value("params", nlohmann::json::object());
            nlohmann::json id = request.value("id", nlohmann::json());

            bool is_notification = id.is_null();

            nlohmann::json result = dispatch(method, params, id);

            if (!is_notification) {
                write_line(client_fd, result.dump());
            }
        }
    }

    if (on_disconnect_) {
        on_disconnect_(client_fd);
    }

    close(client_fd);
    SPDLOG_LOGGER_DEBUG(logger, "IPC client disconnected (fd={})", client_fd);
}

std::string IpcServer::read_line(int fd) {
    std::string line;
    char ch;
    while (read(fd, &ch, 1) == 1) {
        if (ch == '\n') break;
        line += ch;
    }
    return line;
}

bool IpcServer::write_line(int fd, const std::string& line) {
    std::string data = line + "\n";
    size_t total = 0;
    while (total < data.size()) {
        ssize_t n = write(fd, data.data() + total, data.size() - total);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return true;
}

nlohmann::json IpcServer::dispatch(const std::string& method,
                                    const nlohmann::json& params,
                                    const nlohmann::json& id) {
    auto it = methods_.find(method);
    if (it == methods_.end()) {
        return {
            {"jsonrpc", "2.0"},
            {"error", {{"code", -32601}, {"message", "Method not found: " + method}}},
            {"id", id}
        };
    }

    try {
        nlohmann::json result = it->second(params);
        return {
            {"jsonrpc", "2.0"},
            {"result", result},
            {"id", id}
        };
    } catch (const std::exception& e) {
        return {
            {"jsonrpc", "2.0"},
            {"error", {{"code", -32000}, {"message", e.what()}}},
            {"id", id}
        };
    }
}

} // namespace odc
