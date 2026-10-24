#pragma once

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace odc {

class NetlinkManager {
public:
    NetlinkManager() {
        sock_ = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
        if (sock_ < 0) {
            throw std::runtime_error("Failed to create netlink socket: " + std::string(strerror(errno)));
        }
        std::memset(&nl_addr_, 0, sizeof(nl_addr_));
        nl_addr_.nl_family = AF_NETLINK;
        nl_addr_.nl_pid = 0;
        if (bind(sock_, reinterpret_cast<struct sockaddr*>(&nl_addr_), sizeof(nl_addr_)) < 0) {
            close(sock_);
            throw std::runtime_error("Failed to bind netlink socket: " + std::string(strerror(errno)));
        }
        timeval tv{};
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    ~NetlinkManager() {
        if (sock_ >= 0) {
            close(sock_);
        }
    }

    NetlinkManager(const NetlinkManager&) = delete;
    NetlinkManager& operator=(const NetlinkManager&) = delete;

    bool add_address(const std::string& iface, const std::string& ip, int prefix_len, bool ipv6 = false) {
        struct {
            struct nlmsghdr nh;
            struct ifaddrmsg ifa;
            char buf[256];
        } req;

        std::memset(&req, 0, sizeof(req));

        req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
        req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;
        req.nh.nlmsg_type = RTM_NEWADDR;

        req.ifa.ifa_family = ipv6 ? AF_INET6 : AF_INET;
        req.ifa.ifa_prefixlen = prefix_len;
        req.ifa.ifa_index = if_nametoindex(iface.c_str());
        req.ifa.ifa_scope = RT_SCOPE_UNIVERSE;

        if (req.ifa.ifa_index == 0) {
            SPDLOG_ERROR("Interface not found: {}", iface);
            return false;
        }

        char* buf_ptr = req.buf;
        auto append_attr = [&](uint16_t type, const void* data, size_t len) {
            struct rtattr* rta = reinterpret_cast<struct rtattr*>(buf_ptr);
            rta->rta_type = type;
            rta->rta_len = RTA_LENGTH(len);
            std::memcpy(RTA_DATA(rta), data, len);
            buf_ptr += RTA_ALIGN(rta->rta_len);
        };

        if (!ipv6) {
            struct in_addr addr;
            if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
                SPDLOG_ERROR("Invalid IPv4 address: {}", ip);
                return false;
            }
            // Subnet topology (OpenVPN): LOCAL + ADDRESS + BROADCAST.
            append_attr(IFA_LOCAL, &addr, sizeof(addr));
            append_attr(IFA_ADDRESS, &addr, sizeof(addr));
            if (prefix_len > 0 && prefix_len <= 32) {
                uint32_t host = ntohl(addr.s_addr);
                uint32_t mask = prefix_len == 32 ? 0xFFFFFFFFu
                                                : (0xFFFFFFFFu << (32 - prefix_len));
                struct in_addr bcast;
                bcast.s_addr = htonl(host | ~mask);
                append_attr(IFA_BROADCAST, &bcast, sizeof(bcast));
            }
        } else {
            struct in6_addr addr;
            if (inet_pton(AF_INET6, ip.c_str(), &addr) != 1) {
                SPDLOG_ERROR("Invalid IPv6 address: {}", ip);
                return false;
            }
            append_attr(IFA_LOCAL, &addr, sizeof(addr));
            append_attr(IFA_ADDRESS, &addr, sizeof(addr));
        }

        req.nh.nlmsg_len += static_cast<uint32_t>(buf_ptr - req.buf);

        struct sockaddr_nl dest_addr;
        std::memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.nl_family = AF_NETLINK;

        struct iovec iov = { .iov_base = &req, .iov_len = req.nh.nlmsg_len };
        struct msghdr msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.msg_name = &dest_addr;
        msg.msg_namelen = sizeof(dest_addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        if (sendmsg(sock_, &msg, 0) < 0) {
            SPDLOG_ERROR("Failed to add address {} to {}: {}", ip, iface, strerror(errno));
            return false;
        }

        return read_netlink_response();
    }

    bool add_route(const std::string& iface, const std::string& dst, int prefix_len,
                   const std::string& gw = "", bool ipv6 = false) {
        struct {
            struct nlmsghdr nh;
            struct rtmsg rt;
            char buf[512];
        } req;

        std::memset(&req, 0, sizeof(req));

        req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
        req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;
        req.nh.nlmsg_type = RTM_NEWROUTE;

        req.rt.rtm_family = ipv6 ? AF_INET6 : AF_INET;
        req.rt.rtm_dst_len = prefix_len;
        req.rt.rtm_table = RT_TABLE_MAIN;
        req.rt.rtm_protocol = RTPROT_BOOT;
        req.rt.rtm_scope = gw.empty() ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;
        req.rt.rtm_type = RTN_UNICAST;

        char* buf_ptr = req.buf;

        if (!dst.empty() && dst != "0.0.0.0" && dst != "::") {
            struct rtattr* rta = reinterpret_cast<struct rtattr*>(buf_ptr);
            rta->rta_type = RTA_DST;
            if (!ipv6) {
                struct in_addr addr;
                if (inet_pton(AF_INET, dst.c_str(), &addr) != 1) {
                    SPDLOG_ERROR("Invalid IPv4 destination: {}", dst);
                    return false;
                }
                rta->rta_len = RTA_LENGTH(sizeof(addr));
                std::memcpy(RTA_DATA(rta), &addr, sizeof(addr));
                buf_ptr += RTA_ALIGN(rta->rta_len);
            } else {
                struct in6_addr addr;
                if (inet_pton(AF_INET6, dst.c_str(), &addr) != 1) {
                    SPDLOG_ERROR("Invalid IPv6 destination: {}", dst);
                    return false;
                }
                rta->rta_len = RTA_LENGTH(sizeof(addr));
                std::memcpy(RTA_DATA(rta), &addr, sizeof(addr));
                buf_ptr += RTA_ALIGN(rta->rta_len);
            }
        }

        if (!iface.empty()) {
            struct rtattr* rta = reinterpret_cast<struct rtattr*>(buf_ptr);
            rta->rta_type = RTA_OIF;
            rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
            uint32_t ifindex = if_nametoindex(iface.c_str());
            std::memcpy(RTA_DATA(rta), &ifindex, sizeof(ifindex));
            buf_ptr += RTA_ALIGN(rta->rta_len);
        }

        if (!gw.empty()) {
            struct rtattr* rta = reinterpret_cast<struct rtattr*>(buf_ptr);
            rta->rta_type = RTA_GATEWAY;
            if (!ipv6) {
                struct in_addr addr;
                if (inet_pton(AF_INET, gw.c_str(), &addr) != 1) {
                    SPDLOG_ERROR("Invalid IPv4 gateway: {}", gw);
                    return false;
                }
                rta->rta_len = RTA_LENGTH(sizeof(addr));
                std::memcpy(RTA_DATA(rta), &addr, sizeof(addr));
                buf_ptr += RTA_ALIGN(rta->rta_len);
            } else {
                struct in6_addr addr;
                if (inet_pton(AF_INET6, gw.c_str(), &addr) != 1) {
                    SPDLOG_ERROR("Invalid IPv6 gateway: {}", gw);
                    return false;
                }
                rta->rta_len = RTA_LENGTH(sizeof(addr));
                std::memcpy(RTA_DATA(rta), &addr, sizeof(addr));
                buf_ptr += RTA_ALIGN(rta->rta_len);
            }
        }

        req.nh.nlmsg_len += (buf_ptr - req.buf);

        struct sockaddr_nl dest_addr;
        std::memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.nl_family = AF_NETLINK;

        struct iovec iov = { .iov_base = &req, .iov_len = req.nh.nlmsg_len };
        struct msghdr msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.msg_name = &dest_addr;
        msg.msg_namelen = sizeof(dest_addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        if (sendmsg(sock_, &msg, 0) < 0) {
            SPDLOG_ERROR("Failed to add route {} via {} on {}: {}",
                         dst, gw.empty() ? "(link)" : gw, iface, strerror(errno));
            return false;
        }

        return read_netlink_response();
    }

    bool delete_route(const std::string& iface, const std::string& dst, int prefix_len,
                      const std::string& gw = "", bool ipv6 = false) {
        struct {
            struct nlmsghdr nh;
            struct rtmsg rt;
            char buf[512];
        } req;

        std::memset(&req, 0, sizeof(req));

        req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
        req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
        req.nh.nlmsg_type = RTM_DELROUTE;

        req.rt.rtm_family = ipv6 ? AF_INET6 : AF_INET;
        req.rt.rtm_dst_len = prefix_len;
        req.rt.rtm_table = RT_TABLE_MAIN;
        req.rt.rtm_scope = gw.empty() ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;
        req.rt.rtm_type = RTN_UNICAST;

        char* buf_ptr = req.buf;

        if (!dst.empty() && dst != "0.0.0.0" && dst != "::") {
            struct rtattr* rta = reinterpret_cast<struct rtattr*>(buf_ptr);
            rta->rta_type = RTA_DST;
            if (!ipv6) {
                struct in_addr addr;
                if (inet_pton(AF_INET, dst.c_str(), &addr) == 1) {
                    rta->rta_len = RTA_LENGTH(sizeof(addr));
                    std::memcpy(RTA_DATA(rta), &addr, sizeof(addr));
                    buf_ptr += RTA_ALIGN(rta->rta_len);
                }
            } else {
                struct in6_addr addr;
                if (inet_pton(AF_INET6, dst.c_str(), &addr) == 1) {
                    rta->rta_len = RTA_LENGTH(sizeof(addr));
                    std::memcpy(RTA_DATA(rta), &addr, sizeof(addr));
                    buf_ptr += RTA_ALIGN(rta->rta_len);
                }
            }
        }

        if (!iface.empty()) {
            struct rtattr* rta = reinterpret_cast<struct rtattr*>(buf_ptr);
            rta->rta_type = RTA_OIF;
            rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
            uint32_t ifindex = if_nametoindex(iface.c_str());
            std::memcpy(RTA_DATA(rta), &ifindex, sizeof(ifindex));
            buf_ptr += RTA_ALIGN(rta->rta_len);
        }

        req.nh.nlmsg_len += (buf_ptr - req.buf);

        struct sockaddr_nl dest_addr;
        std::memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.nl_family = AF_NETLINK;

        struct iovec iov = { .iov_base = &req, .iov_len = req.nh.nlmsg_len };
        struct msghdr msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.msg_name = &dest_addr;
        msg.msg_namelen = sizeof(dest_addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        if (sendmsg(sock_, &msg, 0) < 0) {
            SPDLOG_ERROR("Failed to delete route on {}: {}", iface, strerror(errno));
            return false;
        }

        return read_netlink_response();
    }

    bool set_mtu(const std::string& iface, int mtu) {
        struct {
            struct nlmsghdr nh;
            struct ifinfomsg ifi;
            struct rtattr rta;
            int mtu_val;
        } req;

        std::memset(&req, 0, sizeof(req));

        req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg) + RTA_LENGTH(sizeof(int)));
        req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
        req.nh.nlmsg_type = RTM_NEWLINK;

        req.ifi.ifi_family = AF_UNSPEC;
        req.ifi.ifi_index = if_nametoindex(iface.c_str());
        // Only touch MTU — do not rewrite interface flags.
        req.ifi.ifi_change = 0;
        req.ifi.ifi_flags = 0;

        req.rta.rta_type = IFLA_MTU;
        req.rta.rta_len = RTA_LENGTH(sizeof(int));
        req.mtu_val = mtu;

        if (req.ifi.ifi_index == 0) {
            SPDLOG_ERROR("Interface not found: {}", iface);
            return false;
        }

        struct sockaddr_nl dest_addr;
        std::memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.nl_family = AF_NETLINK;

        struct iovec iov = { .iov_base = &req, .iov_len = req.nh.nlmsg_len };
        struct msghdr msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.msg_name = &dest_addr;
        msg.msg_namelen = sizeof(dest_addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        if (sendmsg(sock_, &msg, 0) < 0) {
            SPDLOG_ERROR("Failed to set MTU on {}: {}", iface, strerror(errno));
            return false;
        }

        return read_netlink_response();
    }

    bool bring_interface_up(const std::string& iface) {
        struct {
            struct nlmsghdr nh;
            struct ifinfomsg ifi;
        } req;

        std::memset(&req, 0, sizeof(req));

        req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
        req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
        req.nh.nlmsg_type = RTM_NEWLINK;

        req.ifi.ifi_family = AF_UNSPEC;
        req.ifi.ifi_index = if_nametoindex(iface.c_str());
        req.ifi.ifi_change = IFF_UP;
        req.ifi.ifi_flags = IFF_UP;

        if (req.ifi.ifi_index == 0) {
            SPDLOG_ERROR("Interface not found: {}", iface);
            return false;
        }

        struct sockaddr_nl dest_addr;
        std::memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.nl_family = AF_NETLINK;

        struct iovec iov = { .iov_base = &req, .iov_len = req.nh.nlmsg_len };
        struct msghdr msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.msg_name = &dest_addr;
        msg.msg_namelen = sizeof(dest_addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        if (sendmsg(sock_, &msg, 0) < 0) {
            SPDLOG_ERROR("Failed to bring up interface {}: {}", iface, strerror(errno));
            return false;
        }

        return read_netlink_response();
    }

    bool bring_interface_down(const std::string& iface) {
        struct {
            struct nlmsghdr nh;
            struct ifinfomsg ifi;
        } req;

        std::memset(&req, 0, sizeof(req));

        req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
        req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
        req.nh.nlmsg_type = RTM_NEWLINK;

        req.ifi.ifi_family = AF_UNSPEC;
        req.ifi.ifi_index = if_nametoindex(iface.c_str());
        req.ifi.ifi_change = IFF_UP;
        req.ifi.ifi_flags = 0;

        if (req.ifi.ifi_index == 0) {
            SPDLOG_ERROR("Interface not found: {}", iface);
            return false;
        }

        struct sockaddr_nl dest_addr;
        std::memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.nl_family = AF_NETLINK;

        struct iovec iov = { .iov_base = &req, .iov_len = req.nh.nlmsg_len };
        struct msghdr msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.msg_name = &dest_addr;
        msg.msg_namelen = sizeof(dest_addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        if (sendmsg(sock_, &msg, 0) < 0) {
            SPDLOG_ERROR("Failed to bring down interface {}: {}", iface, strerror(errno));
            return false;
        }

        return read_netlink_response();
    }

private:
    int sock_{-1};
    struct sockaddr_nl nl_addr_;

    bool read_netlink_response() {
        char buf[4096];
        // Drain until ACK/DONE/ERROR or socket timeout (SO_RCVTIMEO).
        for (;;) {
            struct iovec iov = { .iov_base = buf, .iov_len = sizeof(buf) };
            struct sockaddr_nl nl_addr;
            struct msghdr msg;
            std::memset(&msg, 0, sizeof(msg));
            msg.msg_name = &nl_addr;
            msg.msg_namelen = sizeof(nl_addr);
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;

            int len = recvmsg(sock_, &msg, 0);
            if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    SPDLOG_ERROR("Netlink response timed out or interrupted: {}",
                                 strerror(errno));
                    return false;
                }
                SPDLOG_ERROR("Failed to read netlink response: {}", strerror(errno));
                return false;
            }

            for (struct nlmsghdr* nh = reinterpret_cast<struct nlmsghdr*>(buf);
                 NLMSG_OK(nh, static_cast<unsigned int>(len));
                 nh = NLMSG_NEXT(nh, len)) {

                if (nh->nlmsg_type == NLMSG_DONE) {
                    return true;
                }

                if (nh->nlmsg_type == NLMSG_ERROR) {
                    struct nlmsgerr* err = reinterpret_cast<struct nlmsgerr*>(NLMSG_DATA(nh));
                    if (err->error != 0) {
                        // EEXIST is benign for CREATE|REPLACE address/route ops.
                        if (-err->error == EEXIST) {
                            return true;
                        }
                        SPDLOG_ERROR("Netlink error: {}", strerror(-err->error));
                        return false;
                    }
                    return true;
                }
            }
        }
    }
};

}
