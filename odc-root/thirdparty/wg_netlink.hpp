#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/wireguard.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <unistd.h>
#include <spdlog/spdlog.h>

#ifndef WG_KEY_LEN
#define WG_KEY_LEN 32
#endif

#ifndef MNL_SOCKET_AUTOPID
#define MNL_SOCKET_AUTOPID 0
#endif

#ifndef MNL_ALIGNTO
#define MNL_ALIGNTO 4
#endif

#ifndef MNL_ALIGN
#define MNL_ALIGN(len) (((len)+MNL_ALIGNTO-1) & ~(MNL_ALIGNTO-1))
#endif

#ifndef MNL_ATTR_HDRLEN
#define MNL_ATTR_HDRLEN MNL_ALIGN(sizeof(struct nlattr))
#endif

namespace odc {

struct WgAllowedIP {
    uint16_t family;
    union {
        struct in_addr ip4;
        struct in6_addr ip6;
    };
    uint8_t cidr;
};

struct WgPeer {
    uint8_t public_key[WG_KEY_LEN];
    uint8_t preshared_key[WG_KEY_LEN];
    bool has_preshared_key = false;
    struct sockaddr_storage endpoint;
    uint16_t persistent_keepalive_interval = 0;
    bool has_keepalive = false;
    std::vector<WgAllowedIP> allowed_ips;
};

class WgNetlink {
public:
    WgNetlink() {
        buf_.resize(getpagesize() > 8192 ? 8192 : getpagesize());

        nl_ = socket_open(NETLINK_GENERIC);
        if (!nl_) {
            throw std::runtime_error("Failed to open netlink socket: " + std::string(strerror(errno)));
        }
        if (socket_bind(nl_, 0, MNL_SOCKET_AUTOPID) < 0) {
            socket_close(nl_);
            throw std::runtime_error("Failed to bind netlink socket: " + std::string(strerror(errno)));
        }
        portid_ = socket_get_portid(nl_);

        wg_family_id_ = get_family_id();
        if (wg_family_id_ < 0) {
            socket_close(nl_);
            throw std::runtime_error("WireGuard netlink family not found. Is the wireguard kernel module loaded?");
        }

        rtnl_ = socket_open(NETLINK_ROUTE);
        if (!rtnl_) {
            socket_close(nl_);
            throw std::runtime_error("Failed to open rtnetlink socket: " + std::string(strerror(errno)));
        }
        if (socket_bind(rtnl_, 0, MNL_SOCKET_AUTOPID) < 0) {
            socket_close(rtnl_);
            socket_close(nl_);
            throw std::runtime_error("Failed to bind rtnetlink socket: " + std::string(strerror(errno)));
        }
        rtnl_portid_ = socket_get_portid(rtnl_);
    }

    ~WgNetlink() {
        if (nl_) socket_close(nl_);
        if (rtnl_) socket_close(rtnl_);
    }

    bool create_interface(const std::string& name) {
        char req_buf[512];
        std::memset(req_buf, 0, sizeof(req_buf));

        struct nlmsghdr* nlh = (struct nlmsghdr*)req_buf;
        nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
        nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;
        nlh->nlmsg_type = RTM_NEWLINK;

        struct ifinfomsg* ifi = (struct ifinfomsg*)NLMSG_DATA(nlh);
        ifi->ifi_family = AF_UNSPEC;

        char* buf_ptr = (char*)ifi + sizeof(struct ifinfomsg);

        // IFLA_IFNAME = name
        struct nlattr* ifname = (struct nlattr*)buf_ptr;
        ifname->nla_type = IFLA_IFNAME;
        size_t name_len = strlen(name.c_str()) + 1;
        ifname->nla_len = MNL_ATTR_HDRLEN + name_len;
        memcpy((char*)ifname + MNL_ATTR_HDRLEN, name.c_str(), name_len);
        buf_ptr += MNL_ALIGN(ifname->nla_len);

        // IFLA_LINKINFO nested attribute
        struct nlattr* linkinfo = (struct nlattr*)buf_ptr;
        linkinfo->nla_type = IFLA_LINKINFO;
        buf_ptr += MNL_ALIGN(sizeof(struct nlattr));

        // IFLA_INFO_KIND = "wireguard"
        struct nlattr* kind = (struct nlattr*)buf_ptr;
        kind->nla_type = IFLA_INFO_KIND;
        const char* wg_str = "wireguard";
        size_t wg_len = strlen(wg_str) + 1;
        kind->nla_len = MNL_ATTR_HDRLEN + wg_len;
        memcpy((char*)kind + MNL_ATTR_HDRLEN, wg_str, wg_len);
        buf_ptr += MNL_ALIGN(kind->nla_len);

        linkinfo->nla_len = buf_ptr - (char*)linkinfo;
        nlh->nlmsg_len += buf_ptr - ((char*)ifi + sizeof(struct ifinfomsg));

        return send_rtnl(nlh, nlh->nlmsg_len);
    }

