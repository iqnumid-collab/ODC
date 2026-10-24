#pragma once

#include "ipc/jsonrpc.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/select.h>
#include <string>
#include <mutex>
#include <optional>

namespace odc::ipc {

class IpcClient {
public:
    IpcClient() : fd_(-1), next_id_(1) {}

    ~IpcClient() { disconnect(); }

    bool connect(const std::string& socket_path) {
        fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0) return false;

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        return true;
    }

    void disconnect() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        buffer_.clear();
        pending_response_.reset();
    }

    bool is_connected() const { return fd_ >= 0; }

    nlohmann::json send_request(const std::string& method,
                                 const nlohmann::json& params = {},
                                 int timeout_ms = 5000) {
        JsonRpcRequest req;
        req.method = method;
        req.params = params;
        req.id = next_id_++;

        std::string data = serialize_request(req);
        if (::send(fd_, data.data(), data.size(), MSG_NOSIGNAL) < 0) {
            throw std::runtime_error("Failed to send request: " + std::string(strerror(errno)));
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        while (std::chrono::steady_clock::now() < deadline) {
            auto resp = read_response(100);
            if (resp.has_value()) {
                if (resp->error.has_value()) {
                    throw std::runtime_error(resp->error->message);
                }
                if (resp->result.has_value()) {
                    auto& r = resp->result.value();
                    return r.is_null() ? nlohmann::json::object() : r;
                }
                return nlohmann::json::object();
            }
        }

        throw std::runtime_error("Request timed out: " + method);
    }

    std::optional<std::string> poll_events() {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd_, &readfds);

        timeval tv{0, 0};

        int ret = ::select(fd_ + 1, &readfds, nullptr, nullptr, &tv);
        if (ret <= 0) return std::nullopt;

        char buf[4096];
        ssize_t n = ::recv(fd_, buf, sizeof(buf) - 1, MSG_DONTWAIT);
        if (n <= 0) return std::nullopt;

        buf[n] = '\0';
        buffer_ += buf;

        size_t pos;
        while ((pos = buffer_.find('\n')) != std::string::npos) {
            std::string line = buffer_.substr(0, pos);
            buffer_.erase(0, pos + 1);

            if (line.empty()) continue;

            auto resp = parse_response(line);
            if (resp.has_value()) {
                pending_response_ = *resp;
                continue;
            }

            auto notif = parse_notification(line);
            if (notif.has_value()) {
                return serialize_notification(*notif);
            }
        }

        return std::nullopt;
    }

    bool has_pending_response() const { return pending_response_.has_value(); }

    std::optional<JsonRpcResponse> take_pending_response() {
        auto r = std::move(pending_response_);
        pending_response_.reset();
        return r;
    }

private:
    int fd_;
    int64_t next_id_;
    std::string buffer_;
    std::optional<JsonRpcResponse> pending_response_;

    std::optional<JsonRpcResponse> read_response(int timeout_ms) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd_, &readfds);

        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};

        int ret = ::select(fd_ + 1, &readfds, nullptr, nullptr, &tv);
        if (ret <= 0) return std::nullopt;

        char buf[4096];
        ssize_t n = ::recv(fd_, buf, sizeof(buf) - 1, 0);
        if (n <= 0) return std::nullopt;

        buf[n] = '\0';
        buffer_ += buf;

        size_t pos;
        while ((pos = buffer_.find('\n')) != std::string::npos) {
            std::string line = buffer_.substr(0, pos);
            buffer_.erase(0, pos + 1);

            if (line.empty()) continue;

            auto resp = parse_response(line);
            if (resp.has_value()) return resp;

            auto notif = parse_notification(line);
            if (notif.has_value() && event_callback_) {
                event_callback_(serialize_notification(*notif));
            }
        }

        return std::nullopt;
    }

public:
    std::function<void(const std::string& event_json)> event_callback_;
};

static inline bool socket_exists(const std::string& socket_path) {
    struct stat st;
    if (stat(socket_path.c_str(), &st) != 0) return false;
    return S_ISSOCK(st.st_mode);
}

}
