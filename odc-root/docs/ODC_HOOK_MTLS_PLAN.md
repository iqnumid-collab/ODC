# odc-hook + odc-registry-manager mTLS — Implementation Plan

**Goal**: Add a cross-platform `odc-hook` service (Ubuntu/Windows/macOS) that connects to `odc-registry-manager` via WebSocket with mutual TLS (mTLS). Harden the registry-manager's TLS implementation to follow industry-standard mTLS patterns per RFC 8705.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Phase 0: Cross-Platform Primitives (common lib)](#phase-0-cross-platform-primitives-common-lib)
   - [Task P0.1 — `platform.hpp` header](#task-p01--platformhpp-header)
   - [Task P0.2 — `platform_linux.cpp`](#task-p02--platform_linuxcpp)
   - [Task P0.3 — `platform_windows.cpp`](#task-p03--platform_windowscpp)
   - [Task P0.4 — `platform_macos.cpp`](#task-p04--platform_macoscpp)
   - [Task P0.5 — Update `common/CMakeLists.txt`](#task-p05--update-commoncmakeliststxt)
3. [Phase 1: Registry mTLS Hardening](#phase-1-registry-mtls-hardening)
   - [Task R1.1 — TLS context hardening](#task-r11--tls-context-hardening)
   - [Task R1.2 — Client certificate identity extraction](#task-r12--client-certificate-identity-extraction)
   - [Task R1.3 — Certificate revocation list](#task-r13--certificate-revocation-list)
   - [Task R1.4 — mTLS config schema and YAML parsing](#task-r14--mtls-config-schema-and-yaml-parsing)
   - [Task R1.5 — Per-connection cert binding and callbacks](#task-r15--per-connection-cert-binding-and-callbacks)
   - [Task R1.6 — Update `registry-manager.yaml`](#task-r16--update-registry-manageryaml)
4. [Phase 2: Cross-Platform Async HTTP Client (common lib)](#phase-2-cross-platform-async-http-client-common-lib)
   - [Task H2.1 — `HttpClient` ASIO-based HTTP/HTTPS client](#task-h21--httpclient-asio-based-httphttps-client)
   - [Task H2.2 — Replace raw POSIX HTTP in `webhook_client.cpp`](#task-h22--replace-raw-posix-http-in-webhook_clientcpp)
5. [Phase 3: odc-hook Service](#phase-3-odc-hook-service)
   - [Task H3.1 — Directory structure and build scaffolding](#task-h31--directory-structure-and-build-scaffolding)
   - [Task H3.2 — `WsClient` — mTLS WebSocket client](#task-h32--wsclient--mtls-websocket-client)
   - [Task H3.3 — `EventBus` — internal pub/sub](#task-h33--eventbus--internal-pubsub)
   - [Task H3.4 — `ThreadPool` — worker pool](#task-h34--threadpool--worker-pool)
   - [Task H3.5 — `HookService` — lifecycle orchestration](#task-h35--hookservice--lifecycle-orchestration)
   - [Task H3.6 — `main.cpp` — entry point](#task-h36--maincpp--entry-point)
   - [Task H3.7 — `odc-hook.yaml` — config template](#task-h37--odc-hookyaml--config-template)
   - [Task H3.8 — `CMakeLists.txt`](#task-h38--cmakeliststxt)
   - [Task H3.9 — systemd unit file](#task-h39--systemd-unit-file)
6. [Phase 4: Tests](#phase-4-tests)
   - [Task T4.1 — EventBus unit tests](#task-t41--eventbus-unit-tests)
   - [Task T4.2 — ThreadPool unit tests](#task-t42--threadpool-unit-tests)
   - [Task T4.3 — WsClient unit tests](#task-t43--wsclient-unit-tests)
   - [Task T4.4 — End-to-end mTLS integration test](#task-t44--end-to-end-mtls-integration-test)
7. [Phase 5: Cross-Compilation Validation](#phase-5-cross-compilation-validation)
   - [Task V5.1 — MinGW Windows cross-compile](#task-v51--mingw-windows-cross-compile)
   - [Task V5.2 — macOS cross-compile check](#task-v52--macos-cross-compile-check)
8. [Appendix A: mTLS Industry Standard Notes](#appendix-a-mtls-industry-standard-notes)
9. [Appendix B: Cross-Platform API Reference](#appendix-b-cross-platform-api-reference)

---

## Architecture Overview

```
┌───────────────────────────────────────────────────────────────────────┐
│                     odc-registry-manager                               │
│                                                                       │
│  IpcServer (Unix) ◄── odc-tunnel-manager (colocated, Linux-only)     │
│                                                                       │
│  WsServer (mTLS, hardened) ◄── odc-hook (remote/local, cross-plat)   │
│  ┌──────────────────────────┐                                         │
│  │ TLS 1.2+ minimum          │                                        │
│  │ Client cert required      │                                        │
│  │ Identity extracted (CN,   │                                        │
│  │   SHA-256 fingerprint)    │                                        │
│  │ Revocation check (CRL +   │                                        │
│  │   in-memory deny-list)    │                                        │
│  │ Per-connection cert info  │                                        │
│  └──────────────────────────┘                                         │
│                                                                       │
│  WebhookClient (mTLS outbound) ──► External HTTPS targets             │
│  (ASIO-based, cross-platform)                                         │
└───────────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────────┐
│                          odc-hook                                       │
│                                                                       │
│  WsClient (mTLS) ──► odc-registry-manager:9443                        │
│  ┌──────────────────────────────────────────────────────────────────┐ │
│  │ Presents client cert (server verifies us)                         │ │
│  │ Verifies server cert against CA (we verify them)                  │ │
│  │ Auto-reconnect with backoff                                       │ │
│  │ WebSocket frames: text + ping/pong keepalive                       │ │
│  └──────────────────────────────────────────────────────────────────┘ │
│           │                                                            │
│           ▼                                                            │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │ EventBus (internal, thread-safe)                                  │  │
│  │ - Bounded MPMC queue (configurable capacity)                      │  │
│  │ - Backpressure: block on full queue / drop with try_publish       │  │
│  │ - All JSON events pass through a single channel                   │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│           │                                                            │
│           ▼                                                            │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │ ThreadPool (configurable thread count)                            │  │
│  │ - N worker threads consume from EventBus                          │  │
│  │ - Each thread: consume(timeout) → process_event(json)             │  │
│  │ - Graceful shutdown: drain remaining events, then exit            │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                                                                       │
│  Health Server (mTLS, optional) — GET /health → status JSON           │
└───────────────────────────────────────────────────────────────────────┘
```

**Key design decisions**:

| Decision | Choice | Rationale |
|---|---|---|
| Transport | ASIO `ssl::stream<tcp::socket>` | Cross-platform, already a dependency |
| IPC (tunnel↔registry) | Unix domain sockets, unchanged | Colocated, Linux-only, don't break working code |
| Hook↔Registry transport | WebSocket over TCP + mTLS | Cross-platform, firewall-friendly, standard |
| Internal bus | Mutex + condition_variable bounded queue | Sufficient for <10K events/sec, simpler than lock-free |
| Worker threading | Fixed-size thread pool | Configurable, avoids unbounded thread creation |
| mTLS standard | RFC 8705 (OAuth 2.0 mTLS) | Industry standard for service-to-service mTLS |
| Certificate format | PEM files, SHA-256 fingerprint | Matches existing CertManager |
| Platform abstraction | `#ifdef` in `platform.hpp/.cpp` | Minimal abstraction surface, no heavy framework |

---

## Phase 0: Cross-Platform Primitives (common lib)

### Task P0.1 — `platform.hpp` header

**File**: `common/include/odc/platform.hpp`

Create a header that provides:
- Compile-time platform detection (`ODC_PLATFORM_LINUX`, `ODC_PLATFORM_WINDOWS`, `ODC_PLATFORM_MACOS`)
- `odc::platform::default_config_path()` — returns platform-specific default config dir
- `odc::platform::default_data_path()` — returns platform-specific data/state dir
- `odc::platform::default_runtime_path()` — returns platform-specific runtime dir
- `odc::platform::daemonize()` — daemonize on Linux/macOS, no-op on Windows (returns bool)
- `odc::platform::setup_signal_handler(asio::signal_set&, callback)` — register SIGINT/SIGTERM or console handler

**Verification**: Header compiles on Linux with `#include "odc/platform.hpp"`.

---

### Task P0.2 — `platform_linux.cpp`

**File**: `common/src/platform_linux.cpp`

Implement Linux-specific:
- `default_config_path()` → `"/etc/odc-root"`
- `default_data_path()` → `"/var/lib/odc-root"`
- `default_runtime_path()` → `"/run/odc-root"`
- `daemonize()` → `fork()`, child calls `setsid()`, closes stdin/stdout/stderr, parent exits
- `setup_signal_handler()` → `signals.add(SIGINT)`, `signals.add(SIGTERM)`, `signals.async_wait(callback)`

**Verification**: Compiles and links into `odc-common`. Not tested at runtime yet.

---

### Task P0.3 — `platform_windows.cpp`

**File**: `common/src/platform_windows.cpp`

Implement Windows-specific:
- `default_config_path()` → `"C:\\ProgramData\\odc-root"`
- `default_data_path()` → `"C:\\ProgramData\\odc-root\\data"`
- `default_runtime_path()` → `"C:\\ProgramData\\odc-root\\run"`
- `daemonize()` → Logs "Daemon mode not supported on Windows — running in foreground" and returns true
- `setup_signal_handler()` → calls `SetConsoleCtrlHandler()` to catch Ctrl+C and service stop events

Include `<windows.h>`, `<winsock2.h>` guarded by `#ifdef _WIN32`.

**Verification**: Compiles when cross-compiled for Windows (MinGW/MSVC).

---

### Task P0.4 — `platform_macos.cpp`

**File**: `common/src/platform_macos.cpp`

Implement macOS-specific:
- `default_config_path()` → `"/usr/local/etc/odc-root"`
- `default_data_path()` → `"/usr/local/var/lib/odc-root"`
- `default_runtime_path()` → `"/usr/local/var/run/odc-root"`
- `daemonize()` → Same as Linux (`fork()` + `setsid()`)
- `setup_signal_handler()` → Same as Linux

**Verification**: Compiles on macOS with Clang.

---

### Task P0.5 — Update `common/CMakeLists.txt`

**File**: `common/CMakeLists.txt`

Add platform-specific source files:
```cmake
if(WIN32)
    target_sources(odc-common PRIVATE src/platform_windows.cpp)
    target_link_libraries(odc-common PUBLIC ws2_32)
elseif(APPLE)
    target_sources(odc-common PRIVATE src/platform_macos.cpp)
else()
    target_sources(odc-common PRIVATE src/platform_linux.cpp)
endif()
```

Add `target_include_directories` to expose `platform.hpp` and verify `find_package(asio)` is in the top-level CMakeLists (already present).

**Verification**: `cmake --build build --target odc-common` succeeds on Linux.

---

## Phase 1: Registry mTLS Hardening

### Task R1.1 — TLS context hardening

**File**: `registry-manager/ws_server.cpp`, method `setup_ssl_context()`

Changes to the `setup_ssl_context()` method in `WsServer`:

| # | Change | Detail |
|---|---|---|
| 1 | Set minimum TLS version | `SSL_CTX_set_min_proto_version(ssl_ctx_->native_handle(), TLS1_2_VERSION)` — enforce TLS 1.2+ |
| 2 | Disable TLS 1.0/1.1 via options | Add `asio::ssl::context::no_tlsv1` and `asio::ssl::context::no_tlsv1_1` to `set_options()` |
| 3 | Disable client-initiated renegotiation | `SSL_CTX_set_options(native, SSL_OP_NO_RENEGOTIATION)` |
| 4 | Set cipher suite list | `SSL_CTX_set_cipher_list(native, "ECDHE+AESGCM:!aNULL:!MD5:!RC4:!3DES")` |
| 5 | Set ECDH curve | `SSL_CTX_set1_curves(native, NID_X9_62_prime256v1)` (P-256) |
| 6 | Disable session tickets | `SSL_CTX_set_options(native, SSL_OP_NO_TICKET)` |
| 7 | Set verify depth | `SSL_CTX_set_verify_depth(native, mtls_verify_depth)` (max 2) |

The method takes new parameters from `WsServerConfig.mtls` section.

**Verification**: Server starts. Test with `openssl s_client -connect localhost:9443 -tls1_2` — must connect with TLS 1.2+. `openssl s_client -connect localhost:9443 -tls1_1` — must fail.

---

### Task R1.2 — Client certificate identity extraction

**File**: `registry-manager/ws_server.cpp`, method `handle_accept()`

After the TLS handshake succeeds, extract the client certificate:

1. Get native SSL handle: `SSL* ssl = socket->native_handle();`
2. Get peer cert: `X509* client_cert = SSL_get_peer_certificate(ssl);`
3. If `client_cert == nullptr` and mTLS is enabled → fail and close connection
4. Compute SHA-256 fingerprint: `X509_digest(client_cert, EVP_sha256(), digest, &len)`
5. Format fingerprint as colon-separated hex: `aa:bb:cc:...` (matches `CertManager::format_fingerprint`)
6. Extract Subject CN: `X509_NAME_get_text_by_NID(name, NID_commonName, buf, sizeof(buf))`
7. Extract validity: `X509_get_notBefore()` / `X509_get_notAfter()`
8. Extract serial: `ASN1_INTEGER_get(X509_get_serialNumber(cert))`
9. Store in `ClientCertInfo` struct
10. Add to `connection_certs_` map: `connection_certs_[conn_id] = cert_info`
11. Free: `X509_free(client_cert)`

New data structure in `ws_server.hpp` (before `private:`):
```cpp
struct ClientCertInfo {
    std::string fingerprint;       // SHA-256 colon-separated hex
    std::string common_name;       // Subject CN
    int64_t not_before;            // Unix timestamp
    int64_t not_after;             // Unix timestamp
    std::string serial;            // Hex serial number
};
```

**Verification**: Connect with a valid client cert, inspect log output for fingerprint and CN.

---

### Task R1.3 — Certificate revocation list

**File**: New file `registry-manager/cert_revocation.hpp/.cpp`

```cpp
namespace odc {

class CertRevocationList {
public:
    void deny(const std::string& fingerprint);
    void allow(const std::string& fingerprint);
    bool is_denied(const std::string& fingerprint) const;

    // Load CRL from PEM file
    bool load_crl(const std::string& crl_path, X509_STORE* store);

    size_t denied_count() const;

private:
    std::unordered_set<std::string> denied_;
    mutable std::shared_mutex mutex_;
};

} // namespace odc
```

**Implementation notes**:
- `deny()` / `allow()`: modify in-memory `std::unordered_set` under exclusive lock
- `is_denied()`: check set under shared lock (fast path)
- `load_crl()`: parse CRL PEM file using `PEM_read_X509_CRL()`, add entries to deny list. Call `X509_STORE_add_crl()` if store provided
- Thread-safe: `shared_mutex` for read-heavy workload

**Integration**: In `setup_ssl_context()`, if `mtls.crl_path` is configured, load CRL. In `handle_accept()`, after extracting fingerprint, check `revocation_list_->is_denied(fingerprint)` — if denied, close connection.

**Verification**: Revoke a cert fingerprint, attempt connection — connection rejected.

---

### Task R1.4 — mTLS config schema and YAML parsing

**File**: `registry-manager/ws_server.hpp` — extend `WsServerConfig`

Add nested struct:
```cpp
struct MtlsConfig {
    bool enabled{true};
    std::string ca_cert_path;
    int verify_depth{2};
    std::string crl_path;
    std::string cipher_list{"ECDHE+AESGCM:!aNULL:!MD5:!RC4"};
    bool require_client_cert{true};
};
```

Add to `WsServerConfig`:
```cpp
MtlsConfig mtls;
```

**File**: `registry-manager/registry_manager.cpp` — `setup_websocket()` method

Parse the `mtls:` block from YAML config into `MtlsConfig`. Use existing `get_value<>()` helper from yaml_config.hpp. All fields have sensible defaults so YAML can omit the entire block.

**Verification**: Load YAML config with and without `mtls:` block — both parse correctly. `mtls.enabled: false` should allow non-mTLS connections (dev/debug mode).

---

### Task R1.5 — Per-connection cert binding and callbacks

**File**: `registry-manager/ws_server.hpp`

Add a callback so `RegistryManager` can react to mTLS identities:

```cpp
using CertCallback = std::function<void(const std::string& connection_id,
                                         const ClientCertInfo& cert)>;
void set_on_client_cert(CertCallback cb) { on_client_cert_ = std::move(cb); }
```

Invoke the callback in `handle_accept()` after cert extraction succeeds. The registry-manager can then:
- Log the authenticated client identity
- Associate the connection with a registered hook client in the database
- Enforce authorization: only allow subscribed topics for this identity

Add getter:
```cpp
std::optional<ClientCertInfo> get_client_cert(const std::string& connection_id) const;
```

Thread safety: `connection_certs_` protected by `conn_mutex_` (same mutex as `connections_`).

**Verification**: Integrate callback in `RegistryManager::setup_websocket()` — on cert received, log identity.

---

### Task R1.6 — Update `registry-manager.yaml`

**File**: `registry-manager/config/registry-manager.yaml`

Replace the `verify_client_cert` / `ca_cert_path` flat fields with the nested `mtls:` block:

```yaml
websocket:
  listen_port: 9443
  listen_address: 0.0.0.0
  tls_cert_path: /etc/odc-root/ws/server.crt
  tls_key_path: /etc/odc-root/ws/server.key
  url_path: /ws
  max_connections: 1000
  idle_timeout_sec: 300
  mtls:
    enabled: true
    ca_cert_path: /etc/odc-root/ca/ca.crt
    verify_depth: 2
    crl_path: ""
    cipher_list: "ECDHE+AESGCM:!aNULL:!MD5:!RC4"
    require_client_cert: true
```

Remove the old flat `verify_client_cert` and `ca_cert_path` from the websocket section (backward compat: if present, migrate to `mtls.*` equivalents).

**Verification**: Load config, verify `WsServerConfig.mtls.enabled == true` and all fields are populated.

---

## Phase 2: Cross-Platform Async HTTP Client (common lib)

### Task H2.1 — `HttpClient` ASIO-based HTTP/HTTPS client

**File**: `common/include/odc/http_client.hpp`

The current `webhook_client.cpp` uses raw POSIX sockets (`socket()`, `connect()`, `send()`, `recv()`) with no TLS support. This must be replaced with an ASIO-based client that supports HTTPS with mTLS.

```cpp
namespace odc {

struct HttpClientConfig {
    std::chrono::seconds connect_timeout{10};
    std::chrono::seconds request_timeout{30};
    bool verify_peer{true};
    std::string ca_cert_path;
    std::string client_cert_path;     // For mTLS outbound
    std::string client_key_path;      // For mTLS outbound
};

struct HttpResponse {
    int status_code{0};
    std::string status_message;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

class HttpClient {
public:
    explicit HttpClient(asio::io_context& io, const HttpClientConfig& cfg = {});

    // Synchronous POST (blocks until response or timeout, runs io_context internally)
    HttpResponse post(const std::string& url,
                      const std::string& body,
                      const std::unordered_map<std::string, std::string>& extra_headers = {});

private:
    asio::io_context& io_;
    HttpClientConfig cfg_;
    asio::ssl::context ssl_ctx_{asio::ssl::context::tls_client};
};

} // namespace odc
```

**Implementation notes**:
- `ssl_ctx_` configured with CA cert for server verification, plus client cert+key if provided (mTLS)
- `post()`: parse URL, DNS resolve via `asio::ip::tcp::resolver`, connect, SSL handshake, send HTTP/1.1 request, read response headers (Content-Length or chunked), read body
- Timeout via `asio::steady_timer` that cancels the socket on expiry
- Redirect handling: follow up to 5 redirects (301, 302, 307, 308)
- All ASIO API — no POSIX sockets

**Verification**: Unit test: POST to a local echo server, verify request/response.

---

### Task H2.2 — Replace raw POSIX HTTP in `webhook_client.cpp`

**File**: `registry-manager/webhook_client.cpp`

Replace the `http_post()` method (raw POSIX sockets) with `HttpClient::post()`:

1. Add `asio::io_context` member to `WebhookClient`
2. Create `HttpClient` instance with mTLS config from webhook config
3. Replace `http_post()` body with `http_client_->post(url, body, {{"X-ODC-Signature", signature}})` 
4. Remove all POSIX socket code: `getaddrinfo()`, `socket()`, `connect()`, `send()`, `recv()`, `close()`, `setsockopt()`, `<sys/socket.h>`, `<netdb.h>`, `<unistd.h>`
5. Keep the HMAC computation and retry logic unchanged

**Verification**: `cmake --build build --target odc-registry-manager` compiles on Linux. WebhookClient still delivers events (test with a mock HTTP server).

---

## Phase 3: odc-hook Service

### Task H3.1 — Directory structure and build scaffolding

**Create directories**:
```
odc-hook/
├── CMakeLists.txt
├── main.cpp
├── hook_service.hpp
├── hook_service.cpp
├── ws_client.hpp
├── ws_client.cpp
├── event_bus.hpp
├── event_bus.cpp
├── thread_pool.hpp
├── thread_pool.cpp
├── config/
│   └── odc-hook.yaml
```

**Top-level CMakeLists.txt**: add `add_subdirectory(odc-hook)`.

**Verification**: Directory tree exists, cmake configures without error.

---

### Task H3.2 — `WsClient` — mTLS WebSocket client

**Files**: `odc-hook/ws_client.hpp`, `odc-hook/ws_client.cpp`

Cross-platform WebSocket client with mutual TLS using ASIO `ssl::stream`.

**Public API** (see design above in the architecture section):

Core responsibilities:
1. **SSL context setup**: Load client cert + key (to present to server), load CA cert (to verify server), enforce TLS 1.2+, set cipher list
2. **TCP connection**: `asio::ip::tcp::resolver` → `asio::connect()`
3. **TLS handshake**: `socket.async_handshake(ssl::stream_base::client, ...)`
4. **WebSocket handshake**: Send HTTP upgrade request with `Sec-WebSocket-Key`, verify `101` response, extract `Sec-WebSocket-Accept`
5. **Frame read/write**: 
   - Send: mask frames per RFC 6455 (client always masks), support text opcode
   - Receive: unmask frames, reassemble fragmented frames
   - Handle close frames, ping/pong (auto-reply to pings)
6. **Auto-reconnect**: On disconnect, wait `reconnect_interval_sec`, retry. Exponential backoff up to 60s
7. **Keepalive**: Send WebSocket ping every `idle_timeout_sec / 2` seconds

**WebSocket frame encode (private)**:
```cpp
static std::string ws_frame(const std::string& payload, uint8_t opcode);
static bool ws_parse_frame(const char* data, size_t len,
                           std::string& payload, uint8_t& opcode, bool& fin);
```

**Cross-platform**: Zero POSIX-specific APIs. All I/O via ASIO. SSL via OpenSSL.

**Verification**: Connect to a test WebSocket echo server (e.g., `websocat`). Send text frame, receive same frame. Test reconnect: kill server, wait, restart, client reconnects.

---

### Task H3.3 — `EventBus` — internal pub/sub

**Files**: `odc-hook/event_bus.hpp`, `odc-hook/event_bus.cpp`

Thread-safe bounded queue for passing events from the WebSocket receive handler to worker threads.

**API** (as designed above):

Key implementation details:
- **Data structure**: `std::deque<nlohmann::json>` + `std::mutex` + two `std::condition_variable` (producer and consumer)
- **`publish()`**: Lock, push to back, notify one consumer. If queue full, wait on producer CV (backpressure)
- **`try_publish()`**: Lock, if not full push and return true; else return false (no block)
- **`consume()`**: Lock, if queue empty wait on consumer CV with timeout. If shutdown signaled, return null json. Pop front.
- **`try_consume()`**: Lock, if not empty pop and return; else return false
- **`shutdown()`**: Set flag, notify all consumers and producers
- **Statistics**: `published_count_` and `consumed_count_` as `std::atomic<uint64_t>`

**Verification**: Unit test — multiple producers, multiple consumers, verify all events delivered exactly once, backpressure works, shutdown drains cleanly.

---

### Task H3.4 — `ThreadPool` — worker pool

**Files**: `odc-hook/thread_pool.hpp`, `odc-hook/thread_pool.cpp`

Fixed-size thread pool that consumes events from the EventBus and passes them to a handler function.

**API** (as designed above):

Implementation details:
- `start()`: create N threads, each running `worker_loop()`
- `worker_loop()`: while running, call `bus_.consume(timeout)` → if event valid, increment `active_count_`, call `handler_(event)`, decrement `active_count_`
- `stop()`: set `running_ = false`, wait for all threads to join
- `wait()`: join all threads (blocking)
- Thread count default: `std::thread::hardware_concurrency()` if config value is 0
- Handler is a single `std::function` — all events go through the same processing pipeline

**Verification**: Unit test — publish events to bus, thread pool processes them, verify handler call count matches published count.

---

### Task H3.5 — `HookService` — lifecycle orchestration

**Files**: `odc-hook/hook_service.hpp`, `odc-hook/hook_service.cpp`

Orchestrates WsClient, EventBus, ThreadPool. Owns the asio `io_context`.

**Lifecycle**:

```
initialize()
  ├── load_config()          → YAML → struct fields
  ├── setup_logging()        → spdlog level from config
  ├── load_certs()           → CA + client cert from file paths in config
  ├── create_event_bus()     → EventBus(queue_capacity)
  ├── create_thread_pool()   → ThreadPool(num_threads, bus, handler)
  └── create_ws_client()     → WsClient(io_ctx_, ws_config)

start()
  ├── ws_client->connect()   → TCP + TLS + WS handshake
  ├── ws_client->send(       → subscribe to all topics
  │     {"type":"subscribe","topic":"*"})
  └── thread_pool->start()   → launch worker threads

run()
  └── io_ctx_.run()          → blocking ASIO event loop

stop()
  ├── ws_client->disconnect()
  ├── thread_pool->stop()
  ├── event_bus->shutdown()
  └── io_ctx_.stop()
```

**Default event handler**:
```cpp
void process_event(const nlohmann::json& event) {
    auto logger = get_logger("hook");
    std::string type = event.value("type", "unknown");
    std::string topic = event.value("topic", "");
    SPDLOG_LOGGER_INFO(logger, "Event: topic={} type={}", topic, type);
    // Future: dispatch to plugin system
}
```

**Verification**: Service initializes, starts, connects to registry (if available), receives events, stops cleanly.

---

### Task H3.6 — `main.cpp` — entry point

**File**: `odc-hook/main.cpp`

Entry point with:
- CLI11 argument parsing: `--config`, `--daemon`, `--generate-cert`, `--version`
- Platform-specific config path default (via `platform.hpp`)
- Signal handling via `asio::signal_set`
- Daemon mode via `platform::daemonize()`
- `--generate-cert`: use `CertManager` to generate client cert+key signed by the registry CA, write to disk, print paths, exit

**Verification**: `./odc-hook --help` prints usage. `./odc-hook --version` prints version.

---

### Task H3.7 — `odc-hook.yaml` — config template

**File**: `odc-hook/config/odc-hook.yaml`

```yaml
version: 1

general:
  log_level: info
  pid_file: ""           # Populated by platform default at runtime

server:
  host: 127.0.0.1
  port: 9443
  uri: /ws
  reconnect_interval_sec: 5
  connect_timeout_sec: 10
  idle_timeout_sec: 300

mtls:
  client_cert_path: /etc/odc-root/hook/client.crt
  client_key_path: /etc/odc-root/hook/client.key
  ca_cert_path: /etc/odc-root/ca/ca.crt
  verify_server: true

event_bus:
  queue_capacity: 4096

thread_pool:
  num_threads: 0           # 0 = auto (std::thread::hardware_concurrency)
  event_timeout_ms: 1000

health:
  enabled: false
  listen_port: 9080
  listen_address: 127.0.0.1
```

**Verification**: YAML parses via yaml-cpp without error.

---

### Task H3.8 — `CMakeLists.txt`

**File**: `odc-hook/CMakeLists.txt`

```cmake
add_executable(odc-hook
    main.cpp
    hook_service.cpp
    ws_client.cpp
    event_bus.cpp
    thread_pool.cpp
)

target_link_libraries(odc-hook PRIVATE
    odc-common
    CLI11::CLI11
    asio::asio
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_features(odc-hook PRIVATE cxx_std_20)

# Platform-specific
if(WIN32)
    target_link_libraries(odc-hook PRIVATE ws2_32)
endif()

install(TARGETS odc-hook RUNTIME DESTINATION bin)
install(FILES config/odc-hook.yaml DESTINATION /etc/odc-root)
```

**Verification**: `cmake --build build --target odc-hook` produces binary.

---

### Task H3.9 — systemd unit file

**File**: `configs/systemd/odc-hook.service`

```ini
[Unit]
Description=odc-hook — Cross-platform mTLS WebSocket event hook client
After=network-online.target odc-registry-manager.service
Wants=network-online.target odc-registry-manager.service

[Service]
Type=simple
ExecStart=/usr/bin/odc-hook --config /etc/odc-root/odc-hook.yaml --daemon
Restart=on-failure
RestartSec=10
User=odc-root
Group=odc-root

[Install]
WantedBy=multi-user.target
```

---

## Phase 4: Tests

### Task T4.1 — EventBus unit tests

**File**: `tests/test_event_bus.cpp`

| Test | Description |
|---|---|
| `publish_consume_single` | Publish 1 event, consume it, verify equality |
| `publish_consume_many` | Publish 100 events, consume 100, verify count |
| `try_publish_when_full` | Fill queue to capacity, try_publish returns false |
| `backpressure` | Publish from thread A (blocks when full), consume from thread B, thread A unblocks |
| `shutdown_drains` | shutdown() → publish no-ops, consume returns null after drain |
| `multi_producer_multi_consumer` | 4 producers, 4 consumers, 1000 events — all delivered exactly once |
| `statistics` | Verify published_count and consumed_count match after all events processed |

**Verification**: All tests pass.

---

### Task T4.2 — ThreadPool unit tests

**File**: `tests/test_thread_pool.cpp`

| Test | Description |
|---|---|
| `start_stop` | Create pool, start, stop — no crash |
| `processes_events` | Publish events to bus, pool processes them, handler counter matches |
| `graceful_shutdown` | Publish events, stop pool — remaining events drained before exit |
| `concurrency` | 4 threads, 4 events with sleep(100ms) each — total time < 200ms (parallel) |
| `handler_exception` | Handler throws — pool continues, other events still processed |
| `auto_thread_count` | num_threads=0 → pool uses hardware_concurrency |

**Verification**: All tests pass.

---

### Task T4.3 — WsClient unit tests

**File**: `tests/test_ws_client.cpp`

| Test | Description |
|---|---|
| `ws_frame_encode_decode` | Encode text frame, decode, verify payload matches |
| `ws_frame_mask_unmask` | Client sends masked frame, server reads unmasked payload |
| `ws_handshake_request` | Build HTTP upgrade request, verify headers (Sec-WebSocket-Key, Upgrade, Connection) |
| `ws_handshake_response` | Parse HTTP 101 response, extract Sec-WebSocket-Accept, verify correct key derivation |
| `ws_ping_pong` | Receive ping frame → auto-send pong |
| `ssl_context_client_cert` | Create SSL context with client cert loaded, verify cert and key are present |
| `ssl_context_verify_server` | Create SSL context with CA cert, verify peer mode is set |
| `reconnect_after_disconnect` | Mock disconnect → reconnect timer fires → connect called again |

**Verification**: All tests pass.

---

### Task T4.4 — End-to-end mTLS integration test

**File**: `tests/test_hook_mtls_integration.cpp`

**Setup**:
1. Generate CA via `CertManager::create_ca()`
2. Generate server cert + key for registry-manager (signed by CA)
3. Generate client cert + key for odc-hook (signed by CA)
4. Start odc-registry-manager with mTLS enabled using the generated server cert
5. Start odc-hook with the generated client cert and CA cert

**Test flow**:
1. odc-hook connects to registry-manager via mTLS WebSocket
2. Registry extracts client cert fingerprint, verifies it against CA
3. odc-hook subscribes to `tunnel.events`
4. Tunnel-manager (or test IPC client) sends `tunnel.client.connected` notification
5. Registry logs the event, broadcasts to WebSocket subscribers
6. odc-hook receives the event via WebSocket
7. odc-hook's EventBus delivers event to ThreadPool
8. Event processed, logged by odc-hook
9. Verify the event payload matches what was sent
10. Disconnect odc-hook, verify auto-reconnect works

| Test | Description |
|---|---|
| `mtls_handshake_success` | Valid client cert → connection accepted, identity extracted |
| `mtls_handshake_reject_bad_cert` | Invalid/self-signed client cert → connection rejected |
| `mtls_handshake_reject_no_cert` | No client cert presented → connection rejected |
| `mtls_handshake_reject_revoked` | Revoked cert fingerprint → connection rejected |
| `event_flow` | Full event flow: tunnel → registry → hook |
| `reconnect_on_disconnect` | Kill registry, restart — hook reconnects within reconnect_interval |
| `hook_cert_verifies_server` | Hook rejects connection if server cert doesn't match CA |

**Verification**: All tests pass.

---

## Phase 5: Cross-Compilation Validation

### Task V5.1 — MinGW Windows cross-compile

Install MinGW-w64 toolchain on Linux:
```bash
sudo apt-get install mingw-w64 g++-mingw-w64-x86-64
```

Cross-compile:
```bash
mkdir build-win && cd build-win
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchains/mingw-w64.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

**Verification**: `odc-hook.exe` binary produced. `file odc-hook.exe` shows PE32+ executable.

Toolchain file (`cmake/toolchains/mingw-w64.cmake`):
```cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
```

---

### Task V5.2 — macOS cross-compile check

macOS cross-compilation from Linux requires osxcross. For validation:
- Verify all source files compile without Apple-specific headers (already true — ASIO+OpenSSL are portable)
- Verify no `#include <sys/capability.h>`, `<linux/*>`, `<sys/un.h>` in odc-hook or common library code
- If osxcross is not available, validate by code review: grep for Linux-only headers

**Verification**: `grep -rn "sys/socket.h\|sys/un.h\|sys/capability.h\|linux/\|poll.h" odc-hook/ common/include/ common/src/` returns zero results from odc-hook and common lib (excluding Linux platform file and IPC files which are Linux-only by design).

---

## Appendix A: mTLS Industry Standard Notes

### RFC 8705 (OAuth 2.0 Mutual-TLS Client Authentication and Certificate-Bound Access Tokens)

| Section | Requirement | Implementation |
|---|---|---|
| 2.1 | PKI method: client presents cert signed by trusted CA | Registry's `verify_peer` mode + CA cert |
| 2.2 | Self-signed method: match raw certificate against registered JWK | Supported via fingerprint whitelist (future) |
| 3.1 | Certificate thumbprint: SHA-256 of DER-encoded cert | `CertManager::issue_client_cert()` already computes this |
| 5 | Separate mTLS endpoint aliases | Registry uses port 9443 for mTLS; could add separate port for non-mTLS in future |
| 6.1 | TLS 1.2 minimum (BCP 195) | Enforced via `SSL_CTX_set_min_proto_version(TLS1_2_VERSION)` |
| 7 | Certificate bound access tokens | Future: bind hook identity to subscription via fingerprint |

### Cloudflare mTLS Best Practices

| Practice | Implementation |
|---|---|
| API Shield: only authenticated clients reach API | mTLS mandatory on registry WS port |
| Client certificates issued from private CA | `CertManager::create_ca()` → `issue_client_cert()` |
| Short-lived certificates (hours/days) | Configurable `validity_days` in cert generation |
| Revocation via CRL or API | `CertRevocationList` class |
| Log client certificate identity | `ClientCertInfo` stored per connection, logged on connect |

### TLS Cipher Suite Recommendations (Mozilla SSL Configuration Generator, "Intermediate" profile)

| Protocol | Ciphers |
|---|---|
| TLS 1.3 | `TLS_AES_256_GCM_SHA384`, `TLS_CHACHA20_POLY1305_SHA256`, `TLS_AES_128_GCM_SHA256` |
| TLS 1.2 | `ECDHE-ECDSA-AES256-GCM-SHA384`, `ECDHE-RSA-AES256-GCM-SHA384`, `ECDHE-ECDSA-AES128-GCM-SHA256`, `ECDHE-RSA-AES128-GCM-SHA256` |
| Disabled | NULL, MD5, RC4, 3DES, EXPORT, static RSA key exchange |

---

## Appendix B: Cross-Platform API Reference

| Operation | Linux | Windows | macOS | Used in odc-hook |
|---|---|---|---|---|
| TCP socket | `socket(AF_INET, SOCK_STREAM)` | `socket(AF_INET, SOCK_STREAM)` via Winsock | Same as Linux | **No** — ASIO `tcp::socket` |
| DNS resolve | `getaddrinfo()` | `getaddrinfo()` via Winsock | Same | **No** — ASIO `tcp::resolver` |
| TLS | OpenSSL via `asio::ssl::stream` | OpenSSL via ASIO (or SChannel) | OpenSSL via ASIO | **Yes** |
| Non-blocking I/O | `poll()` / `epoll()` | `WSAPoll()` / IOCP | `poll()` / `kqueue` | **No** — ASIO `async_read`/`async_write` |
| Event loop | ASIO `io_context` | ASIO `io_context` (IOCP backend) | ASIO `io_context` (kqueue backend) | **Yes** |
| Threads | `std::thread` (pthreads) | `std::thread` (Win32 threads) | `std::thread` (pthreads) | **Yes** |
| Mutex/CV | `std::mutex`, `std::condition_variable` | Same | Same | **Yes** |
| Sleep | `std::this_thread::sleep_for()` | Same | Same | **Yes** |
| File I/O | `std::ifstream`, `std::ofstream` | Same | Same | **Yes** |
| File paths | `/etc/odc-root/...` | `C:\ProgramData\odc-root\...` | `/usr/local/etc/odc-root/...` | Via `platform.hpp` |
| Daemon | `fork()` + `setsid()` | Win32 Service API | `fork()` + `setsid()` or `launchd` | Via `platform.hpp` |
| Signals | `SIGTERM`, `SIGINT` | `SetConsoleCtrlHandler()` | `SIGTERM`, `SIGINT` | Via ASIO `signal_set` |
| Environment | `getenv()` | `GetEnvironmentVariable()` | `getenv()` | **No** — YAML config instead |

**Zero POSIX-only headers in odc-hook**. All platform-specific code is in `platform_*.cpp` and selected at build time by CMake.

---

## Task Summary

| Phase | Tasks | Files | Est. Effort |
|---|---|---|---|
| **Phase 0** — Cross-platform primitives | 5 | 5 new, 1 modified | 0.5 day |
| **Phase 1** — Registry mTLS hardening | 6 | 3 modified, 1 new, 1 config | 1 day |
| **Phase 2** — Cross-platform HTTP client | 2 | 1 new, 1 modified | 1 day |
| **Phase 3** — odc-hook service | 9 | 11 new | 2 days |
| **Phase 4** — Tests | 4 | 4 new | 1 day |
| **Phase 5** — Cross-compile validation | 2 | 1 new (toolchain) | 0.5 day |
| **Total** | **28 tasks** | **~25 files** | **~6 days** |