    bool delete_interface(const std::string& name) {
        char req_buf[256];
        std::memset(req_buf, 0, sizeof(req_buf));

        struct nlmsghdr* nlh = (struct nlmsghdr*)req_buf;
        nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
        nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
        nlh->nlmsg_type = RTM_DELLINK;

        struct ifinfomsg* ifi = (struct ifinfomsg*)NLMSG_DATA(nlh);
        ifi->ifi_family = AF_UNSPEC;
        ifi->ifi_index = if_nametoindex(name.c_str());

        if (ifi->ifi_index == 0) {
            return true;
        }

        return send_rtnl(nlh, nlh->nlmsg_len);
    }

    // Retrieve verbose peer data from kernel via WG_CMD_GET_DEVICE.
    // Returns raw parsed peer information keyed by base64-encoded public key.
    struct WgPeerStats {
        std::string public_key_b64;
        int64_t last_handshake_sec{0};
        uint64_t rx_bytes{0};
        uint64_t tx_bytes{0};
        struct sockaddr_storage endpoint{};
        bool has_endpoint{false};
        std::string allowed_ip;
    };

    std::vector<WgPeerStats> get_device_peers(const std::string& name) {
        std::vector<WgPeerStats> result;

        char req_buf[512];
        std::memset(req_buf, 0, sizeof(req_buf));
        struct nlmsghdr* nlh = (struct nlmsghdr*)req_buf;
        nlh->nlmsg_type = wg_family_id_;
        nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
        nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct genlmsghdr));
        struct genlmsghdr* genl = (struct genlmsghdr*)((char*)nlh + NLMSG_HDRLEN);
        genl->cmd = WG_CMD_GET_DEVICE;
        genl->version = 1;

        // Attach interface name
        struct nlattr* nla = (struct nlattr*)((char*)genl + GENL_HDRLEN);
        nla->nla_type = WGDEVICE_A_IFNAME;
        size_t name_len = name.size() + 1;
        nla->nla_len = MNL_ATTR_HDRLEN + MNL_ALIGN(name_len);
        std::memcpy((char*)nla + MNL_ATTR_HDRLEN, name.c_str(), name_len);
        nlh->nlmsg_len += MNL_ALIGN(nla->nla_len);

        if (socket_sendto(nl_, nlh, nlh->nlmsg_len) < 0) {
            SPDLOG_ERROR("Failed to send WG_CMD_GET_DEVICE: {}", strerror(errno));
            return result;
        }

