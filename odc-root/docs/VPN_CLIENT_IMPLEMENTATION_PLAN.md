# Implementation Plan: VPN Client Libraries in odc-hook

> **Target**: Add OpenVPN and WireGuard client support into `odc-hook`, cross-compiled for Linux/Windows/macOS, with full event bus integration. Enabled by default via `BUILD_ODC_HOOK_VPN=ON`.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│ odc-hook                                                            │
│                                                                     │
│  WsClient ──▶ EventBus ──▶ ThreadPool ──▶ process_event()          │
│                                                │                    │
│                        ┌───────────────────────┼────────────┐       │
│                        │  VpnClientManager (new)             │       │
│                        │                                     │       │
│                        │  connect(id, cfg)                   │       │
│                        │  disconnect(id)                     │       │
│                        │  get_state(id) → VpnState           │       │
│                        │  get_stats(id) → VpnStats           │       │
│                        │                                     │       │
│                        │  ┌──────────┐  ┌──────────────┐    │       │
│                        │  │ OpenVPN  │  │ WireGuard    │    │       │
│                        │  │ Client   │  │ Client       │    │       │
│                        │  │          │  │              │    │       │
│                        │  │ ovpncli  │  │ wg_crypto +  │    │       │
│                        │  │ (lib)    │  │ wg_noise +   │    │       │
│                        │  │          │  │ wg_aead +    │    │       │
│                        │  │          │  │ wg_device    │    │       │
│                        │  └────┬─────┘  └──────┬───────┘    │       │
│                        │       │               │            │       │
│                        │  ┌────▼───────────────▼────────┐   │       │
│                        │  │  TunDevice (cross-platform) │   │       │
│                        │  │  Linux: /dev/net/tun        │   │       │
│                        │  │  Win:   wintun.dll          │   │       │
│                        │  │  macOS: utun socket         │   │       │
│                        │  └─────────────────────────────┘   │       │
│                        └─────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Task List

### Task 1: Import OpenVPN3 Client Library

**Goal**: Copy `minimal-openvpn3` sources into `odc-root/thirdparty/` and add CMake target.

#### 1.1 Copy source files

Copy from `odc-onboard/thirdparty/minimal-openvpn3/` into `odc-root/thirdparty/minimal-openvpn3/`.

Required source files (only the files needed for compilation; headers are compile-time only):

```
odc-root/thirdparty/minimal-openvpn3/
├── client/
│   ├── ovpncli.cpp              # OpenVPNClient C++ API (blocking connect loop)
│   └── ovpncli.hpp
├── openvpn/
│   ├── crypto/
│   │   └── data_epoch.cpp       # Crypto epoch management
│   └── openssl/xkey/
│       ├── xkey_helper.c        # OpenSSL 3.x provider keymgmt bridge (C)
│       └── xkey_provider.c      # OpenSSL 3.x external key provider (C)
│
├── openvpn/tun/                  # TUN abstraction headers
├── openvpn/transport/            # UDP/TCP transport headers
├── openvpn/ssl/                  # TLS session headers
├── openvpn/apple/                # macOS helpers
├── openvpn/win/                  # Windows helpers (wintun, tap)
└── ...                           # ~200 header files, no other .cpp needed
```

**Note**: The full header tree must be preserved — the library uses `#include` paths relative to `thirdparty/minimal-openvpn3/`.

#### 1.2 Add CMake target

In `odc-root/CMakeLists.txt`, after the existing CPM dependencies section, add:

```cmake
# ── OpenVPN3 Client (cross-platform static library) ────────────────────
option(BUILD_ODC_HOOK_VPN "Build VPN client support into odc-hook" ON)

add_library(openvpn3_client STATIC
    thirdparty/minimal-openvpn3/client/ovpncli.cpp
    thirdparty/minimal-openvpn3/openvpn/crypto/data_epoch.cpp
    thirdparty/minimal-openvpn3/openvpn/openssl/xkey/xkey_helper.c
    thirdparty/minimal-openvpn3/openvpn/openssl/xkey/xkey_provider.c
)
target_include_directories(openvpn3_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/minimal-openvpn3
)
target_compile_definitions(openvpn3_client PUBLIC
    ASIO_STANDALONE USE_ASIO HAVE_LZ4 HAVE_JSONCPP NOMINMAX USE_OPENSSL
)
target_compile_features(openvpn3_client PUBLIC cxx_std_20)

# Platform-specific dependencies (from upstream minimal-openvpn3/CMakeLists.txt)
if(WIN32)
    target_compile_definitions(openvpn3_client PUBLIC
        _WIN32_WINNT=0x0600 TAP_WIN_COMPONENT_ID=tap0901
        _CRT_SECURE_NO_WARNINGS ASIO_DISABLE_LOCAL_SOCKETS)
    target_link_libraries(openvpn3_client PUBLIC
        fwpuclnt iphlpapi wininet setupapi rpcrt4 wtsapi32 ws2_32 wsock32)
endif()

if(APPLE)
    find_library(CF_LIB CoreFoundation)
    find_library(IOKIT_LIB IOKit)
    find_library(SC_LIB SystemConfiguration)
    find_library(CS_LIB CoreServices)
    target_link_libraries(openvpn3_client PUBLIC
        ${CF_LIB} ${IOKIT_LIB} ${SC_LIB} ${CS_LIB})
endif()

target_link_libraries(openvpn3_client PUBLIC
    asio::asio
    fmt::fmt
    jsoncpp_static
    lz4::lz4
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
```

**Prerequisites**: `jsoncpp` in `odc-root/CMakeLists.txt` must expose the `jsoncpp_static` target. If currently using `jsoncpp_lib` or similar, add an alias:

```cmake
# Ensure the static jsoncpp target name matches what openvpn3_client expects
if(NOT TARGET jsoncpp_static AND TARGET jsoncpp_lib)
    add_library(jsoncpp_static ALIAS jsoncpp_lib)
endif()
```

**Verification**: `cmake --build build --target openvpn3_client` succeeds on Linux.

---

### Task 2: Import WireGuard Crypto Tools

**Goal**: Copy WG crypto C sources into `odc-root/thirdparty/wg-tools/` and add CMake target.

#### 2.1 Copy source files

Extract from `odc-onboard/thirdparty/minimal-wireguard-cpp/third_party/wg-tools/`:

```
odc-root/thirdparty/wg-tools/
├── genkey.c                   # wg_generate_private_key()
├── genkey.h
├── pubkey.c                   # wg_generate_public_key()
├── pubkey.h
├── encoding.c                 # base64 encode/decode
├── encoding.h
├── curve25519.c               # X25519 scalar ops, point multiplication
├── curve25519.h
├── curve25519-fiat32.h        # Fiat-crypto formal-verified impl (32-bit)
├── curve25519-hacl64.h        # HACL* verified impl (64-bit)
├── sec_store.c                # Secure zero-on-free memory storage
├── sec_store.h
├── containers.h               # Linked list macros
└── wg_ctype.h                 # Character type helpers
```

**Note**: `curve25519.c` contains a self-contained X25519 implementation independent of libsodium. It generates keys used by the WireGuard protocol. The `genkey.c` / `pubkey.c` files delegate to libsodium's `crypto_scalarmult_curve25519_base` for the core operation, but the standalone curve25519 implementation is available as a fallback.

