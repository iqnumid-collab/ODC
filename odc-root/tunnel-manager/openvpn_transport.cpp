#include <openvpn/log/logsimple.hpp>

#include "openvpn_transport.hpp"

#include "odc/logging.hpp"

#include <openvpn/addr/ip.hpp>
#include <openvpn/buffer/buffer.hpp>
#include <openvpn/server/peeraddr.hpp>

#include <array>
#include <sstream>

namespace odc {
namespace {

std::string ep_key(const openvpn_io::ip::udp::endpoint& ep) {
    return ep.address().to_string() + ":" + std::to_string(ep.port());
}

} // namespace

class OvpnTransportServer;

class OvpnTransportClientSend : public openvpn::TransportClientInstance::Send {
public:
    OvpnTransportClientSend(OvpnTransportServer& server,
                            openvpn_io::ip::udp::socket& sock,
                            openvpn_io::ip::udp::endpoint ep)
        : server_(server), sock_(sock), ep_(std::move(ep)) {
        std::ostringstream os;
        os << "UDP " << ep_.address().to_string() << ':' << ep_.port();
        info_ = os.str();
    }

    bool defined() const override { return !stopped_; }
    void stop() override;
    bool transport_send_const(const openvpn::Buffer& buf) override;
    bool transport_send(openvpn::BufferAllocated& buf) override {
        return transport_send_const(buf);
    }
    const std::string& transport_info() const override { return info_; }
    bool stats_pending() const override { return stats_dirty_; }
    openvpn::PeerStats stats_poll() override {
        openvpn::PeerStats ps;
        ps.rx_bytes = rx_;
        ps.tx_bytes = tx_;
        stats_dirty_ = false;
        return ps;
    }

    void add_rx(std::size_t n) {
        rx_ += n;
        stats_dirty_ = true;
    }

    openvpn::TransportClientInstance::Recv::Ptr session;
    openvpn_io::ip::udp::endpoint ep_;

private:
    OvpnTransportServer& server_;
    openvpn_io::ip::udp::socket& sock_;
    std::string info_;
    bool stopped_{false};
    bool stats_dirty_{false};
    std::uint64_t rx_{0};
    std::uint64_t tx_{0};
};

class OvpnTransportServer : public openvpn::TransportServer {
public:
    OvpnTransportServer(openvpn_io::io_context& io,
                        uint16_t port,
                        openvpn::ServerProto::Factory::Ptr proto_factory)
        : io_(io),
          port_(port),
          proto_factory_(std::move(proto_factory)),
          sock_(io) {}

    void start() override {
        auto logger = get_logger("ovpn_server");
        openvpn_io::ip::udp::endpoint ep(openvpn_io::ip::udp::v4(), port_);
        sock_.open(ep.protocol());
        sock_.set_option(openvpn_io::socket_base::reuse_address(true));
        sock_.bind(ep);
        local_ = sock_.local_endpoint();
        SPDLOG_LOGGER_INFO(logger, "OpenVPN UDP listen on :{}", port_);
        start_receive();
    }

    void stop() override {
        halted_ = true;
        openvpn_io::error_code ec;
        sock_.cancel(ec);
        sock_.close(ec);
        std::lock_guard lock(mu_);
        for (auto& [_, send] : sessions_) {
            if (send && send->session) send->session->stop();
        }
        sessions_.clear();
    }

    std::string local_endpoint_info() const override {
        return "UDP 0.0.0.0:" + std::to_string(port_);
    }

    openvpn::IP::Addr local_endpoint_addr() const override {
        return openvpn::IP::Addr::from_asio(local_.address());
    }

    void remove_session(const std::string& key) {
        std::lock_guard lock(mu_);
        sessions_.erase(key);
    }

private:
    void start_receive() {
        if (halted_) return;
        sock_.async_receive_from(
            openvpn_io::buffer(raw_buf_),
            remote_,
            [self = openvpn::RCPtr<OvpnTransportServer>(this)](
                const openvpn_io::error_code& ec, std::size_t n) {
                self->handle_receive(ec, n);
            });
    }

    void handle_receive(const openvpn_io::error_code& ec, std::size_t n) {
        if (halted_) return;
        if (ec) {
            if (ec != openvpn_io::error::operation_aborted) {
                start_receive();
            }
            return;
        }

        // Own a copy — raw_buf_ is reused by the next async_receive_from.
        openvpn::BufferAllocated pkt;
        pkt.init(raw_buf_.data(), n, openvpn::BufAllocFlags::NO_FLAGS);
        const std::string key = ep_key(remote_);

        openvpn::RCPtr<OvpnTransportClientSend> send;
        {
            std::lock_guard lock(mu_);
            auto it = sessions_.find(key);
            if (it == sessions_.end()) {
                if (!proto_factory_->validate_initial_packet(pkt)) {
                    start_receive();
                    return;
                }
                send.reset(new OvpnTransportClientSend(*this, sock_, remote_));
                auto sess = proto_factory_->new_client_instance();
                send->session = sess;

                auto peer = openvpn::PeerAddr::Ptr(new openvpn::PeerAddr());
                peer->tcp = false;
                peer->remote.addr = openvpn::IP::Addr::from_asio(remote_.address());
                peer->remote.port = remote_.port();
                peer->local.addr = openvpn::IP::Addr::from_asio(local_.address());
                peer->local.port = local_.port();

                sess->start(send, peer, next_peer_id_++);
                sessions_[key] = send;
            } else {
                send = it->second;
            }
        }

        send->add_rx(n);
        send->session->transport_recv(pkt);
        start_receive();
    }

    openvpn_io::io_context& io_;
    uint16_t port_;
    openvpn::ServerProto::Factory::Ptr proto_factory_;
    openvpn_io::ip::udp::socket sock_;
    openvpn_io::ip::udp::endpoint local_;
    openvpn_io::ip::udp::endpoint remote_;
    std::array<unsigned char, 2048> raw_buf_{};
    std::mutex mu_;
    std::unordered_map<std::string, openvpn::RCPtr<OvpnTransportClientSend>> sessions_;
    int next_peer_id_{1};
    bool halted_{false};
};

void OvpnTransportClientSend::stop() {
    if (stopped_) return;
    stopped_ = true;
    server_.remove_session(ep_key(ep_));
    session.reset();
}

bool OvpnTransportClientSend::transport_send_const(const openvpn::Buffer& buf) {
    if (stopped_ || buf.empty()) return false;
    openvpn_io::error_code ec;
    sock_.send_to(openvpn_io::buffer(buf.c_data(), buf.size()), ep_, 0, ec);
    if (!ec) {
        tx_ += buf.size();
        stats_dirty_ = true;
        return true;
    }
    return false;
}

OvpnTransportFactory::OvpnTransportFactory(
    uint16_t port,
    openvpn::ServerProto::Factory::Ptr proto_factory)
    : port_(port), proto_factory_(std::move(proto_factory)) {}

openvpn::TransportServer::Ptr OvpnTransportFactory::new_server_obj(
    openvpn_io::io_context& io_context) {
    return openvpn::TransportServer::Ptr(
        new OvpnTransportServer(io_context, port_, proto_factory_));
}

} // namespace odc
