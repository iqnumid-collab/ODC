#include "tun_device.hpp"

#include <cstring>
#include <stdexcept>
#include <string>
#include <spdlog/spdlog.h>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <unistd.h>
// Prevent linux/if.h conflict with net/if.h on newer kernels
#ifndef _LINUX_IF_H
#include <linux/if_tun.h>
#endif
#elif defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/socket.h>
#include <sys/sys_domain.h>
#include <net/if.h>
#include <net/if_utun.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace odc::hook {

#if defined(__linux__) || defined(__APPLE__)
struct TunDevice::Impl {
    int fd{-1};
    std::string name;
    TunConfig cfg;
    uint64_t tx_bytes{0};
    uint64_t rx_bytes{0};
};
#elif defined(_WIN32)
struct WintunApi;
struct TunDevice::Impl {
    std::string name;
    TunConfig cfg;
    WintunApi* api{nullptr};
    WINTUN_ADAPTER_HANDLE adapter{nullptr};
    WINTUN_SESSION_HANDLE session{nullptr};
    uint64_t tx_bytes{0};
    uint64_t rx_bytes{0};
};
#endif

uint64_t TunDevice::tx_bytes() const { return impl_->tx_bytes; }
uint64_t TunDevice::rx_bytes() const { return impl_->rx_bytes; }
void TunDevice::add_rx_bytes(uint64_t n) { impl_->rx_bytes += n; }

// ── Linux implementation: /dev/net/tun ────────────────────────────────────
#if defined(__linux__)

TunDevice::TunDevice() : impl_(std::make_unique<Impl>()) {}
TunDevice::~TunDevice() { close(); }
TunDevice::TunDevice(TunDevice&&) noexcept = default;
TunDevice& TunDevice::operator=(TunDevice&&) noexcept = default;

bool TunDevice::open(const TunConfig& cfg) {
    impl_->cfg = cfg;

    int fd = ::open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        SPDLOG_ERROR("TunDevice: failed to open /dev/net/tun: {}", strerror(errno));
        return false;
    }

    struct ifreq ifr{};
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (!cfg.name.empty()) {
        std::strncpy(ifr.ifr_name, cfg.name.c_str(), IFNAMSIZ - 1);
    }

    if (::ioctl(fd, TUNSETIFF, static_cast<void*>(&ifr)) < 0) {
        SPDLOG_ERROR("TunDevice: TUNSETIFF ioctl failed: {}", strerror(errno));
        ::close(fd);
        return false;
    }

    impl_->fd = fd;
    impl_->name = ifr.ifr_name;
    SPDLOG_INFO("TunDevice: created {} (fd={})", impl_->name, impl_->fd);
    return true;
}

void TunDevice::close() {
    if (impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
        impl_->name.clear();
    }
}

bool TunDevice::write(const uint8_t* data, size_t len) {
    if (impl_->fd < 0) return false;
    ssize_t n = ::write(impl_->fd, data, len);
    if (n == static_cast<ssize_t>(len)) {
        impl_->tx_bytes += len;
        return true;
    }
    return false;
}

bool TunDevice::bring_up() {
    if (impl_->name.empty()) return false;

    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        SPDLOG_ERROR("TunDevice: socket() for bring_up failed: {}", strerror(errno));
        return false;
    }

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, impl_->name.c_str(), IFNAMSIZ - 1);

    if (::ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        SPDLOG_ERROR("TunDevice: SIOCGIFFLAGS failed: {}", strerror(errno));
        ::close(sock);
        return false;
    }

    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (::ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
        SPDLOG_ERROR("TunDevice: SIOCSIFFLAGS failed: {}", strerror(errno));
        ::close(sock);
        return false;
    }

    ::close(sock);
    return true;
}

bool TunDevice::set_ip() {
    if (impl_->name.empty() || impl_->cfg.ip_address.empty()) return false;

    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, impl_->name.c_str(), IFNAMSIZ - 1);

    struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(&ifr.ifr_addr);
    addr->sin_family = AF_INET;
    ::inet_pton(AF_INET, impl_->cfg.ip_address.c_str(), &addr->sin_addr);

    if (::ioctl(sock, SIOCSIFADDR, &ifr) < 0) {
        SPDLOG_WARN("TunDevice: SIOCSIFADDR failed for {}: {}", impl_->cfg.ip_address, strerror(errno));
        ::close(sock);
        return false;
    }

    struct sockaddr_in mask{};
    mask.sin_family = AF_INET;
    mask.sin_addr.s_addr = htonl(0xFFFFFFFF << (32 - impl_->cfg.prefix_len));
    std::memcpy(&ifr.ifr_netmask, &mask, sizeof(mask));

    ::ioctl(sock, SIOCSIFNETMASK, &ifr);
    ::close(sock);
    return true;
}