#### 2.2 Add CMake target

In `odc-root/CMakeLists.txt`:

```cmake
# ── WireGuard Crypto Tools ────────────────────────────────────────────
add_library(wg_crypto STATIC
    thirdparty/wg-tools/genkey.c
    thirdparty/wg-tools/pubkey.c
    thirdparty/wg-tools/encoding.c
    thirdparty/wg-tools/curve25519.c
    thirdparty/wg-tools/sec_store.c
)
target_include_directories(wg_crypto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/wg-tools
)
target_compile_definitions(wg_crypto PRIVATE _GNU_SOURCE)
target_link_libraries(wg_crypto PUBLIC sodium::sodium)
set_target_properties(wg_crypto PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
)
```

**Verification**: `cmake --build build --target wg_crypto` succeeds on Linux.

---

### Task 3: Add libsodium Dependency

**Goal**: Make libsodium available for all three platforms via CMake and Docker cross-compilation.

#### 3.1 CMake integration

In `odc-root/CMakeLists.txt`, add after the OpenSSL `find_package`:

```cmake
# ── libsodium ─────────────────────────────────────────────────────────
# Try system package first (Linux native, or provided via CMAKE_PREFIX_PATH)
find_package(sodium QUIET)

if(NOT sodium_FOUND)
    find_library(SODIUM_LIB NAMES sodium libsodium REQUIRED
        PATHS ${CMAKE_FIND_ROOT_PATH}
        PATH_SUFFIXES lib lib64)
    find_path(SODIUM_INCLUDE_DIR sodium.h
        PATHS ${CMAKE_FIND_ROOT_PATH}
        PATH_SUFFIXES include)
    if(SODIUM_LIB AND SODIUM_INCLUDE_DIR)
        add_library(sodium::sodium UNKNOWN IMPORTED)
        set_target_properties(sodium::sodium PROPERTIES
            IMPORTED_LOCATION "${SODIUM_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_INCLUDE_DIR}")
        message(STATUS "libsodium: found at ${SODIUM_LIB}")
    else()
        message(FATAL_ERROR "libsodium not found. Install libsodium-dev or set CMAKE_FIND_ROOT_PATH")
    endif()
endif()
```

#### 3.2 Linux Docker: native package

In `deploy/linux/Dockerfile`, add to the `apt-get install` line:

```dockerfile
    libsodium-dev \
```

#### 3.3 Windows Docker: cross-compile from source

In `deploy/windows/Dockerfile`, after the OpenSSL cross-compile block:

```dockerfile
# Cross-compile libsodium for Windows (MinGW-w64)
RUN cd /tmp && \
    curl -sSL -o libsodium.tar.gz \
        https://download.libsodium.org/libsodium/releases/libsodium-1.0.20.tar.gz && \
    tar xzf libsodium.tar.gz --strip-components=1 && \
    ./configure \
        --host=x86_64-w64-mingw32 \
        --prefix=/opt/cross/x86_64-w64-mingw32 \
        --disable-shared --enable-static \
        CC=x86_64-w64-mingw32-gcc \
        CFLAGS="-O2" && \
    make -j"$(nproc)" && \
    make install && \
    rm -rf /tmp/libsodium
```

#### 3.4 macOS Docker: cross-compile from source

In `deploy/macos/Dockerfile`, in the `deps-builder` stage (Stage 2), after the OpenSSL block:

```dockerfile
# Cross-compile libsodium for macOS
RUN cd /tmp && \
    curl -sSL -o libsodium.tar.gz \
        https://download.libsodium.org/libsodium/releases/libsodium-1.0.20.tar.gz && \
    tar xzf libsodium.tar.gz --strip-components=1 && \
    ./configure \
        --host=x86_64-apple-${OSXCROSS_TARGET} \
        --prefix=/opt/cross/macos-x86_64 \
        --disable-shared --enable-static \
        CC=x86_64-apple-${OSXCROSS_TARGET}-clang \
        CFLAGS="-O2 -mmacosx-version-min=10.13" && \
    make -j"$(nproc)" && \
    make install && \
    rm -rf /tmp/libsodium
```

The `builder` stage (Stage 3) already copies `/opt/cross/macos-x86_64` from `deps-builder` and sets `CMAKE_PREFIX_PATH`, so libsodium headers/libs will be available automatically.

#### 3.5 Add shared build helper

In `deploy/common/build-common.sh`, add a reusable function:

```bash
build_libsodium_cross() {
    local host="$1"
    local prefix="$2"
    local cc="${3:-gcc}"
    local version="${4:-1.0.20}"

    if [ -f "${prefix}/lib/libsodium.a" ]; then
        echo "libsodium already built at ${prefix}"
        return 0
    fi

    echo "--- Cross-compiling libsodium for ${host} ---"
    mkdir -p /tmp/libsodium-build && cd /tmp/libsodium-build

    curl -sSL -o sodium.tar.gz \
        "https://download.libsodium.org/libsodium/releases/libsodium-${version}.tar.gz"
    tar xzf sodium.tar.gz --strip-components=1

    ./configure --host="${host}" --prefix="${prefix}" \
        --disable-shared --enable-static \
        CC="${cc}" CFLAGS="-O2" && \
        make -j"$(nproc)" && \
        make install && \
        rm -rf /tmp/libsodium-build
    echo "--- libsodium build complete ---"
}
```

**Verification**: `cmake -B build -GNinja` finds libsodium correctly on Linux. Docker Windows/macOS builds compile libsodium from source and CMake finds it via `CMAKE_FIND_ROOT_PATH`.

---

### Task 4: Platform TUN Device Abstraction

**Goal**: Create a cross-platform TUN interface manager that works on Linux, Windows, and macOS.

#### 4.1 Header file: `odc-hook/vpn/tun_device.hpp`

Design the `TunDevice` class with this interface:

```cpp
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <memory>

namespace odc::hook {

enum class TunResult { Ok, Error, Closed };

struct TunConfig {
    std::string name;           // e.g. "odc-wg-0" or "odc-ovpn-0"
    std::string ip_address;     // e.g. "10.0.0.2"
    int prefix_len{24};         // subnet prefix length
    int mtu{1420};              // MTU for the interface
};

class TunDevice {
public:
    using ReadCallback = std::function<void(const uint8_t* data, size_t len)>;

    TunDevice() = default;
    ~TunDevice();
    TunDevice(const TunDevice&) = delete;
    TunDevice& operator=(const TunDevice&) = delete;

    bool open(const TunConfig& cfg);
    void close();
    bool write(const uint8_t* data, size_t len);
    bool bring_up();
    bool set_ip();
    bool set_mtu(int mtu);

    int native_handle() const;       // fd or WINTUN_SESSION_HANDLE
    const std::string& name() const;
    bool is_open() const;
    std::string read_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace odc::hook
```

#### 4.2 Implementation: `odc-hook/vpn/tun_device.cpp`

Use `#ifdef` per platform in a single `.cpp` file. Three platform implementations:

**Linux** (`/dev/net/tun`):
```
1. open("/dev/net/tun", O_RDWR)
2. ioctl(fd, TUNSETIFF, &ifr) with IFF_TUN | IFF_NO_PI
3. ioctl(SIOCSIFADDR / SIOCSIFNETMASK / SIOCSIFMTU) for IP config
4. ioctl(SIOCGIFFLAGS) → set IFF_UP → ioctl(SIOCSIFFLAGS)
5. read(fd, buf, len) / write(fd, buf, len) for packet I/O
```

**Windows** (wintun.dll):
```
1. LoadLibrary("wintun.dll")
2. WintunCreateAdapter(name, "odc-hook", &guid)
3. WintunOpenAdapter(name)
4. WintunStartSession(adapter, capacity)
5. WintunGetReadWaitEvent(session) + WintunReceivePacket(session, &size)
6. WintunAllocateSendPacket(session, size) + WintunSendPacket(session, packet)
7. IP assignment via netsh or SetupAPI
```

**macOS** (utun):
```
1. socket(AF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL)
2. ctl_info.ctl_id = CTLIOCGINFO for "com.apple.net.utun_control"
3. connect(fd, ...) → assigns next utunN interface
4. ioctl(SIOCSIFADDR / SIOCSIFMTU) via AF_INET socket
5. read(fd, buf, len) / write(fd, buf, len) for packet I/O
```

**Note**: For Windows, `wintun.dll` must be downloaded from https://www.wintun.net/ (MIT licensed) and bundled alongside the binary. On macOS, utun requires no additional dependencies — it's built into the kernel.

**Verification**: Compiles on all three platforms. On Linux, `open()` creates a TUN device visible in `ip link show`.

---

### Task 5: WireGuard Protocol Types

**Goal**: Define WireGuard protocol constants and data structures.

#### 5.1 New file: `odc-hook/vpn/wg_types.hpp`

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace odc::hook::wg {

using WgKey = std::array<uint8_t, 32>;
using WgNonce = std::array<uint8_t, 12>;
using WgTag = std::array<uint8_t, 16>;
using WgCookie = std::array<uint8_t, 16>;

struct WgPeerConfig {
    WgKey public_key{};
    WgKey preshared_key{};
    std::string endpoint;                    // "host:port"
    std::vector<std::string> allowed_ips;     // "10.0.0.2/32"
    uint16_t persistent_keepalive_interval{25};
};

struct WgDeviceConfig {
    WgKey private_key{};
    uint16_t listen_port{0};                 // 0 = OS-assigned
    std::vector<WgPeerConfig> peers;
};

enum class WgMessageType : uint8_t {
    Invalid = 0,
    HandshakeInitiation  = 1,
    HandshakeResponse    = 2,
    CookieReply          = 3,
    TransportData        = 4,
};

enum class WgPeerState : uint8_t {
    Unknown,
    HandshakeInitiationSent,
    HandshakeResponseReceived,
    Connected,
    Dead,
};

// RFC 6479 / WireGuard constants
constexpr size_t  WG_MESSAGE_PREFIX_LEN   = 4;     // type(1) + reserved(3)
constexpr size_t  WG_HANDSHAKE_INIT_LEN   = 148;   // fixed-size initiation msg
constexpr size_t  WG_HANDSHAKE_RESP_LEN   = 92;    // fixed-size response msg
constexpr size_t  WG_COOKIE_REPLY_LEN     = 64;    // fixed-size cookie reply
constexpr size_t  WG_DATA_OVERHEAD        = 32;    // 16 poly1305 tag + 16 pad
constexpr size_t  WG_KEY_LEN              = 32;
constexpr size_t  WG_AEAD_NONCE_LEN       = 12;
constexpr size_t  WG_COOKIE_LEN           = 16;
constexpr size_t  WG_HASH_LEN             = 32;
constexpr size_t  WG_IDENTIFIER_LEN       = 32;

constexpr uint64_t REKEY_AFTER_MESSAGES   = 1ULL << 60;
constexpr int      REKEY_AFTER_TIME_SEC    = 120;
constexpr int      REJECT_AFTER_TIME_SEC   = 180;
constexpr int      KEEPALIVE_TIMEOUT_SEC   = 10;
constexpr int      HANDSHAKE_RETRY_SEC     = 5;

} // namespace odc::hook::wg
```

**Verification**: Compiles. No test needed — this is a header-only types file.

---

### Task 6: WireGuard AEAD (ChaCha20-Poly1305)

**Goal**: Wrap libsodium AEAD primitives for WireGuard data packet encryption.

#### 6.1 New file: `odc-hook/vpn/wg_aead.hpp`

```cpp
#pragma once
#include "wg_types.hpp"
#include <cstddef>

namespace odc::hook::wg {

class WgAead {
public:
    static bool encrypt(
        const uint8_t* plaintext, size_t plaintext_len,
        const uint8_t* ad, size_t ad_len,
        const WgKey& key, uint64_t counter,
        uint8_t* ciphertext, WgTag& tag_out);

    static bool decrypt(
        const uint8_t* ciphertext, size_t ciphertext_len,
        const uint8_t* ad, size_t ad_len,
        const WgKey& key, uint64_t counter,
        uint8_t* plaintext, const WgTag& tag_in);

private:
    static void counter_to_nonce(uint64_t counter, WgNonce& nonce);
};

} // namespace odc::hook::wg
```

#### 6.2 New file: `odc-hook/vpn/wg_aead.cpp`

Implementation uses libsodium:
- `crypto_aead_chacha20poly1305_ietf_encrypt` / `_decrypt`
- Nonce construction from 64-bit counter (little-endian, zero-padded to 12 bytes)

**Verification**: Unit test: encrypt known plaintext with known key/counter, verify ciphertext and tag are deterministic. Decrypt ciphertext, verify original plaintext is recovered.

---

### Task 7: WireGuard Noise Handshake

**Goal**: Implement the Noise_IKpsk2 handshake (initiator side only) using libsodium.

#### 7.1 New file: `odc-hook/vpn/wg_noise.hpp`

```cpp
#pragma once
#include "wg_types.hpp"
#include <functional>
#include <chrono>

namespace odc::hook::wg {

struct NoiseHandshakeResult {
    bool success{false};
    // On success, populated with derived session keys:
    WgKey sending_key{};
    WgKey receiving_key{};
    uint64_t sending_key_counter{0};
    uint64_t receiving_key_counter{0};
};

class WgNoiseHandshake {
public:
    WgNoiseHandshake(
        const WgKey& static_private,       // our private key
        const WgKey& peer_static_public,    // server's public key
        const WgKey& preshared_key = {}     // optional PSK
    );

    // Build handshake initiation message (148 bytes)
    bool build_initiation(
        uint8_t out_msg[WG_HANDSHAKE_INIT_LEN],
        uint32_t& out_sender_index);

    // Process handshake response (92 bytes input)
    // Returns true + populates keys on success
    NoiseHandshakeResult process_response(
        const uint8_t msg[WG_HANDSHAKE_RESP_LEN],
        uint32_t sender_index);

    // Process cookie reply (updates our internal state, affects next initiation)
    void process_cookie_reply(
        const uint8_t msg[WG_COOKIE_REPLY_LEN]);

