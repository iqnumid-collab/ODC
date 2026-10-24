#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace odc {

using MethodHandler = std::function<nlohmann::json(const nlohmann::json& params)>;

class IpcServer {
public:
    explicit IpcServer(const std::string& socket_path);
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool start();
    void run();
    void stop();
    bool is_running() const { return running_; }

    void register_method(const std::string& method, MethodHandler handler);

    using ConnectCallback = std::function<void(int client_fd)>;
    using DisconnectCallback = std::function<void(int client_fd)>;
    void set_on_connect(ConnectCallback cb) { on_connect_ = std::move(cb); }
    void set_on_disconnect(DisconnectCallback cb) { on_disconnect_ = std::move(cb); }

    const std::string& socket_path() const { return socket_path_; }

private:
    void handle_client(int client_fd);
    std::string read_line(int fd);
    bool write_line(int fd, const std::string& line);
    nlohmann::json dispatch(const std::string& method, const nlohmann::json& params,
                            const nlohmann::json& id);

    std::string socket_path_;
    int listen_fd_{-1};
    bool running_{false};
    bool stop_requested_{false};

    std::unordered_map<std::string, MethodHandler> methods_;
    ConnectCallback on_connect_;
    DisconnectCallback on_disconnect_;
};

} // namespace odc
