#pragma once

#include "ipc/ipc_session.hpp"
#include "thread_orchestrator/orchestrator.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>
#include <spdlog/spdlog.h>

#include <string>
#include <filesystem>
#include <thread>
#include <chrono>

namespace odc::ipc {

class IpcServer {
public:
    IpcServer(const std::string& socket_path,
              MethodRegistry& registry,
              EventBus& bus,
              VpnManager& vpn_mgr,
              LogControl& log_ctrl,
              orch::ThreadOrchestrator& orch)
        : socket_path_(socket_path), registry_(registry),
          event_bus_(bus), vpn_mgr_(vpn_mgr), log_ctrl_(log_ctrl),
          orch_(orch), listen_fd_(-1) {}

    ~IpcServer() {
        stop();
    }

    bool start() {
        std::filesystem::path p(socket_path_);
        std::filesystem::create_directories(p.parent_path());

        if (std::filesystem::exists(socket_path_)) {
            std::filesystem::remove(socket_path_);
        }

        listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            SPDLOG_ERROR("IpcServer: failed to create socket: {}", strerror(errno));
            return false;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            SPDLOG_ERROR("IpcServer: failed to bind socket: {}", strerror(errno));
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        chmod(socket_path_.c_str(), 0666);

        if (::listen(listen_fd_, 16) < 0) {
            SPDLOG_ERROR("IpcServer: failed to listen: {}", strerror(errno));
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        SPDLOG_INFO("IpcServer: listening on {}", socket_path_);
        return true;
    }

    void run(const orch::CancellationToken& token) {
        if (listen_fd_ < 0) return;

        while (!token.is_cancelled()) {
            sockaddr_un client_addr{};
            socklen_t client_len = sizeof(client_addr);

            int client_fd = ::accept(listen_fd_,
                                     reinterpret_cast<sockaddr*>(&client_addr),
                                     &client_len);
            if (client_fd < 0) {
                if (errno == EINTR) continue;
                if (token.is_cancelled()) break;
                SPDLOG_ERROR("IpcServer: accept failed: {}", strerror(errno));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            SPDLOG_INFO("IpcServer: client connected (fd={})", client_fd);

            orch_.submit_simple([this, client_fd]() {
                IpcSession session(client_fd, registry_, event_bus_, vpn_mgr_, log_ctrl_, orch_);
                session.run();
                SPDLOG_INFO("IpcServer: client disconnected (fd={})", client_fd);
            });
        }
    }

    void stop() {
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (!socket_path_.empty() && std::filesystem::exists(socket_path_)) {
            std::filesystem::remove(socket_path_);
        }
    }

    bool is_running() const { return listen_fd_ >= 0; }

private:
    std::string socket_path_;
    MethodRegistry& registry_;
    EventBus& event_bus_;
    VpnManager& vpn_mgr_;
    LogControl& log_ctrl_;
    orch::ThreadOrchestrator& orch_;
    int listen_fd_;
};

}