    bool is_complete() const { return handshake_complete_; }

private:
    WgKey static_private_;
    WgKey peer_static_public_;
    WgKey preshared_key_;
    WgKey ephemeral_private_;         // regenerated each initiation
    WgKey ephemeral_public_;
    uint32_t last_sender_index_{0};
    bool handshake_complete_{false};

    // Internal state for cookie reply handling
    WgCookie last_cookie_{};
    bool has_cookie_reply_{false};
    std::chrono::steady_clock::time_point last_cookie_time_;

    // libsodium DH wrappers
    static bool dh(WgKey& out, const WgKey& private_key, const WgKey& public_key);
    static bool generate_keypair(WgKey& private_key, WgKey& public_key);

    // Chaining key + hash: HKDF using BLAKE2s (via libsodium generic hash)
    // Chain: mix_key(key, input) → (new_key, new_chain)
    bool mix_key(WgKey& ck, WgKey& key, const uint8_t* input, size_t len);
    bool mix_hash(uint8_t hash[WG_HASH_LEN], const uint8_t* input, size_t len);
    bool mix_psk(WgKey& ck, uint8_t hash[WG_HASH_LEN], const WgKey& psk);
};

} // namespace odc::hook::wg
```

#### 7.2 New file: `odc-hook/vpn/wg_noise.cpp`

Implementation follows the Noise_IKpsk2 protocol specification:

1. **Build Initiation** (148 bytes):
   - Generate ephemeral X25519 keypair
   - `mix_hash(C1 ← HASH(IDENTIFIER))`
   - `mix_hash(C2 ← HASH(C1 || server_static_pub))`
   - `mix_key(CK ← EMPTY)`
   - `mix_key(CK ← CK1 || DH(ephemeral_priv, server_static_pub))`
   - `mix_hash(C3 ← HASH(C2 || ephemeral_pub))`
   - `mix_key(CK ← CK2 || DH(ephemeral_priv, server_static_pub))`
   - `mix_key(CK ← CK3 || DH(static_priv, server_static_pub))`
   - `tau ← HMAC(key=CK3, data=C3)`
   - `mix_hash(C4 ← HASH(C3 || tau))`
   - Encrypt static_public with CK3, nonce=0, AD=C4 → enc_static
   - Encrypt timestamp with CK3, nonce=1, AD=C4 → enc_timestamp
   - mac1 = HMAC(key=HASH(label_mac1 || server_pub), data=msg)
   - mac2 = HMAC(key=cookie, data=msg[0..-16]) if cookie exists
   - Output: `[type:1][reserved:3][sender_index:4][ephemeral:32][enc_static:48][enc_timestamp:16][mac1:16][mac2:16]`

2. **Process Response** (92 bytes input):
   - Parse: `[type:2][reserved:3][sender_index:4][receiver_index:4][ephemeral:32][enc_nothing:16][mac1:16][mac2:16]`
   - Verify MACs
   - `mix_hash(C5 ← HASH(C4 || receiver_ephemeral))`
   - `mix_key(CK ← CK4 || DH(ephemeral_priv, receiver_ephemeral))`
   - `mix_key(CK ← CK5 || DH(static_priv, receiver_ephemeral))`
   - `tau ← HASH(HASH(label || C5))`
   - `mix_hash(C6 ← HASH(C5 || tau))`
   - Decrypt "nothing" with CK5, nonce=0, AD=C6 (should be zero-length)
   - Derive `sending_key` ← KDF(CK, C6, 1) for 0...3
   - Derive `receiving_key` ← KDF(CK, C6, 2) for 0...3

3. **Process Cookie Reply**: Store cookie for inclusion in mac2 of next initiation.

**Verification**: Test against known-good test vectors from the WireGuard white paper. Verify handshake completes with expected derived session keys.

---

### Task 8: WireGuard Peer State Machine

**Goal**: Track per-peer state: handshake, data session, timers, rekey.

#### 8.1 New file: `odc-hook/vpn/wg_peer.hpp`

```cpp
#pragma once
#include "wg_types.hpp"
#include "wg_noise.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <netinet/in.h>  // sockaddr_in / sockaddr_in6

namespace odc::hook::wg {

class WgPeer {
public:
    using SendCallback = std::function<bool(const uint8_t* data, size_t len)>;

    WgPeer(const WgPeerConfig& cfg, int peer_index);
    ~WgPeer();

    // Called periodically by WgDevice timer loop
    bool tick(std::chrono::steady_clock::time_point now);

    // Called when a UDP packet arrives from the network
    bool handle_incoming(const uint8_t* packet, size_t len,
                         const sockaddr_storage& from_addr);

    // Called by WgDevice when TUN has a packet for this peer
    bool handle_outgoing_tun(const uint8_t* data, size_t len);

    // Accessors
    WgPeerState state() const { return state_; }
    uint32_t sender_index() const { return sender_index_; }
    const WgKey& peer_pubkey() const { return cfg_.public_key; }
    const sockaddr_storage& endpoint() const { return endpoint_; }
    bool has_valid_session() const;

    // Stats
    uint64_t rx_bytes() const { return rx_bytes_; }
    uint64_t tx_bytes() const { return tx_bytes_; }

    void set_send_callback(SendCallback cb) { send_cb_ = std::move(cb); }

private:
    WgPeerConfig cfg_;
    WgPeerState state_{WgPeerState::Unknown};
    sockaddr_storage endpoint_{};

    // Noise handshake
    std::unique_ptr<WgNoiseHandshake> handshake_;
    std::chrono::steady_clock::time_point last_handshake_time_;
    int handshake_retries_{0};

    // Session keys (from completed handshake)
    WgKey sending_key_{};
    uint64_t sending_key_counter_{0};
    WgKey receiving_key_{};
    uint64_t receiving_key_counter_{0};
    std::chrono::steady_clock::time_point key_established_time_;

    // Timers
    bool need_handshake_initiation_{false};
    bool need_keepalive_{false};
    std::chrono::steady_clock::time_point last_data_packet_rx_;
    std::chrono::steady_clock::time_point last_data_packet_tx_;

    uint32_t sender_index_{0};
    SendCallback send_cb_;

    // Stats
    uint64_t rx_bytes_{0};
    uint64_t tx_bytes_{0};

    std::mutex mutex_;

    // State-specific handlers
    bool handle_handshake_response(const uint8_t* msg, size_t len);
    bool handle_transport_data(const uint8_t* msg, size_t len,
                               const WgKey& key, uint64_t& counter);
    bool send_handshake_initiation();
    bool send_transport_data(const uint8_t* plaintext, size_t len);
    bool send_keepalive();

    // Rekey evaluation
    bool should_rekey(std::chrono::steady_clock::time_point now) const;
    void begin_handshake();
};

} // namespace odc::hook::wg
```

#### 8.2 New file: `odc-hook/vpn/wg_peer.cpp`

State machine:
```
  Unknown →
    HandshakeInitiationSent (send initiation, wait 5s)
      → HandshakeResponseReceived (process response, derive keys)
        → Connected (encrypt/decrypt transport data)
          → rekey after 120s or 2^60 msgs → HandshakeInitiationSent
          → no data for 180s → Dead
      → timeout with no response → retry up to N times → Dead
  Dead → triggered for new handshake → HandshakeInitiationSent
