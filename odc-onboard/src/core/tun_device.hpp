#pragma once

#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace odc {

class TunDevice {
public:
    TunDevice() = default;

    ~TunDevice() {
        close();
    }

    TunDevice(const TunDevice&) = delete;
    TunDevice& operator=(const TunDevice&) = delete;

    bool create(const std::string& name, bool tap = false) {
        const char* dev = "/dev/net/tun";
        fd_ = open(dev, O_RDWR);
        if (fd_ < 0) {
            SPDLOG_ERROR("Failed to open {}: {}", dev, strerror(errno));
            return false;
        }

        struct ifreq ifr;
        std::memset(&ifr, 0, sizeof(ifr));
        ifr.ifr_flags = static_cast<short>((tap ? IFF_TAP : IFF_TUN) | IFF_NO_PI);

        if (!name.empty()) {
            std::strncpy(ifr.ifr_name, name.c_str(), IFNAMSIZ - 1);
        }

        if (ioctl(fd_, TUNSETIFF, static_cast<void*>(&ifr)) < 0) {
            SPDLOG_ERROR("Failed to create TUN device: {}", strerror(errno));
            close();
            return false;
        }

        name_ = ifr.ifr_name;
        SPDLOG_INFO("Created {} device: {} (fd={})", tap ? "TAP" : "TUN", name_, fd_);
        return true;
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd() const { return fd_; }
    const std::string& name() const { return name_; }
    bool is_open() const { return fd_ >= 0; }

private:
    int fd_{-1};
    std::string name_;
};

}
