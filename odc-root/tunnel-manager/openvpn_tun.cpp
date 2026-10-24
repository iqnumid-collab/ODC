#include <openvpn/log/logsimple.hpp>

#include "openvpn_tun.hpp"

#include "odc/logging.hpp"

#include <openvpn/buffer/buffer.hpp>
#include <openvpn/addr/ip.hpp>

#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

namespace odc {
namespace {

bool netlink_addr_up(const std::string& ifname, const std::string& ip, int prefix) {
    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0) return false;
    timeval tv{.tv_sec = 2, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    unsigned ifi = if_nametoindex(ifname.c_str());
    if (!ifi) {
        close(sock);
        return false;
    }

    struct {
        nlmsghdr nh;
        ifaddrmsg ifa;
        char buf[256];
    } req{};
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(ifaddrmsg));
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;
    req.nh.nlmsg_type = RTM_NEWADDR;
    req.ifa.ifa_family = AF_INET;
    req.ifa.ifa_prefixlen = static_cast<unsigned char>(prefix);
    req.ifa.ifa_index = ifi;
    req.ifa.ifa_scope = RT_SCOPE_UNIVERSE;

    in_addr addr{};
    if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
        close(sock);
        return false;
    }
    auto* rta = reinterpret_cast<rtattr*>(req.buf);
    rta->rta_type = IFA_LOCAL;
    rta->rta_len = RTA_LENGTH(sizeof(addr));
    std::memcpy(RTA_DATA(rta), &addr, sizeof(addr));
    req.nh.nlmsg_len += RTA_ALIGN(rta->rta_len);

    sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    iovec iov{.iov_base = &req, .iov_len = req.nh.nlmsg_len};
    msghdr msg{};
    msg.msg_name = &sa;
    msg.msg_namelen = sizeof(sa);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (sendmsg(sock, &msg, 0) < 0) {
        close(sock);
        return false;
    }
    char rbuf[4096];
    iovec riov{.iov_base = rbuf, .iov_len = sizeof(rbuf)};
    msghdr rmsg{};
    rmsg.msg_name = &sa;
    rmsg.msg_namelen = sizeof(sa);
    rmsg.msg_iov = &riov;
    rmsg.msg_iovlen = 1;
    (void)recvmsg(sock, &rmsg, 0);

    // Bring UP via ioctl
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s >= 0) {
        ifreq ifr{};
        std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
        if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0) {
            ifr.ifr_flags |= IFF_UP;
            ioctl(s, SIOCSIFFLAGS, &ifr);
        }
        close(s);
    }
    close(sock);
    return true;
}

std::string parse_gateway(const std::string& subnet, int& prefix) {
    size_t slash = subnet.find('/');
    std::string net = slash == std::string::npos ? subnet : subnet.substr(0, slash);
    prefix = slash == std::string::npos ? 24 : std::stoi(subnet.substr(slash + 1));
    in_addr a{};
    inet_pton(AF_INET, net.c_str(), &a);
    uint32_t host = ntohl(a.s_addr);
    uint32_t mask = prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
    host = (host & mask) + 1;
    a.s_addr = htonl(host);
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf;
}

} // namespace

class OvpnTunSend : public openvpn::TunClientInstance::Send {
public:
    OvpnTunSend(openvpn::TunClientInstance::Recv* parent,
                OvpnTunFactory& factory,
                int fd)
        : parent_(parent), factory_(factory), fd_(fd) {
        info_ = "TUN " + factory_.ifname();
    }

    void stop() override {
        stopped_ = true;
        parent_ = nullptr;
    }

    bool tun_send_const(const openvpn::Buffer& buf) override {
        if (stopped_ || fd_ < 0 || buf.empty()) return false;
        ssize_t n = ::write(fd_, buf.c_data(), buf.size());
        return n == static_cast<ssize_t>(buf.size());
    }

    bool tun_send(openvpn::BufferAllocated& buf) override {
        return tun_send_const(buf);
    }

    openvpn::TunClientInstance::NativeHandle tun_native_handle() override {
        return openvpn::TunClientInstance::NativeHandle(fd_, 0);
    }