```

**Verification**: Unit test with mock send callback and simulated handshake exchange. Verify state transitions (Init → HS-Sent → Connected). Verify rekey timer fires after 120s.

---

### Task 9: Userspace WireGuard Device

**Goal**: Tie together TunDevice + WgPeer(s) + UDP socket. Reads raw IP from TUN, encrypts with peer's session key, sends via UDP. Reads from UDP, decrypts, writes to TUN.

#### 9.1 New file: `odc-hook/vpn/wg_device.hpp`

```cpp
#pragma once
#include "wg_types.hpp"
#include "wg_peer.hpp"
#include "tun_device.hpp"
#include <asio.hpp>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace odc::hook::wg {

class WgDevice {
public:
    WgDevice(asio::io_context& io, TunDevice&& tun);
    ~WgDevice();

    bool start(const WgDeviceConfig& cfg);
    void stop();

    WgPeerState peer_state(size_t peer_index) const;
    uint64_t peer_rx_bytes(size_t peer_index) const;
    uint64_t peer_tx_bytes(size_t peer_index) const;

private:
    asio::io_context& io_;
    TunDevice tun_;
    asio::ip::udp::socket udp_socket_;
    std::vector<std::unique_ptr<WgPeer>> peers_;

    std::thread tun_reader_thread_;       // reads raw IP from TUN
    std::thread tun_writer_thread_;       // writes decrypted IP to TUN
    std::thread timer_thread_;            // tick() each peer every 100ms
    std::thread udp_reader_thread_;       // reads encrypted UDP from network
    std::atomic<bool> running_{false};

    uint8_t tun_read_buf_[65536];
    uint8_t udp_read_buf_[65536];

    void tun_read_loop();
    void tun_write_loop();
    void udp_read_loop();
    void timer_loop();

    // Packet routing
    WgPeer* find_peer_for_ip(const uint8_t* ip_packet, size_t len);
    WgPeer* find_peer_by_sender_index(uint32_t sender_index);

    // Thread-safe write queues (TUN → peer)
    // + UDP socket write (peer → network)
};

} // namespace odc::hook::wg
```

#### 9.2 New file: `odc-hook/vpn/wg_device.cpp`

Core data flow:

```
[TUN read thread]
  tun_read_loop():
    while running:
      n = read(tun_fd, buf, sizeof(buf))
      ip_hdr = parse_ip_header(buf, n)
      peer = find_peer_for_ip(peer_ip)
      if peer && peer.has_valid_session():
        ciphertext = peer.encrypt_transport(buf, n)  // AEAD
        sendto(udp_socket, ciphertext, peer.endpoint())

[UDP read thread]
  udp_read_loop():
    while running:
      n = recvfrom(udp_socket, buf, sizeof(buf), &from_addr)
      msg_type = buf[0]
      if msg_type == HANDSHAKE_RESPONSE:
        peer = find_peer_by_index(buf[sender_idx])
        peer.handle_incoming(buf, n, from_addr)
      elif msg_type == TRANSPORT_DATA:
        peer = find_peer_by_receiver_index(buf[receiver_idx])
        plaintext = peer.decrypt_transport(buf, n)
        write(tun_fd, plaintext)

[Timer thread]
  timer_loop():
    while running:
      now = steady_clock::now()
      for each peer:
        peer.tick(now)
      sleep(100ms)
```

**Note**: On **Linux**, `WgDevice` is not used. Instead, `WireGuardClient` uses the kernel WireGuard module via `WgNetlink` (like odc-onboard does). This avoids userspace crypto overhead on the platform that supports kernel WG.

**Verification**: Integration test with a real WireGuard server. Establish a tunnel, ping through it, verify rx/tx byte counters increment.

---

### Task 10: WireGuard Client Wrapper

**Goal**: Create a unified `WireGuardClient` that uses kernel WG on Linux and userspace `WgDevice` on Windows/macOS.

#### 10.1 New file: `odc-hook/vpn/wireguard_client.hpp`

```cpp
#pragma once
#include "wg_types.hpp"
#include <asio.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace odc::hook {

enum class VpnState : uint8_t {
    Idle, Connecting, Connected, Disconnecting, Disconnected, Error, Reconnecting
};

struct VpnStats {
    uint64_t bytes_in{0};
    uint64_t bytes_out{0};
    uint64_t packets_in{0};
    uint64_t packets_out{0};
    double duration_sec{0};
    int reconnect_count{0};
};

class WireGuardClient {
public:
    using StateCallback = std::function<void(VpnState, std::string error)>;
    using StatsCallback = std::function<void(VpnStats)>;

    WireGuardClient() = default;
    ~WireGuardClient();

    bool start(const wg::WgDeviceConfig& cfg, asio::io_context& io);
    void stop();
    VpnState state() const;
    VpnStats stats() const;

    void on_state_change(StateCallback cb);
    void on_stats(StatsCallback cb);

private:
    asio::io_context* io_{nullptr};
    std::atomic<VpnState> state_{VpnState::Idle};
    StateCallback state_cb_;
    StatsCallback stats_cb_;
    VpnStats stats_;

#if defined(ODC_PLATFORM_LINUX)
    // Linux: use kernel WireGuard via netlink (proven, efficient)
    std::unique_ptr<class WgNetlinkAdapter> netlink_;
#else
    // Windows/macOS: use userspace WgDevice
    std::unique_ptr<wg::WgDevice> device_;
    std::unique_ptr<wg::TunDevice> tun_;
#endif
    std::thread io_thread_;
    std::thread stats_thread_;
    std::atomic<bool> running_{false};
};

} // namespace odc::hook
```

#### 10.2 New file: `odc-hook/vpn/wireguard_client.cpp`

**Linux path** (`wg_netlink.hpp` copied from `odc-onboard/src/core/wg_netlink.hpp`):
```
start():
  tun_.open(cfg)
  netlink_->create_interface(iface_name)
  netlink_->set_device(private_key, listen_port, peers)
  netlink_->add_address(ip, prefix_len)
  netlink_->bring_interface_up()
  for each peer allowed_ips: netlink_->add_route(ip, prefix_len)
  set_state(Connected)
  start_stats_thread()

stop():
  netlink_->delete_interface(iface_name)
  tun_.close()
  set_state(Disconnected)
```

**Windows/macOS path**:
```
start():
  tun_.open(cfg)
  device_->start(cfg)           // creates UDP socket + WgPeers
  // WgDevice runs its own threads for TUN I/O, UDP I/O, timer
  set_state(Connected)          // after first peer handshake succeeds
  start_stats_thread()

stop():
  device_->stop()
  tun_.close()
  set_state(Disconnected)
```

**Verification**: Linux: `start(config)` → `ip link show` shows wg interface. Windows: requires WinTUN. macOS: requires utun.

---

### Task 11: OpenVPN Client Wrapper

**Goal**: Wrap the `openvpn3_client` library with the same interface as `WireGuardClient`.

#### 11.1 New file: `odc-hook/vpn/openvpn_client.hpp`

```cpp
#pragma once
#include <asio.hpp>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <memory>

