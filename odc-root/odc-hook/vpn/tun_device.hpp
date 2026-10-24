#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace odc::hook {

struct TunConfig {
    std::string name;
    std::string ip_address;
    int prefix_len{24};
    int mtu{1420};
};

class TunDevice {
public:
    using ReadCallback = std::function<void(const uint8_t* data, size_t len)>;

    TunDevice();
    ~TunDevice();
    TunDevice(const TunDevice&) = delete;
    TunDevice& operator=(const TunDevice&) = delete;
    TunDevice(TunDevice&&) noexcept;
    TunDevice& operator=(TunDevice&&) noexcept;

    bool open(const TunConfig& cfg);
    void close();
    bool write(const uint8_t* data, size_t len);
    bool bring_up();
    bool set_ip();
    bool set_mtu(int mtu);

    int native_handle() const;
    const std::string& name() const;
    bool is_open() const;

    // Byte counters for cross-platform statistics
    uint64_t tx_bytes() const;
    uint64_t rx_bytes() const;
    void add_rx_bytes(uint64_t n);

#if defined(_WIN32)
    // Windows wintun read packet (separate from native_handle which returns fd on Unix)
    bool read_packet(std::vector<uint8_t>& out);
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace odc::hook
