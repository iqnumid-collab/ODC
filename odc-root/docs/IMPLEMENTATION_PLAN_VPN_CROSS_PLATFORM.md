# odc-hook — Cross-Platform VPN Implementation Plan

**Date**: 2026-07-14
**Goal**: Make the odc-hook VPN subsystem (WireGuard + OpenVPN clients) compile and function correctly on all three target platforms: Linux, macOS, and Windows. Eliminate all conditional-compilation bugs, fix broken CMake build targets, complete the Windows TUN stub via wintun.dll, and wire up cross-compilation Docker containers with correct linking.

**Status pre-fix**: VPN subsystem has 6 source-level conditional-compilation bugs (3 block macOS, 3 block Windows), 3 broken CMake targets (`openvpn3_client`, `wg_crypto`, `sodium::sodium` alias), and a non-functional Windows TUN stub. All currently masked because `BUILD_ODC_HOOK_VPN` defaults to `OFF`.

---

## Table of Contents

1. [Pre-Flight: Full Audit Findings](#pre-flight-full-audit-findings)
2. [Phase 1 — Fix Source Code Conditional-Compilation Bugs](#phase-1--fix-source-code-conditional-compilation-bugs)
   - [Task 1.1 — Fix `wireguard_client.hpp` type declarations for non-Linux](#task-11--fix-wireguard_clienthpp-type-declarations-for-non-linux)
   - [Task 1.2 — Guard POSIX-only headers in `wireguard_client.cpp`](#task-12--guard-posix-only-headers-in-wireguard_clientcpp)
   - [Task 1.3 — Guard POSIX-only headers in `wg_device.cpp`](#task-13--guard-posix-only-headers-in-wg_devicecpp)
   - [Task 1.4 — Add `#else`/`#error` to `tun_device.cpp` for unsupported platforms](#task-14--add-elseerror-to-tun_devicecpp-for-unsupported-platforms)
   - [Task 1.5 — Replace `/sys/class/net/` stats with platform-portable TunDevice counters](#task-15--replace-sysclassnet-stats-with-platform-portable-tundevice-counters)
   - [Task 1.6 — Fix `openvpn_client.cpp` stats path for cross-platform](#task-16--fix-openvpn_clientcpp-stats-path-for-cross-platform)
3. [Phase 2 — Fix CMake Build System](#phase-2--fix-cmake-build-system)
   - [Task 2.1 — Add `thirdparty/minimal-openvpn3` to the build + create `openvpn3_client` alias](#task-21--add-thirdpartyminimal-openvpn3-to-the-build--create-openvpn3_client-alias)
   - [Task 2.2 — Remove vestigial `wg_crypto` link dependency](#task-22--remove-vestigial-wg_crypto-link-dependency)
   - [Task 2.3 — Fix `sodium::sodium` alias for system libsodium case](#task-23--fix-sodiumsodium-alias-for-system-libsodium-case)
   - [Task 2.4 — Add include paths for OpenVPN3 headers to odc-hook](#task-24--add-include-paths-for-openvpn3-headers-to-odc-hook)
4. [Phase 3 — Implement Windows TUN via wintun.dll](#phase-3--implement-windows-tun-via-wintundll)
   - [Task 3.1 — Runtime dynamic loading of wintun.dll](#task-31--runtime-dynamic-loading-of-wintundll)
   - [Task 3.2 — Implement TunDevice::open() for Windows](#task-32--implement-tundeviceopen-for-windows)
   - [Task 3.3 — Implement remaining TunDevice methods for Windows](#task-33--implement-remaining-tundevice-methods-for-windows)
   - [Task 3.4 — Bundle wintun.dll in Windows Docker output](#task-34--bundle-wintundll-in-windows-docker-output)
5. [Phase 4 — Fix WireGuard Userspace IP Routing for Windows](#phase-4--fix-wireguard-userspace-ip-routing-for-windows)
   - [Task 4.1 — Replace `struct iphdr` with portable inline struct in `wg_device.cpp`](#task-41--replace-struct-iphdr-with-portable-inline-struct-in-wg_devicecpp)
6. [Phase 5 — Cross-Compilation Docker Updates](#phase-5--cross-compilation-docker-updates)
   - [Task 5.1 — Windows Dockerfile: Enable VPN build + wintun + libsodium](#task-51--windows-dockerfile-enable-vpn-build--wintun--libsodium)
   - [Task 5.2 — macOS Dockerfile: Enable VPN build + libsodium](#task-52--macos-dockerfile-enable-vpn-build--libsodium)
   - [Task 5.3 — Linux Dockerfile: Add libsodium-dev + enable VPN](#task-53--linux-dockerfile-add-libsodium-dev--enable-vpn)
7. [Phase 6 — Verification per Platform](#phase-6--verification-per-platform)
   - [Task 6.1 — Linux: build + test with VPN enabled](#task-61--linux-build--test-with-vpn-enabled)
   - [Task 6.2 — macOS: cross-compile + header audit](#task-62--macos-cross-compile--header-audit)
   - [Task 6.3 — Windows: cross-compile + header audit](#task-63--windows-cross-compile--header-audit)
   - [Task 6.4 — Runtime smoke tests](#task-64--runtime-smoke-tests)
8. [Task Execution Order & Dependencies](#task-execution-order--dependencies)
9. [Estimated Effort](#estimated-effort)
10. [Platform Capability Matrix](#platform-capability-matrix)

---

## Pre-Flight: Full Audit Findings

### Source Code Bugs (6)

| # | File | Line(s) | Bug | Platform Impact |
|---|---|---|---|---|
| 1 | `wireguard_client.hpp` | 42-47 | Non-Linux `#else` branch declares `class WgDevice` (wrong namespace) and `std::unique_ptr<wg::TunDevice>` (non-existent type). `WgDevice` lives in namespace `wg`, `TunDevice` lives in `odc::hook` | Blocks macOS + Windows build |
| 2 | `wireguard_client.cpp` | 14-15 | Unconditional `#include <arpa/inet.h>` and `#include <netdb.h>` outside any `#if` guard. POSIX-only headers | Blocks Windows build |
| 3 | `wg_device.cpp` | 5 | Unconditional `#include <netinet/ip.h>`. POSIX-only header | Blocks Windows build |
| 4 | `tun_device.cpp` | 317-318 | No `#else` or `#error` after `#elif defined(__APPLE__)` block. Unsupported platforms produce silent undefined behavior | All unsupported platforms |
| 5 | `openvpn_client.cpp` | 227-242 | `std::string base = "/sys/class/net/" + interface_name_ + "/statistics/"`. Linux procfs | Stats fail silently on macOS + Windows |
| 6 | `tun_device.cpp` | 174-196 | Windows `#elif defined(_WIN32)` is a complete stub — `open()` returns `false`, all methods are no-ops | Blocks all VPN on Windows |

### CMake Build System Bugs (3)

| # | File | Line | Bug | Impact |
|---|---|---|---|---|
| 7 | `odc-hook/CMakeLists.txt` | 21 | Links `openvpn3_client` target. This target does not exist in the odc-root build tree. `thirdparty/minimal-openvpn3/CMakeLists.txt` defines `openvpn3` (different name) and is never included via `add_subdirectory()` | Blocks VPN build when `BUILD_ODC_HOOK_VPN=ON` |
| 8 | `odc-hook/CMakeLists.txt` | 22 | Links `wg_crypto` target. This target is never defined anywhere in the repository | Blocks VPN build when `BUILD_ODC_HOOK_VPN=ON` |
| 9 | `CMakeLists.txt` (top-level) | 90-102 | `sodium::sodium` alias is created only inside `if(NOT sodium_FOUND)`. If system `libsodium-dev` is installed and `find_package()` succeeds, the alias is never created | Fragile — works only when system libsodium is absent |

### Research: Platform WireGuard/OpenVPN Architecture

**WireGuard upstream design** (from wireguard.com):
- **Linux**: Kernel module (`wireguard.ko`) accessed via generic netlink — the odc-hook uses `wg_netlink.hpp` for this, correct.
- **macOS**: `wireguard-go` userspace daemon + `utun` driver. Communicates via `/var/run/wireguard/wg0.sock` Unix socket with text-based `get=1`/`set=1` protocol.
- **Windows**: `wireguard-go` + `wintun.dll`. Communicates via named pipe `\\.\pipe\WireGuard\wg0` with same text-based protocol.
- **All non-Linux**: WireGuard crypto (Noise IKpsk2, Chacha20Poly1305) is identical — odc-hook already implements this in `wg_noise.cpp` + `wg_aead.cpp`.

**OpenVPN3 upstream design** (from github.com/OpenVPN/openvpn3):
- Officially cross-platform (Linux, macOS, Windows). Uses `TunBuilderBase` virtual interface for TUN creation.
- On **Windows**: supports wintun and tap-windows6 drivers. The vendored `thirdparty/minimal-openvpn3/openvpn/tun/win/` directory already has ring buffer + wintun client code.
- On **macOS**: uses utun driver (integrated, no external driver needed).
- On **Linux**: uses `/dev/net/tun` (standard).
- Builds on Windows via Visual Studio 2022 + vcpkg OR MinGW-w64 + vcpkg.

**Wintun** (from wintun.net):
- Signed kernel TUN driver for Windows from the WireGuard project.
- Provided as `wintun.dll` + `wintun.h`. Can be bundled with applications.
- API: `WintunCreateAdapter`, `WintunOpenAdapter`, `WintunStartSession`, `WintunReceivePacket`, `WintunAllocateSendPacket`, `WintunSendPacket`, `WintunReleaseReceivePacket`, `WintunEndSession`, `WintunCloseAdapter`, `WintunGetReadWaitEvent`.
- Ring buffer implementation for high-performance I/O is already present in the vendored OpenVPN3 code at `thirdparty/minimal-openvpn3/openvpn/tun/win/ringbuffer.hpp`.

### Existing Cross-Compilation Docker State

| Dockerfile | Platforms Built | VPN Enabled? | OpenSSL | libsodium |
|---|---|---|---|---|
| `deploy/linux/Dockerfile` | Native Linux (gcc-12) | No | System `libssl-dev` (3.0.x) | Not installed |
| `deploy/windows/Dockerfile` | MinGW-w64 cross-compile | No | Cross-compiled 3.2.1 static | Not compiled |
| `deploy/macos/Dockerfile` | osxcross cross-compile | No | Cross-compiled 3.2.1 static | Not compiled |

---

## Phase 1 — Fix Source Code Conditional-Compilation Bugs

### Task 1.1 — Fix `wireguard_client.hpp` type declarations for non-Linux

**File**: `odc-hook/vpn/wireguard_client.hpp`, lines 38-48

**Current state** (buggy):
```cpp
#if defined(__linux__)
    // Linux: use kernel WireGuard via netlink
    class WgNetlinkAdapter;
    std::unique_ptr<WgNetlinkAdapter> netlink_;
#else
    // Windows/macOS: use userspace WgDevice
    class TunDevice;
    class WgDevice;
    std::unique_ptr<wg::TunDevice> tun_;   // BUG: wg::TunDevice does NOT exist
    std::unique_ptr<WgDevice> device_;     // BUG: WgDevice is odc::hook::WgDevice, not odc::hook::wg::WgDevice
#endif
```

**Root cause**: `TunDevice` is declared in namespace `odc::hook` (no `wg` sub-namespace). `WgDevice` is declared in namespace `odc::hook::wg`. The forward declarations resolve to the wrong namespaces, and the `unique_ptr` type parameters don't match.

**Fix**:
```cpp
#if defined(__linux__)
    // Linux: use kernel WireGuard via netlink
    class WgNetlinkAdapter;
    std::unique_ptr<WgNetlinkAdapter> netlink_;
#else
    // Windows/macOS: use userspace WgDevice + TunDevice
    class TunDevice;
    namespace wg { class WgDevice; }
    std::unique_ptr<TunDevice> tun_;
    std::unique_ptr<wg::WgDevice> device_;
#endif
```

**Verification**: Compile `wireguard_client.cpp` on macOS target (`-target x86_64-apple-darwin`). The `WgDevice` forward declaration inside `namespace wg { }` resolves to `odc::hook::wg::WgDevice` which matches the actual class location in `wg_device.hpp`. The `TunDevice` forward declaration resolves to `odc::hook::TunDevice` which is correct.

---

### Task 1.2 — Guard POSIX-only headers in `wireguard_client.cpp`

**File**: `odc-hook/vpn/wireguard_client.cpp`, lines 14-15

**Current state** (buggy):
```cpp
#include <arpa/inet.h>
#include <netdb.h>
```

These are compiled on Windows where `<arpa/inet.h>` and `<netdb.h>` do not exist. On Windows, network functions come from `<winsock2.h>` and `<ws2tcpip.h>`.

**Analysis of usage**: 
- `inet_pton()` from `<arpa/inet.h>` is used inside `resolve_endpoint()` at line ~80, which is ONLY called from the `#if defined(__linux__)` kernel netlink branch.
- `getaddrinfo()` from `<netdb.h>` is also used in `resolve_endpoint()`.
- The `#else` (non-Linux) branch in `wireguard_client.cpp` does NOT call `resolve_endpoint()` — it uses ASIO's built-in resolver instead (which handles platform differences).

**Fix**:
```cpp
#if defined(__linux__)
#include <arpa/inet.h>
#include <netdb.h>
#endif
```

Add these two lines immediately inside the existing `#if defined(__linux__)` block that starts at line 3, alongside the existing `#include "../../thirdparty/wg_netlink.hpp"`. This ensures they're only compiled on Linux.

**Verification**: Compile on MinGW-w64 target — no "file not found" errors.

---

### Task 1.3 — Guard POSIX-only headers in `wg_device.cpp`

**File**: `odc-hook/vpn/wg_device.cpp`, line 5

**Current state** (buggy):
```cpp
#include <netinet/ip.h>
```

This provides `struct iphdr` used in `find_peer_for_ip()` (lines 221-237) for IP header parsing to route packets to the correct peer based on destination address.

**Analysis**: The WG device needs to inspect the destination IP of each packet read from the TUN interface to determine which peer to route it to. `struct iphdr` from `<netinet/ip.h>` is used to parse the IPv4 header. This header does not exist on Windows.

**Fix — Two-step approach**:

**Step A** — Guard the include:
```cpp
#if !defined(_WIN32)
#include <netinet/ip.h>
#endif
```

**Step B** — Replace the dependency on `struct iphdr` with a portable inline struct definition. Add this inside `wg_device.cpp`, in the anonymous namespace, before `find_peer_for_ip()`:

```cpp
// Portable IPv4 header definition (avoids platform-specific <netinet/ip.h>)
#pragma pack(push, 1)
struct portable_ipv4_header {
    uint8_t  version_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_length_be;
    uint16_t identification_be;
    uint16_t flags_fragment_be;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum_be;
    uint8_t  source_ip[4];
    uint8_t  dest_ip[4];

    uint8_t ihl() const { return version_ihl & 0x0F; }
    uint8_t version() const { return (version_ihl >> 4) & 0x0F; }
};
#pragma pack(pop)
```

**Step C** — Update `find_peer_for_ip()` to use `portable_ipv4_header` instead of `struct iphdr`:

```cpp
// OLD:
const auto* ip = reinterpret_cast<const iphdr*>(data);

// NEW:
const auto* ip = reinterpret_cast<const portable_ipv4_header*>(data);
// dest_ip is now ip->dest_ip (4 bytes) — convert with memcpy to avoid alignment issues:
uint32_t dest;
std::memcpy(&dest, ip->dest_ip, 4);
dest = ntohl(dest);
```

This eliminates the platform dependency entirely.

**Verification**: Compile on all three platforms. `find_peer_for_ip()` unit test (if available) or runtime smoke test.

---

### Task 1.4 — Add `#else`/`#error` to `tun_device.cpp` for unsupported platforms

**File**: `odc-hook/vpn/tun_device.cpp`, after line 317 (closing `#endif` of the `#elif defined(__APPLE__)` block)

**Current state**: If none of `__linux__`, `_WIN32`, or `__APPLE__` are defined, the file compiles silently. All method bodies after the last `#endif` would be empty (`{}`) or have `return false` / `return -1` which are undefined in scope because they'd be outside any `#if` block.

Actually, looking at the structure:

```cpp
// Line 43: #if defined(__linux__)
// Line 174: #elif defined(_WIN32)
// Line 199: #elif defined(__APPLE__)
// Line 317: #endif

// Lines 318-??? — no code here, file ends
```

So all three branches close at line 317. If none matched, none of the function bodies would be defined — the linker would fail with "undefined symbol" errors. This is not a silent corruption; it's a linker error. But the error message is confusing.

**Fix**: Add a catch-all:
```cpp
#else
#error "Unsupported platform: TUN device not implemented for this OS. Supported: Linux (/dev/net/tun), Windows (wintun.dll), macOS (utun)."
#endif
```

Replace line 317 (`#endif`) with the above block.

**Verification**: Attempting to build on e.g., FreeBSD would produce a clear error at compile time instead of a cryptic linker error.

---

### Task 1.5 — Replace `/sys/class/net/` stats with platform-portable TunDevice counters

**Files**: `odc-hook/vpn/tun_device.hpp`, `odc-hook/vpn/tun_device.cpp`, `odc-hook/vpn/openvpn_client.cpp`

**Current state**: `openvpn_client.cpp` (lines 227-242) reads VPN traffic stats from Linux procfs:
```cpp
std::string base = "/sys/class/net/" + interface_name_ + "/statistics/";
std::ifstream rx(base + "rx_bytes");
// ...
```

`/sys/class/net/` is Linux-specific. On macOS, interface stats must be queried via `sysctl()` or `getifaddrs()`. On Windows, via `GetIfTable2()`. 

**Design decision**: The simplest cross-platform approach is to track byte counters internally inside `TunDevice`. Every `write()` call (IP packet written into TUN) increments a TX counter, and every `read()` from the TUN fd (IP packet received) increments an RX counter. This is independent of the OS and avoids all platform-specific stats APIs.

**Step A** — Add counter members to `TunDevice::Impl` (in `tun_device.cpp`, all three platform branches):

Inside the `struct Impl { ... };` definition for each platform (`__linux__`, `_WIN32`, `__APPLE__`):

```cpp
uint64_t tx_bytes{0};
uint64_t rx_bytes{0};
```

**Step B** — Increment counters in each platform's `write()` method:

```cpp
bool TunDevice::write(const uint8_t* data, size_t len) {
    // existing write logic...
    if (/* write succeeded */) {
        impl_->tx_bytes += len;
        return true;
    }
    return false;
}
```

**Step C** — Track RX bytes where packets are read from the TUN fd. In the WireGuard userspace `wg_device.cpp`, the TUN read loop already reads packets. Add a counter increment there or add a `notify_read(size_t bytes)` method to `TunDevice`.

**Step D** — Add public accessor methods to `tun_device.hpp`:

```cpp
uint64_t tx_bytes() const;
uint64_t rx_bytes() const;
```

Implement in each platform branch of `tun_device.cpp`:
```cpp
uint64_t TunDevice::tx_bytes() const { return impl_->tx_bytes; }
uint64_t TunDevice::rx_bytes() const { return impl_->rx_bytes; }
```

**Step E** — Update `openvpn_client.cpp` to use `tun_.tx_bytes()` / `tun_.rx_bytes()` instead of reading `/sys/class/net/` files.

**Verification**: Counters increment on all three platforms. Stats reporting matches the previous Linux `/sys/class/net/` values.

---

### Task 1.6 — Fix `openvpn_client.cpp` stats path for cross-platform

**File**: `odc-hook/vpn/openvpn_client.cpp`, lines 227-242

**Current state**:
```cpp
std::string base = "/sys/class/net/" + interface_name_ + "/statistics/";
std::ifstream rx_file(base + "rx_bytes");
std::ifstream tx_file(base + "tx_bytes");
```

After Task 1.5, counters are available via `tun_.tx_bytes()` / `tun_.rx_bytes()`.

**Fix**: Replace the procfs read block with direct TunDevice counter access:

```cpp
// Before (Linux-only):
// std::string base = "/sys/class/net/" + interface_name_ + "/statistics/";

// After (cross-platform):
stats.rx_bytes = tun_.rx_bytes();
stats.tx_bytes = tun_.tx_bytes();
```

If the `OVPN3Impl` class does not have direct access to `tun_` at the stats polling point (it may be in a different scope or callback), expose the counters through the `OpenVPNClient` public API or store them in the `stats_` struct directly.

**Verification**: `odc-cli hook status` shows correct VPN byte counters on all three platforms.

---

## Phase 2 — Fix CMake Build System

### Task 2.1 — Add `thirdparty/minimal-openvpn3` to the build + create `openvpn3_client` alias

**Files**: `CMakeLists.txt` (top-level)

**Current state**: `thirdparty/minimal-openvpn3` is never added via `add_subdirectory()`. The `odc-hook/CMakeLists.txt` links `openvpn3_client` but this target does not exist in the odc-root build tree.

**Step A** — Add the subdirectory to the top-level `CMakeLists.txt`, after the `BUILD_ODC_HOOK_VPN` option declaration and before `add_subdirectory(common)`:

```cmake
# OpenVPN3 core library — required by odc-hook VPN subsystem
if(BUILD_ODC_HOOK_VPN)
    add_subdirectory(thirdparty/minimal-openvpn3)
endif()
```

**Step B** — After the `add_subdirectory`, create an alias so the `odc-hook/CMakeLists.txt` link of `openvpn3_client` resolves:

```cmake
if(BUILD_ODC_HOOK_VPN)
    if(TARGET openvpn3 AND NOT TARGET openvpn3_client)
        add_library(openvpn3_client ALIAS openvpn3)
    endif()
endif()
```

The thirdparty CMake defines the target as `openvpn3` (line 104: `add_library(openvpn3 STATIC ...)`). We alias it to `openvpn3_client` to match the name `odc-hook/CMakeLists.txt` expects.

**Step C** — Handle potential FetchContent conflicts. The `thirdparty/minimal-openvpn3/CMakeLists.txt` uses `FetchContent_Declare` for asio, fmt, jsoncpp, and lz4. The parent `CMakeLists.txt` already has these via CPM. Potential conflict: CMake errors "add_library cannot create target X because another target with the same name already exists."

Mitigation: Set CPM variables that make the thirdparty CMake detect existing targets and skip its own FetchContent:

```cmake
# Prevent thirdparty/minimal-openvpn3 from re-fetching dependencies we already have
set(asio_SOURCE_DIR "${asio_SOURCE_DIR}" CACHE PATH "" FORCE)
set(fmt_SOURCE_DIR "${fmt_SOURCE_DIR}" CACHE PATH "" FORCE)
```

Or add `FetchContent_GetProperties` checks in the thirdparty CMakeLists.txt (requires patching the vendored file).

**Simpler approach**: Use `FetchContent_MakeAvailable` in the parent scope for these deps before adding the subdirectory. The thirdparty CMake checks `if(NOT TARGET asio)` and skips if the target already exists — which it will, since CPM already created it.

**Step D** — Add compile definitions required by OpenVPN3. When `BUILD_ODC_HOOK_VPN=ON`, add target compile definitions to `odc-hook`:

```cmake
if(BUILD_ODC_HOOK_VPN)
    target_compile_definitions(odc-hook PRIVATE
        ASIO_STANDALONE
        ASIO_NO_DEPRECATED
        USE_ASIO
        HAVE_LZ4
        HAVE_JSONCPP
    )
endif()
```

**Verification**: `cmake -B build -DBUILD_ODC_HOOK_VPN=ON` configures without errors. The `openvpn3` library target is built. `openvpn3_client` alias resolves.

---

### Task 2.2 — Remove vestigial `wg_crypto` link dependency

**File**: `odc-hook/CMakeLists.txt`, line 22

**Current state**:
```cmake
target_link_libraries(odc-hook PRIVATE
    openvpn3_client
    wg_crypto           # <-- THIS TARGET DOES NOT EXIST
    sodium::sodium
)
```

**Root cause**: The `wg_crypto` target was planned but never created. The `thirdparty/wg-tools/` directory contains WireGuard key generation utilities (`curve25519.c`, `encoding.c`, `genkey.c`, `pubkey.c`) but:
- No `CMakeLists.txt` exists in `thirdparty/wg-tools/`
- The odc-hook VPN subsystem (`wg_noise.cpp`, `wg_aead.cpp`) implements all WireGuard crypto directly using libsodium
- `wg_noise.cpp` uses `crypto_scalarmult_curve25519`, `crypto_box_keypair`, `crypto_generichash` from `<sodium.h>`
- `wg_aead.cpp` uses `crypto_aead_chacha20poly1305_ietf_encrypt`/`decrypt` from `<sodium.h>`
- `wg_peer.cpp` has no `wg_crypto` dependency — uses `<sodium.h>`, `<random>`, `<cstring>`, `<algorithm>`

No code in the VPN subsystem calls any `wg_crypto` function. The link dependency is vestigial.

**Fix**: Remove `wg_crypto` from the `target_link_libraries` in `odc-hook/CMakeLists.txt`:

```cmake
target_link_libraries(odc-hook PRIVATE
    openvpn3_client
    sodium::sodium
)
```

**Verification**: `BUILD_ODC_HOOK_VPN=ON` build succeeds. No "Target wg_crypto not found" error.

---

### Task 2.3 — Fix `sodium::sodium` alias for system libsodium case

**File**: `CMakeLists.txt` (top-level), lines 88-102

**Current state**:
```cmake
find_package(sodium QUIET)
if(NOT sodium_FOUND)
    CPMAddPackage(NAME libsodium ...)
    if(TARGET sodium)
        add_library(sodium::sodium ALIAS sodium)
    elseif(TARGET sodium_static)
        add_library(sodium::sodium ALIAS sodium_static)
    endif()
endif()
```

**Bug**: If the system has `libsodium-dev` installed, `find_package(sodium)` succeeds. The `if(NOT sodium_FOUND)` block is skipped. The `sodium::sodium` alias is never created. `odc-hook/CMakeLists.txt` then fails at link time with "Target sodium::sodium not found" — because the system package may export targets under a different name (e.g., `sodium` without namespace, or `Sodium::Sodium`).

**Fix**: Move the alias creation outside the `if(NOT sodium_FOUND)` block:

```cmake
find_package(sodium QUIET)
if(NOT sodium_FOUND)
    message(STATUS "libsodium not found via find_package — building from source")
    CPMAddPackage(
        NAME libsodium
        GITHUB_REPOSITORY jedisct1/libsodium
        GIT_TAG 1.0.20-RELEASE
        OPTIONS "SODIUM_DISABLE_TESTS ON" "SODIUM_MINIMAL_BUILD ON"
    )
endif()

# Always create the namespaced alias, regardless of how sodium was found
if(TARGET sodium AND NOT TARGET sodium::sodium)
    add_library(sodium::sodium ALIAS sodium)
elseif(TARGET sodium_static AND NOT TARGET sodium::sodium)
    add_library(sodium::sodium ALIAS sodium_static)
elseif(sodium_FOUND AND NOT TARGET sodium::sodium)
    # System find_package may define sodium_LIBRARIES but not a CMake target
    add_library(sodium::sodium INTERFACE IMPORTED)
    set_target_properties(sodium::sodium PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${sodium_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${sodium_LIBRARIES}"
    )
endif()
```

**Verification**: Build with `libsodium-dev` installed (system find succeeds) AND without it (CPM builds from source). Both paths produce a valid `sodium::sodium` target.

---

### Task 2.4 — Add include paths for OpenVPN3 headers to odc-hook

**File**: `odc-hook/CMakeLists.txt`

**Current state**: `openvpn_client.cpp` at line 4 includes `<client/ovpncli.hpp>`:
```cpp
#include <client/ovpncli.hpp>
```

But no include directory is configured for this path. The compiler would fail to find the header.

**Fix**: Add `thirdparty/minimal-openvpn3` to `odc-hook`'s include directories. In `odc-hook/CMakeLists.txt`, after the `if(BUILD_ODC_HOOK_VPN)` block:

```cmake
if(BUILD_ODC_HOOK_VPN)
    target_include_directories(odc-hook PRIVATE
        ${CMAKE_SOURCE_DIR}/thirdparty/minimal-openvpn3
    )
    target_compile_definitions(odc-hook PRIVATE ODC_HOOK_HAS_VPN=1)
endif()
```

**Verification**: `#include <client/ovpncli.hpp>` resolves correctly on all three platforms.

---

## Phase 3 — Implement Windows TUN via wintun.dll

### Background

Wintun is a signed kernel TUN driver for Windows from the WireGuard project. It provides a simple C API:
- `WintunCreateAdapter(Name, TunnelType, RequestedGUID)` → adapter handle
- `WintunOpenAdapter(Name)` → adapter handle
- `WintunStartSession(Adapter, Capacity)` → session handle
- `WintunGetReadWaitEvent(Session)` → `HANDLE` for async read notification
- `WintunReceivePacket(Session, &PacketSize)` → pointer to received packet data
- `WintunReleaseReceivePacket(Session, Packet)` → releases the packet buffer
- `WintunAllocateSendPacket(Session, PacketSize)` → pointer to send buffer
- `WintunSendPacket(Session, Packet)` → sends the packet
- `WintunEndSession(Session)` → end session
- `WintunCloseAdapter(Adapter)` → close adapter

The DLL is downloaded from https://www.wintun.net/ (prebuilt, signed, redistributable).

The vendored OpenVPN3 code already contains ring buffer implementations for high-performance I/O with wintun at `thirdparty/minimal-openvpn3/openvpn/tun/win/ringbuffer.hpp` and `openvpn/tun/win/client/wintun.hpp`. For odc-hook, we use wintun directly via its C API with runtime DLL loading — this is simpler than integrating the ring buffer approach and still provides correct functionality.

### Task 3.1 — Runtime dynamic loading of wintun.dll

**File**: `odc-hook/vpn/tun_device.cpp` (Windows `#elif defined(_WIN32)` branch)

**Step A** — Define function pointer types and wrapper struct:

```cpp
#elif defined(_WIN32)

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <memory>
#include <stdexcept>
#include <string>

// Wintun opaque handles
typedef void* WINTUN_ADAPTER_HANDLE;
typedef void* WINTUN_SESSION_HANDLE;

// Function pointer typedefs for runtime GetProcAddress
typedef WINTUN_ADAPTER_HANDLE (__stdcall *WintunCreateAdapterFn)(
    const wchar_t* Name, const wchar_t* TunnelType, const GUID* RequestedGUID);
typedef WINTUN_ADAPTER_HANDLE (__stdcall *WintunOpenAdapterFn)(
    const wchar_t* Name);
typedef void (__stdcall *WintunCloseAdapterFn)(
    WINTUN_ADAPTER_HANDLE Adapter);
typedef WINTUN_SESSION_HANDLE (__stdcall *WintunStartSessionFn)(
    WINTUN_ADAPTER_HANDLE Adapter, DWORD Capacity);
typedef void (__stdcall *WintunEndSessionFn)(
    WINTUN_SESSION_HANDLE Session);
typedef HANDLE (__stdcall *WintunGetReadWaitEventFn)(
    WINTUN_SESSION_HANDLE Session);
typedef BYTE* (__stdcall *WintunReceivePacketFn)(
    WINTUN_SESSION_HANDLE Session, DWORD* PacketSize);
typedef void (__stdcall *WintunReleaseReceivePacketFn)(
    WINTUN_SESSION_HANDLE Session, const BYTE* Packet);
typedef BYTE* (__stdcall *WintunAllocateSendPacketFn)(
    WINTUN_SESSION_HANDLE Session, DWORD PacketSize);
typedef void (__stdcall *WintunSendPacketFn)(
    WINTUN_SESSION_HANDLE Session, const BYTE* Packet);

// Holder for all function pointers + DLL handle
struct WintunApi {
    HMODULE dll{nullptr};
    WintunCreateAdapterFn create_adapter{nullptr};
    WintunOpenAdapterFn open_adapter{nullptr};
    WintunCloseAdapterFn close_adapter{nullptr};
    WintunStartSessionFn start_session{nullptr};
    WintunEndSessionFn end_session{nullptr};
    WintunGetReadWaitEventFn get_read_wait_event{nullptr};
    WintunReceivePacketFn receive_packet{nullptr};
    WintunReleaseReceivePacketFn release_receive_packet{nullptr};
    WintunAllocateSendPacketFn allocate_send_packet{nullptr};
    WintunSendPacketFn send_packet{nullptr};

    bool load() {
        dll = LoadLibraryW(L"wintun.dll");
        if (!dll) return false;

        #define LOAD(fn) fn = (fn##Fn)GetProcAddress(dll, #fn); if (!fn) { FreeLibrary(dll); dll = nullptr; return false; }
        LOAD(create_adapter);
        LOAD(open_adapter);
        LOAD(close_adapter);
        LOAD(start_session);
        LOAD(end_session);
        LOAD(get_read_wait_event);
        LOAD(receive_packet);
        LOAD(release_receive_packet);
        LOAD(allocate_send_packet);
        LOAD(send_packet);
        #undef LOAD
        return true;
    }

    void unload() {
        create_adapter = nullptr;
        // ... clear all pointers ...
        if (dll) { FreeLibrary(dll); dll = nullptr; }
    }
};
```

**Step B** — Update the Windows `Impl` struct:

```cpp
struct Impl {
    TunConfig cfg;
    std::string name;
    WintunApi api;
    WINTUN_ADAPTER_HANDLE adapter{nullptr};
    WINTUN_SESSION_HANDLE session{nullptr};
    uint64_t tx_bytes{0};
    uint64_t rx_bytes{0};
};
```

---

### Task 3.2 — Implement TunDevice::open() for Windows

**File**: `odc-hook/vpn/tun_device.cpp` (Windows branch)

**Current state**: `open()` always returns `false`.

**Implementation**:

```cpp
bool TunDevice::open(const TunConfig& cfg) {
    impl_->cfg = cfg;

    if (!impl_->api.load()) {
        SPDLOG_ERROR("TunDevice: wintun.dll not found — place wintun.dll next to odc-hook.exe");
        return false;
    }

    std::wstring name;
    if (cfg.name.empty()) {
        name = L"odc-hook";
    } else {
        // Convert UTF-8 to wide string
        int wlen = MultiByteToWideChar(CP_UTF8, 0, cfg.name.c_str(), -1, nullptr, 0);
        name.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, cfg.name.c_str(), -1, &name[0], wlen);
    }

    // Create or open the wintun adapter
    impl_->adapter = impl_->api.create_adapter(name.c_str(), L"ODC VPN", nullptr);
    if (!impl_->adapter) {
        // Try opening an existing adapter with the same name
        impl_->adapter = impl_->api.open_adapter(name.c_str());
        if (!impl_->adapter) {
            SPDLOG_ERROR("TunDevice: failed to create/open wintun adapter '{}'", cfg.name);
            impl_->api.unload();
            return false;
        }
    }

    // Start session with 4MB ring buffer capacity
    const DWORD kCapacity = 0x400000;
    impl_->session = impl_->api.start_session(impl_->adapter, kCapacity);
    if (!impl_->session) {
        impl_->api.close_adapter(impl_->adapter);
        impl_->adapter = nullptr;
        impl_->api.unload();
        return false;
    }

    impl_->name = cfg.name.empty() ? "odc-hook" : cfg.name;
    impl_->cfg = cfg;
    return true;
}
```

**Error handling**: If `create_adapter` fails (common if the wintun driver is not installed), log the error and return false. The caller (WireGuard client or OpenVPN client) handles the failure gracefully by not proceeding with the connection.

---

### Task 3.3 — Implement remaining TunDevice methods for Windows

**File**: `odc-hook/vpn/tun_device.cpp` (Windows branch)

```cpp
void TunDevice::close() {
    if (impl_->session) {
        impl_->api.end_session(impl_->session);
        impl_->session = nullptr;
    }
    if (impl_->adapter) {
        impl_->api.close_adapter(impl_->adapter);
        impl_->adapter = nullptr;
    }
    impl_->api.unload();
}

bool TunDevice::write(const uint8_t* data, size_t len) {
    if (!impl_->session) return false;
    DWORD pkt_size = static_cast<DWORD>(len);
    BYTE* pkt = impl_->api.allocate_send_packet(impl_->session, pkt_size);
    if (!pkt) return false;
    std::memcpy(pkt, data, len);
    impl_->api.send_packet(impl_->session, pkt);
    impl_->tx_bytes += len;
    return true;
}

bool TunDevice::bring_up() {
    // Wintun adapters are automatically up when session is started.
    return impl_->session != nullptr;
}

bool TunDevice::set_ip() {
    // Use netsh to assign IP address
    std::string cmd = "netsh interface ip set address \"" +
                      impl_->name + "\" static " +
                      impl_->cfg.ip_address + " " +
                      mask_from_prefix(impl_->cfg.prefix_len);
    return system(cmd.c_str()) == 0;
}

bool TunDevice::set_mtu(int mtu) {
    std::string cmd = "netsh interface ipv4 set subinterface \"" +
                      impl_->name + "\" mtu=" + std::to_string(mtu) +
                      " store=persistent";
    return system(cmd.c_str()) == 0;
}

int TunDevice::native_handle() const {
    if (!impl_->session) return -1;
    HANDLE ev = impl_->api.get_read_wait_event(impl_->session);
    // Return as int — caller can use it with WaitForSingleObject or asio
    // Actually, asio on Windows can wrap HANDLE objects.
    // For simplicity, return -1 and provide a separate method:
    return -1;
}

HANDLE TunDevice::read_event() const {
    if (!impl_->session) return nullptr;
    return impl_->api.get_read_wait_event(impl_->session);
}

bool TunDevice::read_packet(std::vector<uint8_t>& out) {
    if (!impl_->session) return false;
    DWORD pkt_size = 0;
    BYTE* pkt = impl_->api.receive_packet(impl_->session, &pkt_size);
    if (!pkt) return false;
    out.assign(pkt, pkt + pkt_size);
    impl_->rx_bytes += pkt_size;
    impl_->api.release_receive_packet(impl_->session, pkt);
    return true;
}

const std::string& TunDevice::name() const { return impl_->name; }
bool TunDevice::is_open() const { return impl_->session != nullptr; }
uint64_t TunDevice::tx_bytes() const { return impl_->tx_bytes; }
uint64_t TunDevice::rx_bytes() const { return impl_->rx_bytes; }
```

**Helper function** — `mask_from_prefix()`:
```cpp
static std::string mask_from_prefix(int prefix) {
    uint32_t mask = htonl(0xFFFFFFFFu << (32 - prefix));
    char buf[16];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
             (mask >> 24) & 0xFF, (mask >> 16) & 0xFF,
             (mask >> 8) & 0xFF, mask & 0xFF);
    return buf;
}
```

**Header update** — Add to `tun_device.hpp`:
```cpp
#if defined(_WIN32)
    void* read_event() const;
    bool read_packet(std::vector<uint8_t>& out);
#endif
    uint64_t tx_bytes() const;
    uint64_t rx_bytes() const;
```

---

### Task 3.4 — Bundle wintun.dll in Windows Docker output

**File**: `deploy/windows/Dockerfile`

**Step A** — Download wintun.dll during Docker build:

```dockerfile
# Download signed wintun.dll for Windows TUN support
RUN mkdir -p /opt/wintun && \
    curl -sSL https://www.wintun.net/builds/wintun-0.14.1.zip -o /tmp/wintun.zip && \
    unzip -j /tmp/wintun.zip "wintun/bin/amd64/wintun.dll" -d /opt/wintun/ && \
    rm /tmp/wintun.zip
```

**Step B** — Copy wintun.dll to output directory alongside odc-hook.exe:

```dockerfile
RUN mkdir -p /output && \
    cp build/odc-hook/odc-hook.exe /output/ && \
    cp /opt/wintun/wintun.dll /output/
```

**Step C** — Also copy wintun.dll to the MinGW system directory so test executables can find it:

```dockerfile
RUN cp /opt/wintun/wintun.dll /usr/x86_64-w64-mingw32/bin/
```

**Note**: wintun.dll is redistributable under its own license (included in the zip). The DLL must be placed in the same directory as `odc-hook.exe` at runtime.

---

## Phase 4 — Fix WireGuard Userspace IP Routing for Windows

### Task 4.1 — Replace `struct iphdr` with portable inline struct in `wg_device.cpp`

**File**: `odc-hook/vpn/wg_device.cpp`

**Current state**: `find_peer_for_ip()` (lines 221-237) uses `struct iphdr` from `<netinet/ip.h>` which doesn't exist on Windows.

**Step A** — Add portable IPv4 header struct in the anonymous namespace at the top of the file (after includes, before any function):

```cpp
namespace {

// Portable IPv4 header — identical layout to <netinet/ip.h> struct iphdr
// but without OS-specific header dependency
#pragma pack(push, 1)
struct ipv4_header {
    uint8_t  version_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  saddr[4];
    uint8_t  daddr[4];

    uint8_t ihl() const { return version_ihl & 0x0F; }
};
#pragma pack(pop)

static_assert(sizeof(ipv4_header) == 20, "IPv4 header must be 20 bytes");

} // namespace
```

**Step B** — Rewrite `find_peer_for_ip()` to use `ipv4_header`:

```cpp
WgPeer* WgDevice::find_peer_for_ip(const uint8_t* data, size_t len) {
    if (len < sizeof(ipv4_header)) return nullptr;

    const auto* ip = reinterpret_cast<const ipv4_header*>(data);

    // Extract destination IP from the header
    uint32_t dest_ip;
    std::memcpy(&dest_ip, ip->daddr, 4);
    dest_ip = ntohl(dest_ip);

    // Find the peer whose allowed_ips cover the destination
    for (auto& peer : peers_) {
        for (const auto& net : peer.allowed_ips) {
            if (net.contains(dest_ip)) {
                return &peer;
            }
        }
    }
    return nullptr;
}
```

**Step C** — Remove the `<netinet/ip.h>` include entirely (replaced by Task 1.3's guard + the portable struct above).

**Verification**: The struct layout matches the standard IPv4 header exactly. Test with captured packet dumps on Linux to verify `daddr` extraction produces correct values.

---

## Phase 5 — Cross-Compilation Docker Updates

### Task 5.1 — Windows Dockerfile: Enable VPN build + wintun + libsodium

**File**: `deploy/windows/Dockerfile`

**Current state**: Only `odc-hook` (non-VPN) + 3 test executables are built. No VPN, no libsodium, no wintun.

**Changes**:

**Step A** — Install unzip for wintun extraction (add to line 9-21 apt install):
```dockerfile
    unzip \
```

**Step B** — Cross-compile libsodium for Windows (after OpenSSL cross-compilation, before CMake):

```dockerfile
# ── Cross-compile libsodium for Windows ─────────────────────────────────────
RUN cd /tmp && \
    curl -sSL https://download.libsodium.org/libsodium/releases/libsodium-1.0.20.tar.gz | tar xz && \
    cd libsodium-stable && \
    ./configure \
        --host=x86_64-w64-mingw32 \
        --prefix=/opt/cross/x86_64-w64-mingw32 \
        --disable-shared \
        --enable-static \
        CC=x86_64-w64-mingw32-gcc \
        && make -j$(nproc) && make install && \
    rm -rf /tmp/libsodium-stable
```

**Step C** — Download wintun.dll (after libsodium):

```dockerfile
# ── Download wintun.dll for Windows TUN support ────────────────────────────
RUN curl -sSL https://www.wintun.net/builds/wintun-0.14.1.zip -o /tmp/wintun.zip && \
    unzip -j /tmp/wintun.zip "wintun/bin/amd64/wintun.dll" -d /opt/cross/x86_64-w64-mingw32/bin/ && \
    rm /tmp/wintun.zip
```

**Step D** — Update CMake invocation to enable VPN:

From:
```dockerfile
    -DBUILD_ODC_HOOK_VPN=OFF \
```

To:
```dockerfile
    -DBUILD_ODC_HOOK_VPN=ON \
```

**Step E** — Copy wintun.dll to output:

```dockerfile
RUN mkdir -p /output /output/tests && \
    cp build/odc-hook/odc-hook.exe /output/ && \
    cp /opt/cross/x86_64-w64-mingw32/bin/wintun.dll /output/ && \
    cp build/tests/test_event_bus.exe /output/tests/ 2>/dev/null || true && \
    cp build/tests/test_thread_pool.exe /output/tests/ 2>/dev/null || true && \
    cp build/tests/test_ws_client.exe /output/tests/ 2>/dev/null || true
```

**Verification**: Windows Docker build produces `odc-hook.exe` + `wintun.dll` + test .exe files. No header audit failures from VPN source files.

---

### Task 5.2 — macOS Dockerfile: Enable VPN build + libsodium

**File**: `deploy/macos/Dockerfile`

**Current state**: Only `odc-hook` (non-VPN) + 3 test executables. No VPN, no libsodium.

**Changes**:

**Step A** — Cross-compile libsodium for macOS (after OpenSSL cross-compilation, before CMake):

```dockerfile
# ── Cross-compile libsodium for macOS ───────────────────────────────────────
RUN cd /tmp && \
    curl -sSL https://download.libsodium.org/libsodium/releases/libsodium-1.0.20.tar.gz | tar xz && \
    cd libsodium-stable && \
    ./configure \
        --host=x86_64-apple-darwin14.0 \
        --prefix=/opt/cross/macos-x86_64 \
        --disable-shared \
        --enable-static \
        CC=x86_64-apple-${OSXCROSS_TARGET}-clang \
        && make -j$(nproc) && make install && \
    rm -rf /tmp/libsodium-stable
```

**Step B** — Update CMake invocation:

From:
```dockerfile
    -DBUILD_ODC_HOOK_VPN=OFF \
```

To:
```dockerfile
    -DBUILD_ODC_HOOK_VPN=ON \
```

**Step C** — Update the header audit (lines 59-64) to allow macOS-specific headers used by `tun_device.cpp`:

The audit currently rejects `<sys/socket.h>`, `<sys/un.h>`, `<sys/capability.h>`, and `<linux/`. These are valid exclusions. But macOS tun_device.cpp uses `<sys/kern_control.h>`, `<net/if_utun.h>`, `<sys/sys_domain.h>` which are macOS-only. The audit must add exclusions for macOS-specific paths:

```dockerfile
RUN echo "=== Cross-platform header audit ===" \
    && (! grep -rn --include='*.hpp' --include='*.cpp' \
        -E '<sys/socket\.h>|<sys/un\.h>|<sys/capability\.h>|<linux/' \
        odc-hook/ common/include/ common/src/ \
        | grep -v 'platform_linux' \
        | grep -v 'tun_device.cpp') \
    || (echo "ERROR: POSIX-only headers found in cross-platform code" && false)
```

Add `| grep -v 'tun_device.cpp'` to the pipeline so the macOS TUN code's platform headers pass the audit.

**Verification**: macOS Docker cross-compile produces `odc-hook` (Mach-O) + test executables. Header audit passes.

---

### Task 5.3 — Linux Dockerfile: Add libsodium-dev + enable VPN

**File**: `deploy/linux/Dockerfile`

**Current state**: Builds odc-hook (non-VPN) + odc-registry-manager + all tests.

**Changes**:

**Step A** — Add `libsodium-dev` to apt install (line 13):
```dockerfile
    g++-12 gcc-12 libssl-dev libsqlite3-dev libcap-dev libsodium-dev \
```

**Step B** — Update CMake invocation:

From:
```dockerfile
    -DBUILD_ODC_HOOK_VPN=OFF \
```

To:
```dockerfile
    -DBUILD_ODC_HOOK_VPN=ON \
```

**Step C** — The `find_package(sodium)` from the top-level CMakeLists.txt should succeed since `libsodium-dev` provides the CMake config. With the fix from Task 2.3, `sodium::sodium` alias will be created correctly.

**Verification**: Linux Docker build produces `odc-hook` with VPN support linked. `ctest` passes.

---

## Phase 6 — Verification per Platform

### Task 6.1 — Linux: build + test with VPN enabled

**Commands**:
```bash
cd odc-root
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_ODC_HOOK_VPN=ON \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure -j$(nproc)
```

**Expected**: All 7 test suites pass. `odc-hook` binary linked with `sodium::sodium` and `openvpn3_client`.

**WireGuard runtime test** (requires root + kernel WireGuard module):
```bash
sudo modprobe wireguard
# Start hook with VPN config, verify wg interface creation, peer connection
```

**OpenVPN runtime test** (requires root + OpenVPN server):
```bash
# Generate OVPN profile, start hook, verify connection via OpenVPN3 client
```

---

### Task 6.2 — macOS: cross-compile + header audit

**Commands** (inside Docker):
```bash
docker build -f deploy/macos/Dockerfile \
    --build-arg SDK_TARBALL=MacOSX14.0.sdk.tar.xz \
    --build-arg MACOS_SDK_VERSION=14.0 \
    -t odc-hook-macos .
```

**Expected**: 
- `odc-hook` (Mach-O binary) produced
- `test_event_bus`, `test_thread_pool`, `test_ws_client` compiled
- Header audit passes (macOS-specific TUN headers allowed)
- No POSIX headers outside `tun_device.cpp` + `platform_linux.cpp`

**Native macOS test** (on macos-13 runner):
```bash
brew install cmake ninja openssl@3 libsodium sqlite
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_ODC_HOOK_VPN=ON \
    -DBUILD_TUNNEL_MANAGER=OFF -DBUILD_REGISTRY_MANAGER=OFF \
    -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
cmake --build build -j$(sysctl -n hw.logicalcpu)
cd build && ctest --output-on-failure
```

**Expected**: `test_event_bus`, `test_thread_pool`, `test_ws_client`, `test_common` pass. `test_ipc_integration`, `test_hook_mtls_integration` may pass (macOS has Unix domain sockets).

---

### Task 6.3 — Windows: cross-compile + header audit

**Commands** (inside Docker):
```bash
docker build -f deploy/windows/Dockerfile -t odc-hook-windows .
```

**Expected**:
- `odc-hook.exe` + `wintun.dll` produced
- `test_event_bus.exe`, `test_thread_pool.exe`, `test_ws_client.exe` compiled
- Header audit passes (no POSIX headers in cross-platform code)
- No `<arpa/inet.h>`, `<netdb.h>`, `<netinet/ip.h>` in VPN sources after Phase 1 fixes

**Wine smoke test** (verify binary is well-formed):
```bash
wine odc-hook.exe --help
```

---

### Task 6.4 — Runtime smoke tests

**WireGuard** (requires: kernel module on Linux, wintun driver on Windows, utun on macOS):
```
1. Start odc-hook with WireGuard config (private_key, peers[], tun config)
2. Verify TUN interface created (ip link show / ifconfig / netsh interface ip show config)
3. Verify IP assigned to TUN interface
4. Verify wg show <interface> shows peer info
5. Generate traffic → verify tx_bytes / rx_bytes counters increment
6. Stop odc-hook → verify TUN interface removed
```

**OpenVPN** (requires: OpenVPN server accessible):
```
1. Generate .ovpn profile with server address, cert paths
2. Start odc-hook with OpenVPN config
3. Verify connection established (OpenVPN3 logs, hook status)
4. Verify TUN interface created with assigned IP
5. Disconnect → verify clean teardown
```

---

## Task Execution Order & Dependencies

```
Phase 1 (Source bugs) ── no dependencies ──────────────────────────────────
  Task 1.1 ─► Task 1.2 ─► Task 1.3 ─► Task 1.4
  Task 1.5 ─► Task 1.6

Phase 2 (CMake fixes) ── no dependencies ──────────────────────────────────
  Task 2.1 ─► Task 2.2 ─► Task 2.3 ─► Task 2.4

Phase 3 (Windows TUN) ── depends on Phase 1 (tun_device.cpp changes) ──
  Task 3.1 ─► Task 3.2 ─► Task 3.3 ─► Task 3.4

Phase 4 (WG IP routing) ── depends on Task 1.3 ───────────────────────────
  Task 4.1

Phase 5 (Docker updates) ── depends on Phase 1-4 complete ─────────────────
  Task 5.1 ─► Task 5.2 ─► Task 5.3

Phase 6 (Verification) ── depends on Phase 5 ──────────────────────────────
  Task 6.1 ─► Task 6.2 ─► Task 6.3 ─► Task 6.4
```

**Parallelism**: Phase 1 + Phase 2 can run concurrently (different files, no conflicts).

---

## Estimated Effort

| Phase | Tasks | Lines Changed | Est. Days | Risk |
|---|---|---|---|---|
| 1 — Source code bugs | 6 | ~50 | 1 day | Low — well-defined fixes |
| 2 — CMake build system | 4 | ~30 | 1 day | Medium — FetchContent conflicts possible |
| 3 — Windows TUN (wintun) | 4 | ~250 | 2 days | Medium — Windows TUN driver integration |
| 4 — WG IP routing fix | 1 | ~30 | 0.5 day | Low — struct redefinition |
| 5 — Docker updates | 3 | ~60 | 1 day | Low — standard Docker patterns |
| 6 — Verification | 4 | 0 | 1.5 days | Low — testing |
| **Total** | **22** | **~420** | **7 days** | |

---

## Platform Capability Matrix (Post-Fix)

| Capability | Linux | macOS | Windows |
|---|---|---|---|
| odc-hook (no VPN) | ✅ Build + Run + Test | ✅ Cross-compile + Header audit | ✅ Cross-compile + Header audit |
| Build with `-DBUILD_ODC_HOOK_VPN=ON` | ✅ | ✅ | ✅ |
| WireGuard build | ✅ (kernel netlink path) | ✅ (userspace + utun) | ✅ (userspace + wintun) |
| OpenVPN3 build | ✅ | ✅ | ✅ |
| WireGuard crypto (Noise + AEAD) | ✅ libsodium | ✅ libsodium | ✅ libsodium |
| TUN device | ✅ `/dev/net/tun` | ✅ `utun` | ✅ `wintun.dll` (runtime-loaded) |
| IP address assignment | ✅ `ip addr add` | ✅ `SIOCAIFADDR` | ✅ `netsh` |
| Interface up/down | ✅ ioctl | ✅ ioctl | ✅ automatic |
| MTU setting | ✅ ioctl | ✅ ioctl | ✅ `netsh` |
| Stats (tx/rx bytes) | ✅ TunDevice counters | ✅ TunDevice counters | ✅ TunDevice counters |
| Docker cross-compile | N/A (native) | ✅ osxcross | ✅ MinGW-w64 |
| Header portability audit | N/A | ✅ (tun_device excluded) | ✅ (tun_device excluded) |
| `WgPeerStats` | ✅ (per-platform compile) | ✅ (per-platform compile) | ✅ (per-platform compile) |
| `sodium::sodium` target | ✅ CPM or system | ✅ Cross-compiled | ✅ Cross-compiled |
| `openvpn3_client` target | ✅ Aliased from `openvpn3` | ✅ Aliased from `openvpn3` | ✅ Aliased from `openvpn3` |
| No `wg_crypto` link | ✅ Removed | ✅ Removed | ✅ Removed |

---

## Key Design Decisions

1. **TunDevice counters over OS-specific stats**: Tracking `tx_bytes`/`rx_bytes` inside `TunDevice` eliminates 3 platform-specific stats code paths (`/sys/class/net/` on Linux, `sysctl`/`getifaddrs` on macOS, `GetIfTable2` on Windows). The counters are accurate because every packet passes through `write()`.

2. **wintun.dll runtime loading over link-time**: Using `LoadLibrary`/`GetProcAddress` means the binary does not have a hard compile-time dependency on wintun. If wintun.dll is absent at runtime, `TunDevice::open()` returns false gracefully. This also simplifies cross-compilation (no need for wintun import library).

3. **Portable `ipv4_header` over `#ifdef`**: Instead of adding `#ifdef` for each platform's IP header (`<netinet/ip.h>` vs `<winsock2.h>`), defining a portable struct inline avoids all platform conditionals and is trivially verifiable (static_assert on size).

4. **CMake alias over build system restructure**: Rather than restructuring the entire VPN build system, aliasing `openvpn3` → `openvpn3_client` preserves the existing `odc-hook/CMakeLists.txt` link while fixing the actual dependency.