namespace odc::hook {

struct OpenVPNConfig {
    std::string config_path;            // path to .ovpn profile on disk
    std::string credentials_path;       // optional .enc + .key for encrypted creds
    std::string interface_name;         // "odc-ovpn-0"
    int max_retries{5};
    int connect_timeout_sec{30};
    bool route_safety_block_default{true};
};

class OpenVPNClient {
public:
    using StateCallback = std::function<void(VpnState, std::string error)>;
    using StatsCallback = std::function<void(VpnStats)>;

    OpenVPNClient() = default;
    ~OpenVPNClient();

    bool start(const OpenVPNConfig& cfg, asio::io_context& io);
    void stop();
    VpnState state() const;
    VpnStats stats() const;

    void on_state_change(StateCallback cb);
    void on_stats(StatsCallback cb);

private:
    asio::io_context* io_{nullptr};
    std::atomic<VpnState> state_{VpnState::Idle};
    StateCallback state_cb_;
    StatsCallback stats_cb_;
    VpnStats stats_;

    class OpenVPNClientImpl;    // subclass of openvpn::ClientAPI::OpenVPNClient
    std::unique_ptr<OpenVPNClientImpl> impl_;
    std::thread io_thread_;
    std::thread stats_thread_;
    std::atomic<bool> running_{false};
};

} // namespace odc::hook
```

#### 11.2 New file: `odc-hook/vpn/openvpn_client.cpp`

`OpenVPNClientImpl` subclasses `openvpn::ClientAPI::OpenVPNClient` (from `minimal-openvpn3/client/ovpncli.hpp`) and implements:
- `event()` callback → map OpenVPN3 events (CONNECTED, DISCONNECTED, RECONNECTING, etc.) to `VpnState` and fire `state_cb_`
- `tun_builder_new()` → creates a `TunDevice` for this connection
- `tun_builder_establish()` → sets MTU on the TUN device
- `tun_builder_add_address()` → sets IP address
- `tun_builder_add_route()` → adds routing entries (with route safety check)
- `tun_builder_set_remote_address()` → adds point-to-point remote IP
- `log()` callback → forward to spdlog

Connection flow:
```
start():
  1. Load .ovpn profile from config_path
  2. If route_safety_block_default: inject "pull-filter ignore redirect-gateway"
  3. eval_config() → validate profile
  4. If credentials_path exists: decrypt + provide_creds()
  5. Spawn io_thread: client_.connect() (blocking OpenVPN3 connect loop)
  6. On CONNECTED event: tun created, IP set, routes added → state = Connected
  7. start_stats_thread(): poll /sys/class/net/<iface>/statistics/ every 5s

stop():
  1. client_.stop() → signal OpenVPN3 to disconnect
  2. Join io_thread, stats_thread
  3. tun.close()
  4. state = Disconnected
```

**Verification**: Test with a real OpenVPN server. `start(cfg)` → verify connection state transition idle→connecting→connected. `stop()` → verify disconnected.

---

### Task 12: VpnClientManager

**Goal**: Orchestrates multiple VPN connections, bridges JSON WebSocket messages to C++ VPN client operations, publishes state/stats back to the EventBus.

#### 12.1 New file: `odc-hook/vpn/vpn_manager.hpp`

```cpp
#pragma once
#include <nlohmann/json.hpp>
#include <asio.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>

namespace odc::hook {

class VpnClientManager {
public:
    using EventSender = std::function<void(const nlohmann::json&)>;

    explicit VpnClientManager(asio::io_context& io);
    ~VpnClientManager();

    void set_event_sender(EventSender sender);

    // Commands (arrive via WebSocket → EventBus → process_event)
    void connect(const std::string& conn_id, const nlohmann::json& config);
    void disconnect(const std::string& conn_id);
    nlohmann::json get_status(const std::string& conn_id) const;

private:
    asio::io_context& io_;
    EventSender send_event_;
    std::mutex mutex_;

    struct VpnClientEntry {
        std::string id;
        std::string vpn_type;       // "wireguard" or "openvpn"
        VpnState state{VpnState::Idle};
        std::shared_ptr<WireGuardClient> wg_client;
        std::shared_ptr<OpenVPNClient> ovpn_client;
    };
    std::unordered_map<std::string, VpnClientEntry> clients_;

    void publish(const std::string& type, const nlohmann::json& data);
    nlohmann::json build_vpn_connect_config(const std::string& vpn_type,
                                            const nlohmann::json& wire_msg);
};

} // namespace odc::hook
```

#### 12.2 New file: `odc-hook/vpn/vpn_manager.cpp`

Message routing:

```cpp
void VpnClientManager::connect(const std::string& conn_id, const nlohmann::json& msg) {
    std::lock_guard lock(mutex_);

    auto& entry = clients_[conn_id];
    entry.id = conn_id;
    entry.vpn_type = msg.value("vpn_type", "");

    if (entry.vpn_type == "wireguard") {
        auto cfg = parse_wireguard_config(msg.value("config", json::object()));
        entry.wg_client = std::make_shared<WireGuardClient>();
        entry.wg_client->on_state_change([this, conn_id](VpnState s, std::string err) {
            publish("vpn.state_change", {
                {"connection_id", conn_id},
                {"vpn_type", "wireguard"},
                {"state", to_string(s)},
                {"error", err}
            });
        });
        entry.wg_client->on_stats([this, conn_id](VpnStats st) {
            publish("vpn.stats", {
                {"connection_id", conn_id},
                {"bytes_in", st.bytes_in},
                {"bytes_out", st.bytes_out}
            });
        });
        entry.wg_client->start(cfg.wg, io_);

    } else if (entry.vpn_type == "openvpn") {
        auto cfg = parse_openvpn_config(msg.value("config", json::object()));
        entry.ovpn_client = std::make_shared<OpenVPNClient>();
        entry.ovpn_client->on_state_change([this, conn_id](VpnState s, std::string err) {
            publish("vpn.state_change", {
                {"connection_id", conn_id},
                {"vpn_type", "openvpn"},
                {"state", to_string(s)},
                {"error", err}
            });
        });
        entry.ovpn_client->on_stats([this, conn_id](VpnStats st) {
            publish("vpn.stats", {
                {"connection_id", conn_id},
                {"bytes_in", st.bytes_in},
                {"bytes_out", st.bytes_out}
            });
        });
        entry.ovpn_client->start(cfg.ovpn, io_);
    }
}

void VpnClientManager::disconnect(const std::string& conn_id) {
    std::lock_guard lock(mutex_);
    auto it = clients_.find(conn_id);
    if (it == clients_.end()) return;
    if (it->second.wg_client) it->second.wg_client->stop();
    if (it->second.ovpn_client) it->second.ovpn_client->stop();
    clients_.erase(it);
}
```

**Verification**: Integration test: send `{"type":"vpn.connect", ...}` via WebSocket, verify a `{"type":"vpn.state_change","new_state":"connected"}` is emitted. Send `{"type":"vpn.disconnect", ...}` and verify `"disconnected"` emitted.

---

### Task 13: Event Bus Integration in HookService

**Goal**: Wire `VpnClientManager` into the existing `process_event()` dispatch.

#### 13.1 Edit `odc-hook/hook_service.hpp`

Add member:
```cpp
#if ODC_HOOK_HAS_VPN
    std::unique_ptr<VpnClientManager> vpn_manager_;