    void relay(const openvpn::IP::Addr&, const int) override {}

    const std::string& tun_info() const override { return info_; }

private:
    openvpn::TunClientInstance::Recv* parent_;
    OvpnTunFactory& factory_;
    int fd_;
    std::string info_;
    bool stopped_{false};
};

OvpnTunFactory::OvpnTunFactory(openvpn_io::io_context& io,
                               OvpnClientRegistry& registry,
                               const std::string& subnet,
                               const std::string& ifname)
    : io_(io), registry_(registry), subnet_(subnet), ifname_(ifname) {
    gateway_ip_ = parse_gateway(subnet_, prefix_);
}

OvpnTunFactory::~OvpnTunFactory() {
    close_device();
}

bool OvpnTunFactory::open_device() {
    auto logger = get_logger("ovpn_server");
    if (tun_fd_ >= 0) return true;

    tun_fd_ = open("/dev/net/tun", O_RDWR | O_CLOEXEC);
    if (tun_fd_ < 0) {
        SPDLOG_LOGGER_ERROR(logger, "open /dev/net/tun failed: {}", strerror(errno));
        return false;
    }
    ifreq ifr{};
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    std::strncpy(ifr.ifr_name, ifname_.c_str(), IFNAMSIZ - 1);
    if (ioctl(tun_fd_, TUNSETIFF, &ifr) < 0) {
        SPDLOG_LOGGER_ERROR(logger, "TUNSETIFF failed: {}", strerror(errno));
        close(tun_fd_);
        tun_fd_ = -1;
        return false;
    }
    ifname_ = ifr.ifr_name;

    if (!netlink_addr_up(ifname_, gateway_ip_, prefix_)) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to configure {} as {}/{}",
                            ifname_, gateway_ip_, prefix_);
        close(tun_fd_);
        tun_fd_ = -1;
        return false;
    }

    sd_ = std::make_unique<openvpn_io::posix::stream_descriptor>(io_, tun_fd_);
    start_read();
    SPDLOG_LOGGER_INFO(logger, "OpenVPN TUN {} up at {}/{}", ifname_, gateway_ip_, prefix_);
    return true;
}

void OvpnTunFactory::close_device() {
    reading_ = false;
    if (sd_) {
        openvpn_io::error_code ec;
        sd_->cancel(ec);
        sd_->release();
        sd_.reset();
    }
    if (tun_fd_ >= 0) {
        ::close(tun_fd_);
        tun_fd_ = -1;
    }
}

openvpn::TunClientInstance::Send::Ptr OvpnTunFactory::new_tun_obj(
    openvpn::TunClientInstance::Recv* parent) {
    return openvpn::TunClientInstance::Send::Ptr(new OvpnTunSend(parent, *this, tun_fd_));
}

void OvpnTunFactory::bind_client_vip(const std::string& vip,
                                     openvpn::TunClientInstance::Recv* session) {
    registry_.register_vip(vip, session);
}

void OvpnTunFactory::unbind_client_vip(const std::string& vip) {
    registry_.unregister_vip(vip);
}

void OvpnTunFactory::start_read() {
    if (!sd_ || reading_) return;
    reading_ = true;
    sd_->async_read_some(
        openvpn_io::buffer(raw_buf_),
        [this](const openvpn_io::error_code& ec, std::size_t n) {
            handle_read(ec, n);
        });
}

void OvpnTunFactory::handle_read(const openvpn_io::error_code& ec, std::size_t n) {
    reading_ = false;
    if (ec || n == 0) {
        if (!ec || ec != openvpn_io::error::operation_aborted) {
            start_read();
        }
        return;
    }

    if (n >= 20 && (raw_buf_[0] >> 4) == 4) {
        char dip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, raw_buf_.data() + 16, dip, sizeof(dip));
        auto* session = registry_.session_for_vip(dip);
        if (session) {
            openvpn::BufferAllocated pkt;
            pkt.init(raw_buf_.data(), n, openvpn::BufAllocFlags::NO_FLAGS);
            session->tun_recv(pkt);
        }
    }

    start_read();
}

} // namespace odc