        // Receive multi-part response
        std::vector<char> recv_buf(65536);
        bool done = false;
        while (!done) {
            ssize_t len = socket_recvfrom(nl_, recv_buf.data(), recv_buf.size());
            if (len < 0) {
                SPDLOG_ERROR("Failed to receive WG_CMD_GET_DEVICE response: {}", strerror(errno));
                return result;
            }

            for (struct nlmsghdr* nh = (struct nlmsghdr*)recv_buf.data();
                 NLMSG_OK(nh, (unsigned int)len);
                 nh = NLMSG_NEXT(nh, len)) {
                if (nh->nlmsg_type == NLMSG_DONE) { done = true; break; }
                if (nh->nlmsg_type == NLMSG_ERROR) {
                    struct nlmsgerr* e = (struct nlmsgerr*)((char*)nh + NLMSG_HDRLEN);
                    if (e->error != 0) {
                        SPDLOG_ERROR("Netlink error: {}", strerror(-e->error));
                        return result;
                    }
                    done = true;
                    break;
                }

                // Parse WGDEVICE_A_PEERS → WGPEER_A_* attributes
                if (nh->nlmsg_type != static_cast<uint16_t>(wg_family_id_)) {
                    continue;
                }
                struct genlmsghdr* rgenl = (struct genlmsghdr*)((char*)nh + NLMSG_HDRLEN);
                struct nlattr* dev_attr = (struct nlattr*)((char*)rgenl + GENL_HDRLEN);
                int dev_attr_len = nh->nlmsg_len - NLMSG_HDRLEN - GENL_HDRLEN;

                auto part = parse_device_attrs(dev_attr, dev_attr_len);
                result.insert(result.end(), part.begin(), part.end());
            }
        }
        return result;
    }

    bool set_device(const std::string& name, const uint8_t* private_key,
                    uint16_t listen_port, const std::vector<WgPeer>& peers) {
        struct nlmsghdr* nlh = prepare_msg(WG_CMD_SET_DEVICE, NLM_F_REQUEST | NLM_F_ACK);
        attr_put_strz(nlh, WGDEVICE_A_IFNAME, name.c_str());

        uint32_t flags = WGDEVICE_F_REPLACE_PEERS;
        attr_put_u32(nlh, WGDEVICE_A_FLAGS, flags);

        if (private_key) {
            attr_put(nlh, WGDEVICE_A_PRIVATE_KEY, WG_KEY_LEN, private_key);
        }
        if (listen_port > 0) {
            attr_put_u16(nlh, WGDEVICE_A_LISTEN_PORT, listen_port);
        }

        if (!peers.empty()) {
            struct nlattr* peers_nest = attr_nest_start(nlh, WGDEVICE_A_PEERS);
            for (const auto& peer : peers) {
                struct nlattr* peer_nest = attr_nest_start(nlh, 0);
                attr_put(nlh, WGPEER_A_PUBLIC_KEY, WG_KEY_LEN, peer.public_key);

                uint32_t peer_flags = WGPEER_F_REPLACE_ALLOWEDIPS;
                if (peer.has_preshared_key) {
                    attr_put(nlh, WGPEER_A_PRESHARED_KEY, WG_KEY_LEN, peer.preshared_key);
                }

                if (peer.endpoint.ss_family == AF_INET) {
                    attr_put(nlh, WGPEER_A_ENDPOINT, sizeof(struct sockaddr_in), &peer.endpoint);
                } else if (peer.endpoint.ss_family == AF_INET6) {
                    attr_put(nlh, WGPEER_A_ENDPOINT, sizeof(struct sockaddr_in6), &peer.endpoint);
                }

                if (peer.has_keepalive) {
                    attr_put_u16(nlh, WGPEER_A_PERSISTENT_KEEPALIVE_INTERVAL, peer.persistent_keepalive_interval);
                }

                if (peer_flags) {
                    attr_put_u32(nlh, WGPEER_A_FLAGS, peer_flags);
                }

                if (!peer.allowed_ips.empty()) {
                    struct nlattr* allowedips_nest = attr_nest_start(nlh, WGPEER_A_ALLOWEDIPS);
                    for (const auto& aip : peer.allowed_ips) {
                        struct nlattr* aip_nest = attr_nest_start(nlh, 0);
                        attr_put_u16(nlh, WGALLOWEDIP_A_FAMILY, aip.family);
                        if (aip.family == AF_INET) {
                            attr_put(nlh, WGALLOWEDIP_A_IPADDR, sizeof(struct in_addr), &aip.ip4);
                        } else {
                            attr_put(nlh, WGALLOWEDIP_A_IPADDR, sizeof(struct in6_addr), &aip.ip6);
                        }
                        attr_put_u8(nlh, WGALLOWEDIP_A_CIDR_MASK, aip.cidr);
                        attr_nest_end(nlh, aip_nest);
                    }
                    attr_nest_end(nlh, allowedips_nest);
                }

                attr_nest_end(nlh, peer_nest);
            }
            attr_nest_end(nlh, peers_nest);
        }

        return send_and_recv();
    }

    bool bring_interface_up(const std::string& name) {
        return set_interface_flags(name, IFF_UP);
    }

    bool bring_interface_down(const std::string& name) {
        return set_interface_flags(name, 0);
    }

    bool add_address(const std::string& name, const std::string& ip, int prefix_len, bool ipv6 = false) {
        char req_buf[512];
        std::memset(req_buf, 0, sizeof(req_buf));

        struct nlmsghdr* nlh = (struct nlmsghdr*)req_buf;
        nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
        nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;
        nlh->nlmsg_type = RTM_NEWADDR;

        struct ifaddrmsg* ifa = (struct ifaddrmsg*)NLMSG_DATA(nlh);
        ifa->ifa_family = ipv6 ? AF_INET6 : AF_INET;
        ifa->ifa_prefixlen = prefix_len;
        ifa->ifa_index = if_nametoindex(name.c_str());
        ifa->ifa_scope = RT_SCOPE_UNIVERSE;

        if (ifa->ifa_index == 0) {
            SPDLOG_ERROR("Interface not found: {}", name);
            return false;
        }

        char* buf_ptr = (char*)ifa + sizeof(struct ifaddrmsg);
        struct nlattr* rta = (struct nlattr*)buf_ptr;
        rta->nla_type = IFA_LOCAL;
        if (!ipv6) {
            struct in_addr addr;
            if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
                SPDLOG_ERROR("Invalid IPv4 address: {}", ip);
                return false;
            }
            rta->nla_len = MNL_ATTR_HDRLEN + sizeof(addr);
            memcpy((char*)rta + MNL_ATTR_HDRLEN, &addr, sizeof(addr));
        } else {
            struct in6_addr addr;
            if (inet_pton(AF_INET6, ip.c_str(), &addr) != 1) {
                SPDLOG_ERROR("Invalid IPv6 address: {}", ip);
                return false;
            }
            rta->nla_len = MNL_ATTR_HDRLEN + sizeof(addr);
            memcpy((char*)rta + MNL_ATTR_HDRLEN, &addr, sizeof(addr));
        }

        nlh->nlmsg_len += MNL_ALIGN(rta->nla_len);
        return send_rtnl(nlh, nlh->nlmsg_len);
    }

    bool add_route(const std::string& name, const std::string& dst, int prefix_len, bool ipv6 = false) {
        char req_buf[512];
        std::memset(req_buf, 0, sizeof(req_buf));

        struct nlmsghdr* nlh = (struct nlmsghdr*)req_buf;
        nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
        nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;
        nlh->nlmsg_type = RTM_NEWROUTE;

        struct rtmsg* rt = (struct rtmsg*)NLMSG_DATA(nlh);
        rt->rtm_family = ipv6 ? AF_INET6 : AF_INET;
        rt->rtm_dst_len = prefix_len;
        rt->rtm_table = RT_TABLE_MAIN;
        rt->rtm_protocol = RTPROT_BOOT;
        rt->rtm_scope = RT_SCOPE_LINK;
        rt->rtm_type = RTN_UNICAST;

        char* buf_ptr = (char*)rt + sizeof(struct rtmsg);
        struct nlattr* rta = (struct nlattr*)buf_ptr;
        rta->nla_type = RTA_OIF;
        rta->nla_len = MNL_ATTR_HDRLEN + sizeof(uint32_t);
        uint32_t ifindex = if_nametoindex(name.c_str());
        memcpy((char*)rta + MNL_ATTR_HDRLEN, &ifindex, sizeof(ifindex));
        buf_ptr += MNL_ALIGN(rta->nla_len);

        if (!dst.empty() && dst != "0.0.0.0" && dst != "::") {
            rta = (struct nlattr*)buf_ptr;
            rta->nla_type = RTA_DST;
            if (!ipv6) {
                struct in_addr addr;
                if (inet_pton(AF_INET, dst.c_str(), &addr) != 1) {
                    SPDLOG_ERROR("Invalid IPv4 destination: {}", dst);
                    return false;
                }
                rta->nla_len = MNL_ATTR_HDRLEN + sizeof(addr);
                memcpy((char*)rta + MNL_ATTR_HDRLEN, &addr, sizeof(addr));
                buf_ptr += MNL_ALIGN(rta->nla_len);
            }
        }

        nlh->nlmsg_len += (buf_ptr - ((char*)rt + sizeof(struct rtmsg)));
        return send_rtnl(nlh, nlh->nlmsg_len);
    }