#endif
```

#### 13.2 Edit `odc-hook/hook_service.cpp`

**In `initialize()`**: Create `vpn_manager_` and wire `set_event_sender()`:
```cpp
#if ODC_HOOK_HAS_VPN
    vpn_manager_ = std::make_unique<VpnClientManager>(io_ctx_);
    vpn_manager_->set_event_sender([this](const nlohmann::json& evt) {
        ws_client_->send_json(evt);
    });
#endif
```

**In `start()`**: No change needed — `VpnClientManager` connections are lazy, created on-demand via `connect()`.

**In `stop()`**: Add cleanup:
```cpp
#if ODC_HOOK_HAS_VPN
    vpn_manager_.reset();
#endif
```

**In `process_event()`**: Add VPN command routing *before* the existing `type == "event"` handler:

```cpp
void HookService::process_event(const nlohmann::json& event) {
    std::string type = event.value("type", "");

#if ODC_HOOK_HAS_VPN
    if (type == "vpn.connect") {
        vpn_manager_->connect(
            event.value("connection_id", ""),
            event.value("config", nlohmann::json::object()));
        return;
    }
    if (type == "vpn.disconnect") {
        vpn_manager_->disconnect(event.value("connection_id", ""));
        return;
    }
    if (type == "vpn.get_status") {
        auto status = vpn_manager_->get_status(
            event.value("connection_id", ""));
        ws_client_->send_json(status);
        return;
    }
#endif

    if (type == "event" && !event.value("topic", "").empty()) {
        // existing behavior: log tunnel.events, services events
        ...
    }
    // ...
}
```

**Verification**: End-to-end test with registry-manager. Send` {"type":"vpn.connect","connection_id":"wg-test",...}` via WebSocket, verify odc-hook receives it, `VpnClientManager` dispatches to the appropriate client, and a `{"type":"vpn.state_change",...}` is sent back over the WebSocket.

---

### Task 14: CMake Integration — Wired Together

**Goal**: Update `odc-hook/CMakeLists.txt` to compile and link all VPN sources into the odc-hook binary.

#### Edit `odc-hook/CMakeLists.txt`:

```cmake
add_executable(odc-hook
    main.cpp
    hook_service.cpp
    ws_client.cpp
    event_bus.cpp
    thread_pool.cpp
)

if(BUILD_ODC_HOOK_VPN)
    target_sources(odc-hook PRIVATE
        # VPN manager
        vpn/vpn_manager.cpp
        # Clients
        vpn/openvpn_client.cpp
        vpn/wireguard_client.cpp
        # Platform TUN
        vpn/tun_device.cpp
        # Userspace WireGuard (Windows/macOS)
        vpn/wg_noise.cpp
        vpn/wg_aead.cpp
        vpn/wg_device.cpp
        vpn/wg_peer.cpp
    )
    target_link_libraries(odc-hook PRIVATE
        openvpn3_client
        wg_crypto
        sodium::sodium
    )
    target_compile_definitions(odc-hook PRIVATE ODC_HOOK_HAS_VPN=1)
endif()