bool TunDevice::set_mtu(int mtu) {
    if (impl_->name.empty()) return false;

    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, impl_->name.c_str(), IFNAMSIZ - 1);
    ifr.ifr_mtu = mtu;

    if (::ioctl(sock, SIOCSIFMTU, &ifr) < 0) {
        SPDLOG_WARN("TunDevice: SIOCSIFMTU failed: {}", strerror(errno));
        ::close(sock);
        return false;
    }

    ::close(sock);
    return true;
}

int TunDevice::native_handle() const { return impl_->fd; }
const std::string& TunDevice::name() const { return impl_->name; }
bool TunDevice::is_open() const { return impl_->fd >= 0; }

// ── Windows implementation: wintun.dll (runtime-loaded) ─────────────────
#elif defined(_WIN32)

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

// Wintun opaque handles
using WINTUN_ADAPTER_HANDLE = void*;
using WINTUN_SESSION_HANDLE   = void*;

// Function pointer types for runtime GetProcAddress
using WintunCreateAdapterFn       = WINTUN_ADAPTER_HANDLE (__stdcall *)(const wchar_t*, const wchar_t*, const GUID*);
using WintunOpenAdapterFn         = WINTUN_ADAPTER_HANDLE (__stdcall *)(const wchar_t*);
using WintunCloseAdapterFn        = void (__stdcall *)(WINTUN_ADAPTER_HANDLE);
using WintunStartSessionFn        = WINTUN_SESSION_HANDLE (__stdcall *)(WINTUN_ADAPTER_HANDLE, DWORD);
using WintunEndSessionFn          = void (__stdcall *)(WINTUN_SESSION_HANDLE);
using WintunGetReadWaitEventFn    = HANDLE (__stdcall *)(WINTUN_SESSION_HANDLE);
using WintunReceivePacketFn       = BYTE* (__stdcall *)(WINTUN_SESSION_HANDLE, DWORD*);
using WintunReleaseReceivePacketFn = void (__stdcall *)(WINTUN_SESSION_HANDLE, const BYTE*);
using WintunAllocateSendPacketFn  = BYTE* (__stdcall *)(WINTUN_SESSION_HANDLE, DWORD);
using WintunSendPacketFn          = void (__stdcall *)(WINTUN_SESSION_HANDLE, const BYTE*);

struct WintunApi {
    HMODULE dll{nullptr};
    WintunCreateAdapterFn        create_adapter{nullptr};
    WintunOpenAdapterFn          open_adapter{nullptr};
    WintunCloseAdapterFn         close_adapter{nullptr};
    WintunStartSessionFn         start_session{nullptr};
    WintunEndSessionFn           end_session{nullptr};
    WintunGetReadWaitEventFn     get_read_wait_event{nullptr};
    WintunReceivePacketFn        receive_packet{nullptr};
    WintunReleaseReceivePacketFn release_receive_packet{nullptr};
    WintunAllocateSendPacketFn   allocate_send_packet{nullptr};
    WintunSendPacketFn           send_packet{nullptr};

    bool load() {
        dll = LoadLibraryW(L"wintun.dll");
        if (!dll) return false;
        #define LOAD(fn) fn = reinterpret_cast<fn##Fn>(GetProcAddress(dll, #fn)); \
                         if (!fn) { FreeLibrary(dll); dll = nullptr; return false; }
        LOAD(WintunCreateAdapter)
        LOAD(WintunOpenAdapter)
        LOAD(WintunCloseAdapter)
        LOAD(WintunStartSession)
        LOAD(WintunEndSession)
        LOAD(WintunGetReadWaitEvent)
        LOAD(WintunReceivePacket)
        LOAD(WintunReleaseReceivePacket)
        LOAD(WintunAllocateSendPacket)
        LOAD(WintunSendPacket)
        #undef LOAD
        return true;
    }

    void unload() {
        if (dll) { FreeLibrary(dll); dll = nullptr; }
    }
};

static std::string wide_to_utf8(const wchar_t* wstr) {
    if (!wstr) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, nullptr, nullptr);
    return result;
}

static std::wstring utf8_to_wide(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
    return result;
}

static std::string mask_from_prefix(int prefix) {
    uint32_t m = htonl(0xFFFFFFFFu << (32 - prefix));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
                  (m >> 24) & 0xFF, (m >> 16) & 0xFF,
                  (m >> 8) & 0xFF, m & 0xFF);
    return buf;
}

