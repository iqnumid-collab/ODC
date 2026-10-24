#pragma once

#include "core/event_bus.hpp"
#include "core/event.hpp"

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace odc {

class VpnInterfaceMonitor {
public:
    explicit VpnInterfaceMonitor(EventBus& bus)
        : bus_(bus) {}

    ~VpnInterfaceMonitor() {
        stop();
    }

    VpnInterfaceMonitor(const VpnInterfaceMonitor&) = delete;
    VpnInterfaceMonitor& operator=(const VpnInterfaceMonitor&) = delete;

    bool start() {
        if (running_.load()) {
            return false;
        }

        nl_sock_ = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
        if (nl_sock_ < 0) {
            return false;
        }

        struct sockaddr_nl addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.nl_family = AF_NETLINK;
        addr.nl_groups = RTNLGRP_LINK | RTNLGRP_IPV4_IFADDR | RTNLGRP_IPV6_IFADDR;

        if (bind(nl_sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(nl_sock_);
            nl_sock_ = -1;
            return false;
        }

        running_.store(true);
        monitor_thread_ = std::thread([this]() {
            monitor_loop();
        });

        return true;
    }

    void stop() {
        if (!running_.load()) {
            return;
        }

        running_.store(false);

        if (nl_sock_ >= 0) {
            shutdown(nl_sock_, SHUT_RDWR);
            close(nl_sock_);
            nl_sock_ = -1;
        }

        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }

    bool is_running() const { return running_.load(); }

    void track_connection(const std::string& connection_id, const std::string& iface_name) {
        std::unique_lock lock(track_mutex_);
        connection_to_iface_[connection_id] = iface_name;
        iface_to_connection_[iface_name] = connection_id;
    }

    void untrack_connection(const std::string& connection_id) {
        std::unique_lock lock(track_mutex_);
        auto it = connection_to_iface_.find(connection_id);
        if (it != connection_to_iface_.end()) {
            iface_to_connection_.erase(it->second);
            connection_to_iface_.erase(it);
        }
    }

    std::vector<std::pair<std::string, std::string>> get_tracked_interfaces() const {
        std::shared_lock lock(track_mutex_);
        std::vector<std::pair<std::string, std::string>> result;
        for (const auto& [conn, iface] : connection_to_iface_) {
            result.emplace_back(conn, iface);
        }
        return result;
    }

    struct InterfaceSnapshot {
        std::string name;
        std::string connection_id;
        bool is_up{false};
        std::vector<std::string> ipv4_addresses;
        std::vector<std::string> ipv6_addresses;
    };

    std::vector<InterfaceSnapshot> snapshot_all() const {
        std::shared_lock lock(track_mutex_);
        std::vector<InterfaceSnapshot> result;

        for (const auto& [conn_id, iface] : connection_to_iface_) {
            InterfaceSnapshot snap;
            snap.name = iface;
            snap.connection_id = conn_id;

            std::string sysfs_path = "/sys/class/net/" + iface;
            if (!std::filesystem::exists(sysfs_path)) {
                result.push_back(std::move(snap));
                continue;
            }

            std::string flags_path = sysfs_path + "/flags";
            std::ifstream flags_file(flags_path);
            if (flags_file) {
                unsigned long flags = 0;
                flags_file >> std::hex >> flags;
                snap.is_up = (flags & IFF_UP) != 0;
            }

            std::string addr_path = sysfs_path + "/addr";
            std::ifstream addr_file(addr_path);
            if (addr_file) {
                std::string mac;
                std::getline(addr_file, mac);
            }

            std::string inet6_path = sysfs_path + "/ipv6";
            if (std::filesystem::exists(inet6_path)) {
                for (const auto& entry : std::filesystem::directory_iterator(inet6_path)) {
                    if (entry.is_directory()) {
                        std::string addr_file_path = entry.path().string() + "/address";
                        std::ifstream af(addr_file_path);
                        if (af) {
                            std::string addr;
                            std::getline(af, addr);
                            snap.ipv6_addresses.push_back(addr);
                        }
                    }
                }
            }

            {
                std::lock_guard state_lock(state_mutex_);
                auto ait = iface_addresses_.find(iface);
                if (ait != iface_addresses_.end()) {
                    for (const auto& a : ait->second) {
                        bool is_v6 = (a.find(':') != std::string::npos);
                        if (is_v6) {
                            snap.ipv6_addresses.push_back(a);
                        } else {
                            snap.ipv4_addresses.push_back(a);
                        }
                    }
                }
                auto uit = iface_up_state_.find(iface);
                if (uit != iface_up_state_.end()) {
                    snap.is_up = uit->second;
                }
            }

            result.push_back(std::move(snap));
        }

        return result;
    }

private:
    void monitor_loop() {
        char buf[8192];

        while (running_.load()) {
            ssize_t len = recv(nl_sock_, buf, sizeof(buf), 0);
            if (len < 0) {
                if (errno == EINTR) continue;
                if (!running_.load()) break;
                continue;
            }

            for (struct nlmsghdr* nh = reinterpret_cast<struct nlmsghdr*>(buf);
                 NLMSG_OK(nh, static_cast<unsigned int>(len));
                 nh = NLMSG_NEXT(nh, len)) {

                switch (nh->nlmsg_type) {
                    case RTM_NEWLINK:
                    case RTM_DELLINK:
                        handle_link_event(nh);
                        break;
                    case RTM_NEWADDR:
                    case RTM_DELADDR:
                        handle_addr_event(nh);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    void handle_link_event(const nlmsghdr* nh) {
        struct ifinfomsg* ifi = reinterpret_cast<struct ifinfomsg*>(NLMSG_DATA(nh));
        std::string iface = iface_name_from_index(ifi->ifi_index);
        if (iface.empty()) return;

        std::string conn_id = find_connection_id_by_iface(iface);
        if (conn_id.empty()) return;

        VpnInterfaceEvent evt;
        evt.connection_id = conn_id;
        evt.interface_name = iface;

        bool is_up = (ifi->ifi_flags & IFF_UP) != 0;

        {
            std::lock_guard lock(state_mutex_);
            auto it = iface_up_state_.find(iface);
            if (it == iface_up_state_.end() || it->second != is_up) {
                iface_up_state_[iface] = is_up;
            } else {
                return;
            }
        }

        if (nh->nlmsg_type == RTM_NEWLINK) {
            if (is_up) {
                evt.action = VpnInterfaceAction::InterfaceUp;
            } else {
                evt.action = VpnInterfaceAction::InterfaceDown;
            }
        } else {
            evt.action = VpnInterfaceAction::InterfaceDeleted;
        }

        publish_event(std::move(evt));
    }

    void handle_addr_event(const nlmsghdr* nh) {
        struct ifaddrmsg* ifa = reinterpret_cast<struct ifaddrmsg*>(NLMSG_DATA(nh));
        std::string iface = iface_name_from_index(ifa->ifa_index);
        if (iface.empty()) return;

        std::string conn_id = find_connection_id_by_iface(iface);
        if (conn_id.empty()) return;

        int attr_len = IFA_PAYLOAD(nh);
        std::string ip_str;

        for (struct rtattr* rta = IFA_RTA(ifa);
             RTA_OK(rta, attr_len);
             rta = RTA_NEXT(rta, attr_len)) {

            if (rta->rta_type == IFA_LOCAL || rta->rta_type == IFA_ADDRESS) {
                if (ifa->ifa_family == AF_INET) {
                    char buf[INET_ADDRSTRLEN];
                    struct in_addr* addr = reinterpret_cast<struct in_addr*>(RTA_DATA(rta));
                    if (inet_ntop(AF_INET, addr, buf, sizeof(buf))) {
                        ip_str = buf;
                    }
                } else if (ifa->ifa_family == AF_INET6) {
                    char buf[INET6_ADDRSTRLEN];
                    struct in6_addr* addr = reinterpret_cast<struct in6_addr*>(RTA_DATA(rta));
                    if (inet_ntop(AF_INET6, addr, buf, sizeof(buf))) {
                        ip_str = buf;
                    }
                }
                break;
            }
        }

        if (ip_str.empty()) return;

        VpnInterfaceEvent evt;
        evt.connection_id = conn_id;
        evt.interface_name = iface;
        evt.ip_address = ip_str;
        evt.prefix_len = ifa->ifa_prefixlen;
        evt.ipv6 = (ifa->ifa_family == AF_INET6);

        {
            std::lock_guard lock(state_mutex_);
            auto& addrs = iface_addresses_[iface];
            if (nh->nlmsg_type == RTM_NEWADDR) {
                evt.action = VpnInterfaceAction::AddressAdded;
                bool found = false;
                for (const auto& a : addrs) {
                    if (a == ip_str) { found = true; break; }
                }
                if (!found) {
                    addrs.push_back(ip_str);
                } else {
                    return;
                }
            } else {
                evt.action = VpnInterfaceAction::AddressRemoved;
                addrs.erase(
                    std::remove(addrs.begin(), addrs.end(), ip_str),
                    addrs.end()
                );
            }
        }

        publish_event(std::move(evt));
    }

    void publish_event(VpnInterfaceEvent evt) {
        bus_.publish(Event{std::move(evt)});
    }

    std::string find_connection_id_by_iface(const std::string& iface) const {
        std::shared_lock lock(track_mutex_);
        auto it = iface_to_connection_.find(iface);
        if (it != iface_to_connection_.end()) {
            return it->second;
        }
        return {};
    }

    std::string iface_name_from_index(uint32_t index) const {
        char buf[IFNAMSIZ];
        if (if_indextoname(index, buf)) {
            return std::string(buf);
        }
        return {};
    }

    EventBus& bus_;
    int nl_sock_{-1};
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;

    mutable std::shared_mutex track_mutex_;
    std::unordered_map<std::string, std::string> connection_to_iface_;
    std::unordered_map<std::string, std::string> iface_to_connection_;

    mutable std::mutex state_mutex_;
    std::unordered_map<std::string, bool> iface_up_state_;
    std::unordered_map<std::string, std::vector<std::string>> iface_addresses_;
};

}