target_link_libraries(odc-hook PRIVATE
    odc-common
    CLI11::CLI11
    asio::asio
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_features(odc-hook PRIVATE cxx_std_20)

if(WIN32)
    target_link_libraries(odc-hook PRIVATE ws2_32)
endif()

install(TARGETS odc-hook RUNTIME DESTINATION bin)
install(FILES config/odc-hook.yaml DESTINATION /etc/odc-root)
```

**Verification**: `cmake --build build --target odc-hook` links successfully on Linux with `ODC_HOOK_HAS_VPN=1` defined. The binary is ~1MB larger than before.

---

### Task 15: Docker Cross-Compilation Updates

**Goal**: Update all Dockerfiles and helper scripts to build libsodium for Windows/macOS cross-compilation.

Files to edit:

| File | Change |
|------|--------|
| `deploy/linux/Dockerfile` | Add `libsodium-dev` to apt install line |
| `deploy/windows/Dockerfile` | Add libsodium cross-compile block after OpenSSL (see Task 3.3) |
| `deploy/macos/Dockerfile` | Add libsodium cross-compile block in deps-builder stage (see Task 3.4) |
| `deploy/common/build-common.sh` | Add `build_libsodium_cross()` helper (see Task 3.5) |

**Verification**: `make -f deploy/Makefile linux` produces a binary with VPN support. `make -f deploy/Makefile windows` cross-compiles successfully and produces `odc-hook.exe` with statically linked `openvpn3_client`, `libsodium`, and VPN code.

---

### Task 16: Bundling Updates

**Goal**: Include new VPN-related files in the output bundle.

#### Edit Dockerfile RUN blocks

Add to each Dockerfile's `cp` commands (alongside the existing binary/config/service copies):

**Linux** (`deploy/linux/Dockerfile`):
```dockerfile
    && cp deploy/linux/install.sh /output/ \
    && mkdir -p /output/vpn
```

**Windows** (`deploy/windows/Dockerfile`):
```dockerfile
    && cp deploy/windows/install.ps1 /output/ \
    && mkdir -p /output/vpn
```

No additional config files are needed for VPN — connection configs are received dynamically via WebSocket JSON, not from disk.

**Verification**: After build, output directories contain the expected VPN-enabled binary and unchanged support files.

---

## File Inventory Summary

### New Files (20 total)

| # | Path | Est. Lines | Purpose |
|---|------|------------|---------|
| 1 | `thirdparty/minimal-openvpn3/**` | ~200 headers + 4 src | OpenVPN3 client library |
| 2 | `thirdparty/wg-tools/*.{c,h}` | ~10 files | WireGuard crypto C sources |
| 3 | `odc-hook/vpn/tun_device.hpp` | ~60 | Cross-platform TUN header |
| 4 | `odc-hook/vpn/tun_device.cpp` | ~250 | Linux/Win/macOS TUN impl |
| 5 | `odc-hook/vpn/wg_types.hpp` | ~50 | WG protocol constants |
| 6 | `odc-hook/vpn/wg_aead.hpp` | ~20 | AEAD encrypt/decrypt |
| 7 | `odc-hook/vpn/wg_aead.cpp` | ~80 | chacha20poly1305 wrapper |
| 8 | `odc-hook/vpn/wg_noise.hpp` | ~40 | Noise_IKpsk2 handshake |
| 9 | `odc-hook/vpn/wg_noise.cpp` | ~350 | Handshake state machine |
| 10 | `odc-hook/vpn/wg_peer.hpp` | ~40 | Per-peer state + timers |
| 11 | `odc-hook/vpn/wg_peer.cpp` | ~150 | Peer state machine |
| 12 | `odc-hook/vpn/wg_device.hpp` | ~50 | Userspace WG device |
| 13 | `odc-hook/vpn/wg_device.cpp` | ~500 | TUN+UDP I/O loops |
| 14 | `odc-hook/vpn/wireguard_client.hpp` | ~40 | Unified WG client |
| 15 | `odc-hook/vpn/wireguard_client.cpp` | ~300 | Linux kernel + userspace WG |
| 16 | `odc-hook/vpn/openvpn_client.hpp` | ~40 | OpenVPN client wrapper |
| 17 | `odc-hook/vpn/openvpn_client.cpp` | ~250 | OpenVPN3 integration |
| 18 | `odc-hook/vpn/vpn_manager.hpp` | ~40 | Connection orchestrator |
| 19 | `odc-hook/vpn/vpn_manager.cpp` | ~200 | JSON↔C++ bridge |
| 20 | `odc-root/thirdparty/wg_netlink.hpp` | copy | Linux kernel WG netlink (for Task 10 Linux path) |

### Edited Files (7 total)

| # | Path | Change |
|---|------|--------|
| 21 | `odc-root/CMakeLists.txt` | Add `openvpn3_client`, `wg_crypto`, `sodium::sodium` targets |
| 22 | `odc-root/odc-hook/CMakeLists.txt` | Add VPN sources + link VPN libs |
| 23 | `odc-hook/hook_service.hpp` | Add `VpnClientManager` member |
| 24 | `odc-hook/hook_service.cpp` | Init/start/stop VPN manager; route VPN messages |
| 25 | `deploy/linux/Dockerfile` | Add `libsodium-dev` to apt |
| 26 | `deploy/windows/Dockerfile` | Add libsodium cross-compile block |
| 27 | `deploy/macos/Dockerfile` | Add libsodium cross-compile block in deps-builder |
| 28 | `deploy/common/build-common.sh` | Add `build_libsodium_cross()` |

### No changes needed

- `deploy/Makefile` — extracts entire `FROM scratch` image including all new files
- `deploy/docker-compose.yml` — no changes needed
- All entrypoint.sh scripts — bundle config + install, VPN uses dynamic WS JSON

---

## Build Size Impact

| Component | Linux | Windows | macOS |
|-----------|-------|---------|-------|
| `openvpn3_client` (static) | ~400 KB | ~450 KB | ~420 KB |
| `wg_crypto` (static) | ~60 KB | ~70 KB | ~70 KB |
| `wg_noise + wg_aead + wg_device + wg_peer` | ~200 KB | ~200 KB | ~200 KB |
| `libsodium` (static) | ~300 KB | ~350 KB | ~320 KB |
| **Total per-binary increase** | **~960 KB** | **~1.1 MB** | **~1.0 MB** |

---

## Build Time Impact

| Platform | Added time | Notes |
|-----------|-----------|-------|
| Linux | +2 min | apt install libsodium-dev + compile ~15 new .cpp/.c |
| Windows | +5 min | libsodium cross-compile from source + compile new sources |
| macOS | +5 min | libsodium cross-compile from source + compile new sources |

---

## Event Bus Wire Format

### registry-manager → odc-hook (Commands)

```json
// Open a VPN connection
{
  "type": "vpn.connect",
  "connection_id": "wg-main",
  "vpn_type": "wireguard",
  "config": {
    "private_key": "yAnz5TF+lXXJte...=",
    "listen_port": 0,
    "peers": [{
      "public_key": "xTIBdKNS...=",
      "preshared_key": "",
      "endpoint": "10.0.0.1:51820",
      "allowed_ips": ["10.0.0.0/24"],
      "persistent_keepalive_interval": 25
    }],
    "tun": {
      "name": "odc-wg-main",
      "address": "10.0.0.2",
      "prefix_len": 24,
      "mtu": 1420
    }
  }
}

// Close a VPN connection
{"type": "vpn.disconnect", "connection_id": "wg-main"}

// Query status
{"type": "vpn.get_status", "connection_id": "wg-main"}
```

### odc-hook → registry-manager (Events)

```json
// State transition
{
  "type": "vpn.state_change",
  "connection_id": "wg-main",
  "vpn_type": "wireguard",
  "old_state": "idle",
  "new_state": "connected",
  "error": "",
  "timestamp": 1751990400
}

// Periodic stats
{
  "type": "vpn.stats",
  "connection_id": "wg-main",
  "bytes_in": 1048576,
  "bytes_out": 524288,
  "packets_in": 8192,
  "packets_out": 4096,
  "duration_sec": 300.5
}
```

---

## Execution Order

| Order | Task | Dependencies | Verifiable |
|-------|------|-------------|------------|
| 1 | Import OpenVPN3 sources | None | cmake --target openvpn3_client compiles |
| 2 | Import wg-tools sources | None | cmake --target wg_crypto compiles |
| 3 | Add libsodium (CMake + Docker) | None | find_package(sodium) succeeds |
| 4 | TUN device abstraction | Tasks 1-3 | TunDevice::open() creates interface on Linux |
| 5 | WG types header | None | Compiles |
| 6 | WG AEAD (chacha20poly1305) | Tasks 3, 5 | Encrypt/decrypt round-trip test |
| 7 | WG Noise handshake | Tasks 3, 5, 6 | Handshake against test vectors |
| 8 | WG Peer state machine | Tasks 3, 5-7 | State transitions test |
| 9 | WG Device (userspace) | Tasks 3-8 | TUN↔UDP packet flow test |
| 10 | WireGuard client wrapper | Tasks 3-9 | start/stop + state/stats callbacks |
| 11 | OpenVPN client wrapper | Tasks 1, 4 | start/stop with .ovpn profile |
| 12 | VpnClientManager | Tasks 10-11 | connect/disconnect/get_status JSON↔C++ |
| 13 | HookService event bus wiring | Tasks 12 | WebSocket → VPN command round-trip |
| 14 | CMake integration (odc-hook target) | Tasks 1-13 | cmake --build build --target odc-hook links |
| 15 | Docker cross-compilation updates | Tasks 3, 14 | make -f deploy/Makefile linux builds |
| 16 | Bundling updates | Task 15 | Output dirs contain VPN-enabled binary |

Tasks 1-3 are independent and can proceed in parallel. Tasks 5-9 form a strict dependency chain but are only needed for Windows/macOS userspace WG (Linux uses kernel WG, skipping Tasks 6-9).

---

## Risks and Mitigations

| Risk | Severity | Mitigation |
|------|----------|------------|
| Userspace WireGuard is ~2500 lines of crypto code — bugs possible | High | Use libsodium for all crypto (well-audited). Test against known WireGuard test vectors. Unit-test handshake against running WireGuard peer. |
| wintun.dll not available in Docker build | Medium | Download from https://www.wintun.net/ during Docker build (MIT license, redistributable). Fallback: Windows build can compile without wintun (TUN operations return errors until DLL is placed beside binary at runtime). |
| macOS utun requires root/sudo | Low | Same requirement as Linux `/dev/net/tun`. Install script runs as root. |
| libsodium configure may fail for exotic MinGW-w64 cross targets | Low | Tested pattern: `./configure --host=x86_64-w64-mingw32` works for libsodium 1.0.20+. Fallback: use libsodium's CMake build system if autotools fails. |
| Binary size increase (~1MB) may be too large for constrained devices | Low | `BUILD_ODC_HOOK_VPN=OFF` removes all VPN code at compile time. |