TunDevice::TunDevice() : impl_(std::make_unique<Impl>()) {}
TunDevice::~TunDevice() { close(); }
TunDevice::TunDevice(TunDevice&&) noexcept = default;
TunDevice& TunDevice::operator=(TunDevice&&) noexcept = default;

bool TunDevice::open(const TunConfig& cfg) {
    impl_->cfg = cfg;

    // Load wintun.dll at runtime — must be placed next to odc-hook.exe
    auto* api = new WintunApi();
    if (!api->load()) {
        SPDLOG_ERROR("TunDevice: wintun.dll not found — place wintun.dll next to odc-hook.exe");
        delete api;
        return false;
    }
    impl_->api = api;

    std::wstring wname = utf8_to_wide(cfg.name.empty() ? "odc-hook" : cfg.name);

    impl_->adapter = api->create_adapter(wname.c_str(), L"ODC VPN", nullptr);
    if (!impl_->adapter) {
        impl_->adapter = api->open_adapter(wname.c_str());
        if (!impl_->adapter) {
            SPDLOG_ERROR("TunDevice: failed to create/open wintun adapter '{}'", cfg.name);
            api->unload();
            delete api;
            impl_->api = nullptr;
            return false;
        }
    }

    const DWORD kCapacity = 0x400000; // 4 MB ring buffer
    impl_->session = api->start_session(impl_->adapter, kCapacity);
    if (!impl_->session) {
        api->close_adapter(impl_->adapter);
        impl_->adapter = nullptr;
        api->unload();
        delete api;
        impl_->api = nullptr;
        return false;
    }

    impl_->name = cfg.name.empty() ? "odc-hook" : cfg.name;
    impl_->cfg = cfg;
    SPDLOG_INFO("TunDevice: created wintun adapter '{}'", impl_->name);
    return true;
}

void TunDevice::close() {
    if (impl_->api) {
        if (impl_->session) {
            impl_->api->end_session(impl_->session);
            impl_->session = nullptr;
        }
        if (impl_->adapter) {
            impl_->api->close_adapter(impl_->adapter);
            impl_->adapter = nullptr;
        }
        impl_->api->unload();
        delete impl_->api;
        impl_->api = nullptr;
    }
    impl_->name.clear();
}

bool TunDevice::write(const uint8_t* data, size_t len) {
    if (!impl_->api || !impl_->session) return false;
    DWORD sz = static_cast<DWORD>(len);
    auto* pkt = impl_->api->allocate_send_packet(impl_->session, sz);
    if (!pkt) return false;
    std::memcpy(pkt, data, len);
    impl_->api->send_packet(impl_->session, pkt);
    impl_->tx_bytes += len;
    return true;
}

bool TunDevice::bring_up() {
    // Wintun adapters are automatically up when session is started
    return impl_->session != nullptr;
}

bool TunDevice::set_ip() {
    if (impl_->name.empty() || impl_->cfg.ip_address.empty()) return false;
    std::string cmd = "netsh interface ip set address \"" +
                      impl_->name + "\" static " +
                      impl_->cfg.ip_address + " " +
                      mask_from_prefix(impl_->cfg.prefix_len);
    return std::system(cmd.c_str()) == 0;
}

bool TunDevice::set_mtu(int mtu) {
    if (impl_->name.empty()) return false;
    std::string cmd = "netsh interface ipv4 set subinterface \"" +
                      impl_->name + "\" mtu=" + std::to_string(mtu) +
                      " store=persistent";
    return std::system(cmd.c_str()) == 0;
}

int TunDevice::native_handle() const {
    if (!impl_->api || !impl_->session) return -1;
    HANDLE ev = impl_->api->get_read_wait_event(impl_->session);
    // Return as int for compatibility; callers should use read_event() for HANDLE
    return -1;
}

bool TunDevice::read_packet(std::vector<uint8_t>& out) {
    if (!impl_->api || !impl_->session) return false;
    DWORD sz = 0;
    auto* pkt = impl_->api->receive_packet(impl_->session, &sz);
    if (!pkt) return false;
    out.assign(pkt, pkt + sz);
    impl_->rx_bytes += sz;
    impl_->api->release_receive_packet(impl_->session, pkt);
    return true;
}

const std::string& TunDevice::name() const { return impl_->name; }
bool TunDevice::is_open() const { return impl_->session != nullptr; }

// ── macOS implementation: utun ────────────────────────────────────────────
#elif defined(__APPLE__)