private:
    struct nl_socket {
        int fd;
        struct sockaddr_nl addr;
    };

    struct nl_socket* nl_ = nullptr;
    struct nl_socket* rtnl_ = nullptr;
    unsigned int portid_ = 0;
    unsigned int rtnl_portid_ = 0;
    int wg_family_id_ = -1;
    std::vector<char> buf_;

    struct nl_socket* socket_open(int bus) {
        struct nl_socket* nl = (struct nl_socket*)calloc(1, sizeof(struct nl_socket));
        if (!nl) return nullptr;
        nl->fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, bus);
        if (nl->fd == -1) {
            free(nl);
            return nullptr;
        }
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(nl->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        return nl;
    }

    int socket_bind(struct nl_socket* nl, unsigned int groups, pid_t pid) {
        struct sockaddr_nl addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.nl_family = AF_NETLINK;
        addr.nl_groups = groups;
        addr.nl_pid = pid;
        int ret = bind(nl->fd, (struct sockaddr*)&addr, sizeof(addr));
        if (ret < 0) return ret;
        socklen_t addr_len = sizeof(addr);
        ret = getsockname(nl->fd, (struct sockaddr*)&addr, &addr_len);
        if (ret < 0) return ret;
        nl->addr = addr;
        return 0;
    }

    unsigned int socket_get_portid(const struct nl_socket* nl) {
        return nl->addr.nl_pid;
    }

    ssize_t socket_sendto(const struct nl_socket* nl, const void* buf, size_t len) {
        static const struct sockaddr_nl snl = { .nl_family = AF_NETLINK };
        return sendto(nl->fd, buf, len, 0, (struct sockaddr*)&snl, sizeof(snl));
    }

    ssize_t socket_recvfrom(const struct nl_socket* nl, void* buf, size_t bufsiz) {
        struct sockaddr_nl addr;
        struct iovec iov = { .iov_base = buf, .iov_len = bufsiz };
        struct msghdr msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.msg_name = &addr;
        msg.msg_namelen = sizeof(addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;
        ssize_t ret = recvmsg(nl->fd, &msg, 0);
        if (ret == -1) return ret;
        if (msg.msg_namelen != sizeof(struct sockaddr_nl)) {
            errno = EINVAL;
            return -1;
        }
        return ret;
    }

    int socket_close(struct nl_socket* nl) {
        if (!nl) return 0;
        int ret = close(nl->fd);
        free(nl);
        return ret;
    }

    struct nlmsghdr* nlmsg_put_header(void* buf) {
        int len = MNL_ALIGN(sizeof(struct nlmsghdr));
        struct nlmsghdr* nlh = (struct nlmsghdr*)buf;
        std::memset(buf, 0, len);
        nlh->nlmsg_len = len;
        return nlh;
    }

    void* nlmsg_put_extra_header(struct nlmsghdr* nlh, size_t size) {
        char* ptr = (char*)nlh + nlh->nlmsg_len;
        size_t len = MNL_ALIGN(size);
        nlh->nlmsg_len += len;
        std::memset(ptr, 0, len);
        return ptr;
    }

    void* nlmsg_get_payload_tail(const struct nlmsghdr* nlh) {
        return (void*)nlh + MNL_ALIGN(nlh->nlmsg_len);
    }

    void attr_put(struct nlmsghdr* nlh, uint16_t type, size_t len, const void* data) {
        struct nlattr* attr = (struct nlattr*)nlmsg_get_payload_tail(nlh);
        uint16_t payload_len = MNL_ALIGN(sizeof(struct nlattr)) + len;
        attr->nla_type = type;
        attr->nla_len = payload_len;
        memcpy((char*)attr + MNL_ATTR_HDRLEN, data, len);
        nlh->nlmsg_len += MNL_ALIGN(payload_len);
    }

    void attr_put_u16(struct nlmsghdr* nlh, uint16_t type, uint16_t data) {
        attr_put(nlh, type, sizeof(uint16_t), &data);
    }

    void attr_put_u32(struct nlmsghdr* nlh, uint16_t type, uint32_t data) {
        attr_put(nlh, type, sizeof(uint32_t), &data);
    }

    void attr_put_u8(struct nlmsghdr* nlh, uint16_t type, uint8_t data) {
        attr_put(nlh, type, sizeof(uint8_t), &data);
    }

    void attr_put_strz(struct nlmsghdr* nlh, uint16_t type, const char* data) {
        attr_put(nlh, type, strlen(data) + 1, data);
    }

    struct nlattr* attr_nest_start(struct nlmsghdr* nlh, uint16_t type) {
        struct nlattr* start = (struct nlattr*)nlmsg_get_payload_tail(nlh);
        start->nla_type = NLA_F_NESTED | type;
        nlh->nlmsg_len += MNL_ALIGN(sizeof(struct nlattr));
        return start;
    }

    void attr_nest_end(struct nlmsghdr* nlh, struct nlattr* start) {
        start->nla_len = (char*)nlmsg_get_payload_tail(nlh) - (char*)start;
    }

    struct nlmsghdr* prepare_msg(uint8_t cmd, uint16_t flags) {
        struct nlmsghdr* nlh = nlmsg_put_header(buf_.data());
        nlh->nlmsg_type = wg_family_id_;
        nlh->nlmsg_flags = flags;
        struct genlmsghdr* genl = (struct genlmsghdr*)nlmsg_put_extra_header(nlh, sizeof(struct genlmsghdr));
        genl->cmd = cmd;
        genl->version = 1;
        return nlh;
    }

    int get_family_id() {
        struct nlmsghdr* nlh = nlmsg_put_header(buf_.data());
        nlh->nlmsg_type = GENL_ID_CTRL;
        nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
        struct genlmsghdr* genl = (struct genlmsghdr*)nlmsg_put_extra_header(nlh, sizeof(struct genlmsghdr));
        genl->cmd = CTRL_CMD_GETFAMILY;
        genl->version = 1;
        attr_put_strz(nlh, CTRL_ATTR_FAMILY_NAME, WG_GENL_NAME);

        if (socket_sendto(nl_, nlh, nlh->nlmsg_len) < 0) {
            return -1;
        }

        ssize_t len = socket_recvfrom(nl_, buf_.data(), buf_.size());
        if (len < 0) return -1;

        struct nlattr* tb[CTRL_ATTR_MAX + 1] = {0};
        struct nlmsghdr* resp = (struct nlmsghdr*)buf_.data();
        struct genlmsghdr* resp_genl = (struct genlmsghdr*)((char*)resp + NLMSG_HDRLEN);
        struct nlattr* attr = (struct nlattr*)((char*)resp_genl + GENL_HDRLEN);
        int attr_len = resp->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);

        while (attr_len > 0 && MNL_ALIGN(sizeof(struct nlattr)) <= attr_len) {
            if (attr->nla_len < MNL_ATTR_HDRLEN || attr->nla_len > (unsigned int)attr_len) break;
            uint16_t type = attr->nla_type & NLA_TYPE_MASK;
            if (type < CTRL_ATTR_MAX + 1) {
                tb[type] = attr;
            }
            attr = (struct nlattr*)((char*)attr + MNL_ALIGN(attr->nla_len));
        }

        if (tb[CTRL_ATTR_FAMILY_ID]) {
            return *(uint16_t*)((char*)tb[CTRL_ATTR_FAMILY_ID] + MNL_ATTR_HDRLEN);
        }
        return -1;
    }

    bool send_and_recv() {
        struct nlmsghdr* nlh = (struct nlmsghdr*)buf_.data();
        if (socket_sendto(nl_, nlh, nlh->nlmsg_len) < 0) {
            SPDLOG_ERROR("Failed to send netlink message: {}", strerror(errno));
            return false;
        }

        ssize_t len = socket_recvfrom(nl_, buf_.data(), buf_.size());
        if (len < 0) {
            SPDLOG_ERROR("Failed to receive netlink response: {}", strerror(errno));
            return false;
        }

        for (struct nlmsghdr* nh = (struct nlmsghdr*)buf_.data();
             NLMSG_OK(nh, (unsigned int)len);
             nh = NLMSG_NEXT(nh, len)) {
            if (nh->nlmsg_type == NLMSG_DONE) return true;
            if (nh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr* err = (struct nlmsgerr*)((char*)nh + NLMSG_HDRLEN);
                if (err->error != 0) {
                    SPDLOG_ERROR("Netlink error: {}", strerror(-err->error));
                    return false;
                }
                return true;
            }
        }
        return true;
    }

    bool send_rtnl(const struct nlmsghdr* nlh, size_t len) {
        if (socket_sendto(rtnl_, nlh, len) < 0) {
            SPDLOG_ERROR("Failed to send rtnetlink message: {}", strerror(errno));
            return false;
        }

        char resp_buf[4096];
        ssize_t resp_len = socket_recvfrom(rtnl_, resp_buf, sizeof(resp_buf));
        if (resp_len < 0) {
            SPDLOG_ERROR("Failed to receive rtnetlink response: {}", strerror(errno));
            return false;
        }

        for (struct nlmsghdr* nh = (struct nlmsghdr*)resp_buf;
             NLMSG_OK(nh, (unsigned int)resp_len);
             nh = NLMSG_NEXT(nh, resp_len)) {
            if (nh->nlmsg_type == NLMSG_DONE) return true;
            if (nh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr* err = (struct nlmsgerr*)((char*)nh + NLMSG_HDRLEN);
                if (err->error != 0) {
                    SPDLOG_ERROR("Rtnetlink error: {}", strerror(-err->error));
                    return false;
                }
                return true;
            }
        }
        return true;
    }

    bool set_interface_flags(const std::string& name, unsigned int flags) {
        char req_buf[256];
        std::memset(req_buf, 0, sizeof(req_buf));

        struct nlmsghdr* nlh = (struct nlmsghdr*)req_buf;
        nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
        nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
        nlh->nlmsg_type = RTM_NEWLINK;

        struct ifinfomsg* ifi = (struct ifinfomsg*)NLMSG_DATA(nlh);
        ifi->ifi_family = AF_UNSPEC;
        ifi->ifi_index = if_nametoindex(name.c_str());
        ifi->ifi_change = 0xFFFFFFFF;
        ifi->ifi_flags = flags;

        if (ifi->ifi_index == 0) {
            SPDLOG_ERROR("Interface not found: {}", name);
            return false;
        }

        return send_rtnl(nlh, nlh->nlmsg_len);
    }

    static std::string key_to_base64(const uint8_t* key, size_t len) {
        static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        result.reserve(44);
        for (size_t i = 0; i < len; i += 3) {
            uint32_t bits = (static_cast<uint32_t>(key[i]) << 16);
            if (i + 1 < len) bits |= (static_cast<uint32_t>(key[i + 1]) << 8);
            if (i + 2 < len) bits |= static_cast<uint32_t>(key[i + 2]);
            result += b64[(bits >> 18) & 0x3F];
            result += b64[(bits >> 12) & 0x3F];
            result += (i + 1 < len) ? b64[(bits >> 6) & 0x3F] : '=';
            result += (i + 2 < len) ? b64[bits & 0x3F] : '=';
        }
        return result;
    }

    std::vector<WgPeerStats> parse_device_attrs(struct nlattr* attr, int attr_len) {
        std::vector<WgPeerStats> peers;

        while (attr_len > 0 &&
               static_cast<int>(MNL_ALIGN(sizeof(struct nlattr))) <= attr_len &&
               attr->nla_len >= MNL_ATTR_HDRLEN &&
               attr->nla_len <= static_cast<unsigned int>(attr_len)) {

            uint16_t type = attr->nla_type & NLA_TYPE_MASK;
            uint16_t payload_len = attr->nla_len - MNL_ATTR_HDRLEN;
            char* payload = (char*)attr + MNL_ATTR_HDRLEN;

            if (type == WGDEVICE_A_PEERS) {
                // Parse nested peer attributes
                struct nlattr* peer_attr = (struct nlattr*)payload;
                int peer_attr_len = payload_len;

                while (peer_attr_len > 0 &&
                       static_cast<int>(MNL_ALIGN(sizeof(struct nlattr))) <= peer_attr_len &&
                       peer_attr->nla_len >= MNL_ATTR_HDRLEN &&
                       peer_attr->nla_len <= static_cast<unsigned int>(peer_attr_len)) {

                    uint16_t peer_type = peer_attr->nla_type & NLA_TYPE_MASK;
                    uint16_t ppl = peer_attr->nla_len - MNL_ATTR_HDRLEN;
                    char* pp = (char*)peer_attr + MNL_ATTR_HDRLEN;

                    if (peer_type == 0) { // Peer index
                        WgPeerStats ps;
                        parse_peer_attrs(pp, ppl, ps);
                        if (!ps.public_key_b64.empty()) {
                            peers.push_back(std::move(ps));
                        }
                    }

                    peer_attr = (struct nlattr*)((char*)peer_attr + MNL_ALIGN(peer_attr->nla_len));
                    peer_attr_len -= MNL_ALIGN(peer_attr->nla_len);
                }
            }

            attr = (struct nlattr*)((char*)attr + MNL_ALIGN(attr->nla_len));
            attr_len -= MNL_ALIGN(attr->nla_len);
        }

        return peers;
    }

    void parse_peer_attrs(char* buf, int buf_len, WgPeerStats& ps) {
        struct nlattr* attr = (struct nlattr*)buf;

        while (buf_len > 0 &&
               static_cast<int>(MNL_ALIGN(sizeof(struct nlattr))) <= buf_len &&
               attr->nla_len >= MNL_ATTR_HDRLEN &&
               attr->nla_len <= static_cast<unsigned int>(buf_len)) {

            uint16_t type = attr->nla_type & NLA_TYPE_MASK;
            uint16_t plen = attr->nla_len - MNL_ATTR_HDRLEN;
            char* payload = (char*)attr + MNL_ATTR_HDRLEN;

            switch (type) {
            case WGPEER_A_PUBLIC_KEY:
                if (plen == WG_KEY_LEN) {
                    ps.public_key_b64 = key_to_base64(
                        reinterpret_cast<const uint8_t*>(payload), WG_KEY_LEN);
                }
                break;
            case WGPEER_A_ENDPOINT:
                if (plen >= (int)sizeof(struct sockaddr_in)) {
                    std::memcpy(&ps.endpoint, payload,
                                std::min(plen, (uint16_t)sizeof(ps.endpoint)));
                    ps.has_endpoint = true;
                }
                break;
            case WGPEER_A_LAST_HANDSHAKE_TIME: {
                if (plen == 8) {
                    uint64_t handshake_ns = 0;
                    std::memcpy(&handshake_ns, payload, sizeof(handshake_ns));
                    // Kernel reports nanoseconds since boot. Convert to seconds.
                    ps.last_handshake_sec = static_cast<int64_t>(handshake_ns / 1000000000ULL);
                }
                break;
            }
            case WGPEER_A_RX_BYTES:
                if (plen == 8) std::memcpy(&ps.rx_bytes, payload, sizeof(ps.rx_bytes));
                break;
            case WGPEER_A_TX_BYTES:
                if (plen == 8) std::memcpy(&ps.tx_bytes, payload, sizeof(ps.tx_bytes));
                break;
            case WGPEER_A_ALLOWEDIPS: {
                // Parse first allowed IP
                struct nlattr* aip = (struct nlattr*)payload;
                int aip_len = plen;
                while (aip_len > 0 &&
                       static_cast<int>(MNL_ALIGN(sizeof(struct nlattr))) <= aip_len &&
                       aip->nla_len >= MNL_ATTR_HDRLEN &&
                       aip->nla_len <= static_cast<unsigned int>(aip_len)) {
                    uint16_t atype = aip->nla_type & NLA_TYPE_MASK;
                    if (atype == 0) {
                        // AllowedIP entry: parse family + ipaddr + cidr
                        parse_allowed_ip(aip, ps);
                        break;
                    }
                    aip = (struct nlattr*)((char*)aip + MNL_ALIGN(aip->nla_len));
                    aip_len -= MNL_ALIGN(aip->nla_len);
                }
                break;
            }
            default:
                break;
            }

            attr = (struct nlattr*)((char*)attr + MNL_ALIGN(attr->nla_len));
            buf_len -= MNL_ALIGN(attr->nla_len);
        }
    }

    void parse_allowed_ip(struct nlattr* aip_nest, WgPeerStats& ps) {
        char* payload = (char*)aip_nest + MNL_ATTR_HDRLEN;
        int nest_len = aip_nest->nla_len - MNL_ATTR_HDRLEN;
        struct nlattr* attr = (struct nlattr*)payload;
        uint16_t family = AF_INET;
        char ip_buf[INET6_ADDRSTRLEN] = {};
        int cidr = 32;

        while (nest_len > 0 &&
               static_cast<int>(MNL_ALIGN(sizeof(struct nlattr))) <= nest_len &&
               attr->nla_len >= MNL_ATTR_HDRLEN &&
               attr->nla_len <= static_cast<unsigned int>(nest_len)) {

            uint16_t type = attr->nla_type & NLA_TYPE_MASK;
            uint16_t plen = attr->nla_len - MNL_ATTR_HDRLEN;
            char* pp = (char*)attr + MNL_ATTR_HDRLEN;

            switch (type) {
            case WGALLOWEDIP_A_FAMILY:
                if (plen == 2) std::memcpy(&family, pp, sizeof(family));
                break;
            case WGALLOWEDIP_A_IPADDR:
                if (family == AF_INET && plen >= 4)
                    inet_ntop(AF_INET, pp, ip_buf, sizeof(ip_buf));
                else if (family == AF_INET6 && plen >= 16)
                    inet_ntop(AF_INET6, pp, ip_buf, sizeof(ip_buf));
                break;
            case WGALLOWEDIP_A_CIDR_MASK:
                if (plen == 1) cidr = static_cast<uint8_t>(*pp);
                break;
            }

            attr = (struct nlattr*)((char*)attr + MNL_ALIGN(attr->nla_len));
            nest_len -= MNL_ALIGN(attr->nla_len);
        }

        if (ip_buf[0]) {
            ps.allowed_ip = std::string(ip_buf) + "/" + std::to_string(cidr);
        }
    }
};

}
