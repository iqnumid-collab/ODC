#pragma once

#include "openvpn_server.hpp"
#include "openvpn_management.hpp"

#include <openvpn/tun/server/tunbase.hpp>
#include <openvpn/io/io.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace odc {

class OvpnTunFactory : public openvpn::TunClientInstance::Factory {
public:
    using Ptr = openvpn::RCPtr<OvpnTunFactory>;

    OvpnTunFactory(openvpn_io::io_context& io,
                   OvpnClientRegistry& registry,
                   const std::string& subnet,
                   const std::string& ifname = "ovpn-root");

    ~OvpnTunFactory() override;

    bool open_device();
    void close_device();
    bool is_open() const { return tun_fd_ >= 0; }
    const std::string& ifname() const { return ifname_; }
    std::string gateway_ip() const { return gateway_ip_; }

    openvpn::TunClientInstance::Send::Ptr new_tun_obj(
        openvpn::TunClientInstance::Recv* parent) override;

    void bind_client_vip(const std::string& vip,
                        openvpn::TunClientInstance::Recv* session);
    void unbind_client_vip(const std::string& vip);

private:
    void start_read();
    void handle_read(const openvpn_io::error_code& ec, std::size_t n);

    openvpn_io::io_context& io_;
    OvpnClientRegistry& registry_;
    std::string subnet_;
    std::string ifname_;
    std::string gateway_ip_;
    int prefix_{24};
    int tun_fd_{-1};
    std::unique_ptr<openvpn_io::posix::stream_descriptor> sd_;
    std::array<unsigned char, 2048> raw_buf_{};
    std::mutex mu_;
    bool reading_{false};
};

} // namespace odc