TunDevice::TunDevice() : impl_(std::make_unique<Impl>()) {}
TunDevice::~TunDevice() { close(); }
TunDevice::TunDevice(TunDevice&&) noexcept = default;
TunDevice& TunDevice::operator=(TunDevice&&) noexcept = default;

bool TunDevice::open(const TunConfig& cfg) {
    impl_->cfg = cfg;

    int fd = ::socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0) {
        SPDLOG_ERROR("TunDevice: socket(PF_SYSTEM) failed: {}", strerror(errno));
        return false;
    }

    struct ctl_info ci{};
    std::strncpy(ci.ctl_name, UTUN_CONTROL_NAME, sizeof(ci.ctl_name) - 1);

    if (::ioctl(fd, CTLIOCGINFO, &ci) < 0) {
        SPDLOG_ERROR("TunDevice: CTLIOCGINFO failed: {}", strerror(errno));
        ::close(fd);
        return false;
    }

    struct sockaddr_ctl sc{};
    sc.sc_len = sizeof(sc);
    sc.sc_family = AF_SYSTEM;
    sc.ss_sysaddr = AF_SYS_CONTROL;
    sc.sc_id = ci.ctl_id;
    sc.sc_unit = 0;

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&sc), sizeof(sc)) < 0) {
        SPDLOG_ERROR("TunDevice: connect(utun) failed: {}", strerror(errno));
        ::close(fd);
        return false;
    }

    char ifname[IFNAMSIZ]{};
    socklen_t ifname_len = sizeof(ifname);
    if (::getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, ifname, &ifname_len) < 0) {
        SPDLOG_ERROR("TunDevice: getsockopt(UTUN_OPT_IFNAME) failed: {}", strerror(errno));
        ::close(fd);
        return false;
    }

    impl_->fd = fd;
    impl_->name = ifname;
    SPDLOG_INFO("TunDevice: created {} (fd={})", impl_->name, impl_->fd);
    return true;
}

void TunDevice::close() {
    if (impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
        impl_->name.clear();
    }
}

bool TunDevice::write(const uint8_t* data, size_t len) {
    if (impl_->fd < 0) return false;
    ssize_t n = ::write(impl_->fd, data, len);
    if (n == static_cast<ssize_t>(len)) {
        impl_->tx_bytes += len;
        return true;
    }
    return false;
}

bool TunDevice::bring_up() {
    if (impl_->name.empty()) return false;
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;
    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, impl_->name.c_str(), IFNAMSIZ - 1);
    ::ioctl(sock, SIOCGIFFLAGS, &ifr);
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    ::ioctl(sock, SIOCSIFFLAGS, &ifr);
    ::close(sock);
    return true;
}

bool TunDevice::set_ip() {
    if (impl_->name.empty() || impl_->cfg.ip_address.empty()) return false;
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;
    struct ifaliasreq ifra{};
    std::strncpy(ifra.ifra_name, impl_->name.c_str(), IFNAMSIZ - 1);
    struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(&ifra.ifra_addr);
    addr->sin_family = AF_INET;
    addr->sin_len = sizeof(struct sockaddr_in);
    ::inet_pton(AF_INET, impl_->cfg.ip_address.c_str(), &addr->sin_addr);
    struct sockaddr_in* mask = reinterpret_cast<struct sockaddr_in*>(&ifra.ifra_mask);
    mask->sin_family = AF_INET;
    mask->sin_len = sizeof(struct sockaddr_in);
    mask->sin_addr.s_addr = htonl(0xFFFFFFFF << (32 - impl_->cfg.prefix_len));
    if (::ioctl(sock, SIOCAIFADDR, &ifra) < 0) {
        SPDLOG_WARN("TunDevice: SIOCAIFADDR failed: {}", strerror(errno));
        ::close(sock);
        return false;
    }
    ::close(sock);
    return true;
}

bool TunDevice::set_mtu(int mtu) {
    if (impl_->name.empty()) return false;
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;
    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, impl_->name.c_str(), IFNAMSIZ - 1);
    ifr.ifr_mtu = mtu;
    ::ioctl(sock, SIOCSIFMTU, &ifr);
    ::close(sock);
    return true;
}

int TunDevice::native_handle() const { return impl_->fd; }
const std::string& TunDevice::name() const { return impl_->name; }
bool TunDevice::is_open() const { return impl_->fd >= 0; }

#else
#error "Unsupported platform: TUN device not implemented for this OS. Supported: Linux (/dev/net/tun), Windows (wintun.dll), macOS (utun)."
#endif

} // namespace odc::hook
