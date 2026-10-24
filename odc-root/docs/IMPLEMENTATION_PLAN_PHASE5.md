# odc-root — Phase 5 Implementation Plan: Complete System

**Date**: 2026-07-14
**Goal**: Eliminate all stubs and incomplete implementations. Every service, every IPC method, every network protocol path must work with real code — no mocks, no placeholder implementations, no dead callbacks.

**Status pre-Phase 5**: ~85% complete. Three critical gaps prevent end-to-end operation.

**Status update (2026-07-25)**: Phase 4 OpenVPN server is implemented in-process via OpenVPN3 `ServerProto` (UDP) — no classic `openvpn` child. Lab verification via `lab-run-vpn-suite.sh`.

---

## Table of Contents

1. [Pre-Flight: System Audit Summary](#pre-flight-system-audit-summary)
2. [Phase 1 — WebSocket Server: Make It Work](#phase-1--websocket-server-make-it-work)
   - [Task 1.1 — HTTP WebSocket Upgrade Handshake](#task-11--http-websocket-upgrade-handshake)
   - [Task 1.2 — WebSocket Read Loop with RFC 6455 Frame Parsing](#task-12--websocket-read-loop-with-rfc-6455-frame-parsing)
   - [Task 1.3 — Subscription API with Auto-Cleanup](#task-13--subscription-api-with-auto-cleanup)
   - [Task 1.4 — Verify End-to-End Registry Manager Message Flow](#task-14--verify-end-to-end-registry-manager-message-flow)
3. [Phase 2 — Build System & Dependencies](#phase-2--build-system--dependencies)
   - [Task 2.1 — Declare libsodium CPM Dependency](#task-21--declare-libsodium-cpm-dependency)
   - [Task 2.2 — Remove Dead websocketpp Artifacts](#task-22--remove-dead-websocketpp-artifacts)
   - [Task 2.3 — Clean Build Verification](#task-23--clean-build-verification)
4. [Phase 3 — WireGuard Server Completion](#phase-3--wireguard-server-completion)
   - [Task 3.1 — Implement Verbose Netlink Peer Enumeration](#task-31--implement-verbose-netlink-peer-enumeration)
   - [Task 3.2 — Add sockaddr_in to Endpoint String Helper](#task-32--add-sockaddr_in-to-endpoint-string-helper)
5. [Phase 4 — OpenVPN Server: Full Implementation](#phase-4--openvpn-server-full-implementation)
   - [Task 4.1 — Architecture Overview](#task-41--architecture-overview)
   - [Task 4.2 — Implement OvpnTransportUdp (Transport Layer)](#task-42--implement-ovpntransportudp-transport-layer)
   - [Task 4.3 — Implement OvpnTunFactory (TUN Layer)](#task-43--implement-ovpntunfactory-tun-layer)
   - [Task 4.4 — Implement OvpnManFactory (Management Layer)](#task-44--implement-ovpnmanfactory-management-layer)
   - [Task 4.5 — Wire OpenVPN Server into OpenVpnServer Class](#task-45--wire-openvpn-server-into-openvpnserver-class)
   - [Task 4.6 — Build System Integration](#task-46--build-system-integration)
   - [Task 4.7 — Enable OpenVPN in Config](#task-47--enable-openvpn-in-config)
6. [Phase 5 — Docker Integration Testing & CI](#phase-5--docker-integration-testing--ci)
   - [Task 5.1 — Run Docker Integration Test from Clean State](#task-51--run-docker-integration-test-from-clean-state)
   - [Task 5.2 — Create GitHub Actions CI Workflow](#task-52--create-github-actions-ci-workflow)
   - [Task 5.3 — Update PROGRESS_REPORT.md](#task-53--update-progress_reportmd)
7. [Phase 6 — CLI Test Coverage](#phase-6--cli-test-coverage)
   - [Task 6.1 — Add CLI Unit Tests](#task-61--add-cli-unit-tests)
8. [Phase 7 — Documentation Updates](#phase-7--documentation-updates)
   - [Task 7.1 — Update ARCHITECTURE.md](#task-71--update-architecturemd)
   - [Task 7.2 — Update API.md](#task-72--update-apimd)
9. [Task Execution Order & Dependencies](#task-execution-order--dependencies)
10. [Estimated Effort](#estimated-effort)
11. [Verification Checklist](#verification-checklist)

---

## Pre-Flight: System Audit Summary

### What is Complete and Working

| Component | Status | Tests |
|---|---|---|
| `libodc-common` (Logging, YamlConfig, SqliteDb, CertManager, IpcServer, IpcClient, HttpClient, Platform) | **Complete** | 15 test cases, 79 assertions |
| `odc-tunnel-manager` (TunnelManager orchestrator, WireGuardServer, SessionTracker, VpnCertGenerator, main.cpp, tunnel IPC server) | **Complete** | Integration suite §6 |
| `odc-registry-manager` (RegistryManager orchestrator, DataStore, WebhookClient, WsCertGenerator, CertRevocationList, main.cpp) | **Complete** | Integration suite §4-5 |
| `odc-hook` (HookService, WsClient with mTLS, EventBus, ThreadPool, VPN subsystem including userspace WireGuard + OpenVPN3 client) | **Complete** | 4 test suites, 23 cases |
| `odc-cli` (16 subcommands: cert, tunnel, events, hook, services, status) | **Complete** | No dedicated tests |
| Systemd unit files (all 3 services) | **Complete** | — |
| Docker build infrastructure (Linux, Windows/MinGW, macOS/osxcross, test) | **Complete** | Not run in clean environment |
| Integration test suite (8 sections, 40+ scenarios) | **Complete** | Not run in Docker |
| ARCHITECTURE.md, API.md | **Complete** | Outdated (missing new features) |

### What is Incomplete — Critical Gaps

| # | Gap | File(s) | Severity | Impact |
|---|---|---|---|---|
| 1 | WsServer: no HTTP upgrade handshake, no read loop, no frame parsing, no message dispatch | `registry-manager/ws_server.cpp` | **BLOCKER** | WS clients TLS-connect but never receive upgrade. `on_message_` and `on_disconnect_` callbacks never fire. pub/sub `subscribers_` map exists but subscribe/unsubscribe API is missing. |
| 2 | OpenVPN server: stub only; validates config then sets `running_=true` without starting anything | `tunnel-manager/openvpn_server.cpp` | **HIGH** | `start()` is a no-op. `list_clients()` returns `{}`. `disconnect_client()` returns `true` without effect. |
| 3 | WireGuard `list_peers()` and `get_peer_info()` return incomplete data | `tunnel-manager/wireguard_server.cpp` | **MEDIUM** | Verbose netlink receive path not implemented. Methods exist but return empty or partial results. |
| 4 | `libsodium` not declared as build dependency | `CMakeLists.txt` | **MEDIUM** | Required by odc-hook VPN subsystem (Noise_IKpsk2 handshake, ChaCha20Poly1305 AEAD). May link by accident via transitive deps. |
| 5 | `websocketpp` never declared in CPM; `cmake/websocketpp_cxx20.patch` is dead code | `CMakeLists.txt`, `cmake/websocketpp_cxx20.patch` | **LOW** | WsServer uses raw asio::ssl::stream directly — no websocketpp dependency exists. Patch file is orphaned. |
| 6 | No CI workflow, Docker builds never run clean, no CLI tests, docs outdated | `.github/`, `tests/`, `docs/` | **MEDIUM** | Everything is defined but never validated in a clean environment. |

### What is Complete but Has No Dedicated Tests

| Component | Status |
|---|---|
| `odc-cli` (16 subcommands) | No `test_cli.cpp` exists |
| `openvpn_server.cpp` (stub) | No tests |
| `webhook_client.cpp` | No unit tests (exercised by integration suite) |
| `data_store.cpp` | No unit tests (exercised by integration suite) |

---

## Phase 1 — WebSocket Server: Make It Work

### Context

The WsServer (`registry-manager/ws_server.cpp`, 483 lines) has fully working:
- TLS/mTLS setup with hardened cipher suites, client cert verification, CRL checking
- TLS handshake with client certificate extraction (SHA-256 fingerprint, CN, serial, timestamps)
- WebSocket frame encoding (`ws_frame()` — RFC 6455 server-to-client, no mask)
- `send()`, `send_json()`, `broadcast()`, `broadcast_json()`, `publish()` — all working
- `on_connect_` callback fires after TLS handshake
- `on_client_cert_` callback fires after cert extraction
- `do_accept()` / `handle_accept()` async accept loop

However, after the TLS handshake on line 319-359 of ws_server.cpp, the code does:
```cpp
// Stores socket in connections_ map
// Fires on_connect_ callback
// Fires on_client_cert_ callback
// Calls do_accept() to accept next client
```
And then **nothing**. No HTTP upgrade handshake is performed. No read loop is started on the socket. The accepted socket sits in `connections_` and never processes any data.

### Task 1.1 — HTTP WebSocket Upgrade Handshake

**Files**: `registry-manager/ws_server.hpp`, `registry-manager/ws_server.cpp`

**Current state**: After TLS handshake, socket is stored in `connections_`.

**What to implement**: After successful TLS handshake and cert extraction in `handle_accept()`, start an async read for the HTTP upgrade request.

**Implementation steps**:

1. **Add new member variables** to `WsServer` class in `ws_server.hpp`:
   ```cpp
   // Per-connection read buffers for WebSocket frame parsing
   struct ConnReadState {
       std::array<uint8_t, 65536> buf;  // 64KB read buffer
       size_t offset{0};                // bytes consumed so far
       std::vector<uint8_t> frame_buf;  // accumulated frame payload for fragmentation
       uint8_t frame_opcode{0};         // opcode of first fragment
       bool upgrade_done{false};        // HTTP upgrade completed
   };
   mutable std::mutex readstate_mutex_;
   std::unordered_map<std::string, std::shared_ptr<ConnReadState>> read_states_;
   ```

2. **Add private method declarations** in `ws_server.hpp`:
   ```cpp
   void start_http_upgrade(const std::string& conn_id);
   void start_read_loop(const std::string& conn_id);
   void handle_read(const std::string& conn_id, const asio::error_code& ec, size_t bytes);
   bool try_http_upgrade(const std::string& conn_id, std::string_view data);
   std::string compute_ws_accept_key(const std::string& client_key);
   ```

3. **Implement `compute_ws_accept_key()`** — static helper:
   - Concatenate `client_key` + `"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"`
   - SHA-1 hash the result (use OpenSSL `SHA1()`)
   - Base64 encode the hash (use OpenSSL `EVP_EncodeBlock`)
   - Return the encoded string

4. **Implement `start_http_upgrade(conn_id)`**:
   - Create `ConnReadState` for this connection, store in `read_states_`
   - Call `async_read_some()` on `connections_[conn_id]` with a short timeout (10 seconds for upgrade)
   - In the read handler, call `try_http_upgrade()`

5. **Implement `try_http_upgrade(conn_id, data)`**:
   - Parse the HTTP request from raw bytes. Expected format:
     ```
     GET /ws HTTP/1.1\r\n
     Host: ...\r\n
     Upgrade: websocket\r\n
     Connection: Upgrade\r\n
     Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n
     Sec-WebSocket-Version: 13\r\n
     \r\n
     ```
   - Validate:
     - Method is `GET`
     - URL path matches `config_.url_path` (default `/ws`)
     - Header `Upgrade` equals `websocket` (case-insensitive)
     - Header `Connection` contains `Upgrade` (case-insensitive)
     - Header `Sec-WebSocket-Version` equals `13`
     - Header `Sec-WebSocket-Key` is present and is valid base64 (24 chars decoded = 16 bytes)
   - On validation failure, send HTTP response:
     - 400 Bad Request if request is malformed
     - 426 Upgrade Required if `Sec-WebSocket-Version` != 13 (include `Sec-WebSocket-Version: 13` header)
     - Then close the socket and remove from `connections_`
   - On success:
     - Compute `Sec-WebSocket-Accept` via `compute_ws_accept_key()`
     - Send HTTP 101 response:
       ```
       HTTP/1.1 101 Switching Protocols\r\n
       Upgrade: websocket\r\n
       Connection: Upgrade\r\n
       Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n
       \r\n
       ```
     - Use `asio::write(socket, asio::buffer(response))` for synchronous write (the handshake is once-per-connection, sync is acceptable)
     - Set `read_state->upgrade_done = true`
     - Call `start_read_loop(conn_id)` to begin WebSocket frame processing
   - If the HTTP request spans multiple read chunks (data doesn't contain `\r\n\r\n`):
     - Accumulate data in the read buffer
     - Continue reading until `\r\n\r\n` is found
     - If 10-second timeout expires without valid upgrade, close with 408 Request Timeout

6. **Wire into `handle_accept()`**: After the existing code at line 346 (`if (on_connect_) on_connect_(conn_id);`) and line 352 (`if (on_client_cert_) { ... }`), add:
   ```cpp
   start_http_upgrade(conn_id);
   ```
   This replaces the implicit "connection is done" behavior with actual protocol negotiation.

**Verification**: After this task, a WebSocket client (e.g., `websocat`, browser `new WebSocket()`, or `odc-cli events watch`) should:
- Successfully complete TLS handshake
- Receive HTTP 101 Switching Protocols
- Any data the client sends after upgrade will be visible as raw bytes (but not yet parsed — that's Task 1.2)

---

### Task 1.2 — WebSocket Read Loop with RFC 6455 Frame Parsing

**Files**: `registry-manager/ws_server.hpp`, `registry-manager/ws_server.cpp`

**Current state**: After Task 1.1, HTTP upgrade completes but incoming WebSocket frames are not processed.

**What to implement**: After HTTP upgrade completes, start a persistent read loop that parses WebSocket frames and dispatches messages and control frames.

**Implementation steps**:

1. **Implement `start_read_loop(conn_id)`**:
   - Get the socket from `connections_[conn_id]`
   - Get the `ConnReadState` from `read_states_[conn_id]`
   - Start `async_read_some()` on the socket into `read_state.buf` (reset `offset` to 0)
   - The read completion handler calls `handle_read(conn_id, ec, bytes)`

2. **Implement `handle_read(conn_id, ec, bytes)`**:
   - On `ec` (EOF, connection reset, timeout), call disconnect cleanup (see Task 1.3)
   - On success, append the received `bytes` to a parse buffer
   - Call `parse_websocket_frames(conn_id)` to extract frames from the buffer
   - If connection still alive, call `start_read_loop(conn_id)` to continue reading

3. **Add `parse_websocket_frames(conn_id)` private method**:
   - Operates on `ConnReadState` for this connection
   - Parses frames in a loop until the buffer is exhausted or a partial frame is encountered

4. **Frame header parsing** (RFC 6455 §5.2):
   ```
   Byte 0:  FIN(1) RSV1(1) RSV2(1) RSV3(1) OPCODE(4)
   Byte 1:  MASK(1) PAYLOAD_LEN(7)
   ```
   - **OPCODE**: 0x1=text, 0x2=binary, 0x8=close, 0x9=ping, 0xA=pong
   - **MASK**: Client-to-server frames MUST be masked (bit 7 of byte 1 = 1). If MASK=0, close the connection per RFC 6455 §5.1.
   - **PAYLOAD_LEN**:
     - 0–125: actual payload length
     - 126: next 2 bytes (uint16 big-endian) are the length
     - 127: next 8 bytes (uint64 big-endian) are the length

5. **Mask key extraction** (4 bytes immediately after extended length, if MASK=1)

6. **Payload unmasking**:
   - For each byte `i` of payload: `payload[i] ^= mask_key[i % 4]`
   - Use `odc::hook::WsClient` as reference — it already implements client-side masking

7. **Frame type dispatch**:
   - **TEXT (0x1)**: Accumulate if FIN=0 (fragmentation), otherwise:
     - If fragmented, append to `frame_buf` and use the accumulated data
     - Parse as JSON via `nlohmann::json::parse()`
     - Dispatch to `on_message_(conn_id, json)`
     - If JSON parse fails, send a Close frame with status 1003 (Unsupported Data) and disconnect
   - **BINARY (0x2)**: Same fragmentation handling as TEXT, but skip JSON parsing — pass raw bytes to a binary callback (or log and ignore for now since our protocol uses JSON text)
   - **CLOSE (0x8)**:
     - If payload >= 2 bytes: extract close status code (uint16 big-endian, first 2 bytes)
     - If payload > 2 bytes: remaining bytes are close reason (UTF-8)
     - Echo back a Close frame with the same payload
     - Remove connection, fire `on_disconnect_`, cleanup (see Task 1.3)
   - **PING (0x9)**:
     - Send back a PONG frame with the same payload (unmasked, since server-to-client)
     - Use existing `ws_frame(payload, 0xA)` helper
   - **PONG (0xA)**:
     - Acknowledge receipt (update last-pong timestamp for keepalive)
     - Do not send a response
   - **CONTINUATION (0x0)**: Only valid after a non-control frame with FIN=0. Append payload to `frame_buf`. When FIN=1 arrives, dispatch the complete accumulated message.

8. **Control frame constraints** (RFC 6455 §5.5):
   - Control frames MUST have payload <= 125 bytes
   - Control frames MUST NOT be fragmented (FIN must be 1)
   - Control frames may appear between fragments of a data frame
   - If any constraint is violated, close with status 1002 (Protocol Error)

9. **Partial frame handling**: If not enough bytes are available in the buffer to complete the header, extended length, mask key, or full payload:
   - Keep the remaining bytes in the buffer
   - Exit `parse_websocket_frames()` and wait for more data from the read loop

**Verification**: After this task, a WebSocket client should:
- Send `{"type":"subscribe","topic":"tunnel.events"}` → the raw JSON should be visible in `on_message_` callback
- Send a Ping frame → server responds with Pong
- Send a Close frame → server echoes Close and disconnects
- TCP disconnect (EOF) → server detects and cleans up

**Reference implementations** (existing code to study):
- `odc-hook/ws_client.cpp`: `WsClient` has complete frame encode/decode, the server side is the inverse
- `odc-hook/ws_client.hpp::ws_encode()` and the read loop in `start_read()`

---

### Task 1.3 — Subscription API with Auto-Cleanup

**Files**: `registry-manager/ws_server.hpp`, `registry-manager/ws_server.cpp`

**Current state**: The `subscribers_` map exists (line 122 of `ws_server.hpp`) and `publish()` already reads from it (line ~467-483 of `ws_server.cpp`). But there is no public API to add or remove subscribers, and no cleanup on disconnect.

**What to implement**:

1. **Add public methods** in `ws_server.hpp`:
   ```cpp
   void subscribe(const std::string& connection_id, const std::string& topic);
   void unsubscribe(const std::string& connection_id, const std::string& topic);
   void unsubscribe_all(const std::string& connection_id);
   ```

2. **Implement `subscribe(conn_id, topic)`**:
   ```cpp
   void WsServer::subscribe(const std::string& conn_id, const std::string& topic) {
       std::lock_guard lock(subs_mutex_);
       subscribers_[topic].insert(conn_id);
   }
   ```

3. **Implement `unsubscribe(conn_id, topic)`**:
   ```cpp
   void WsServer::unsubscribe(const std::string& conn_id, const std::string& topic) {
       std::lock_guard lock(subs_mutex_);
       auto it = subscribers_.find(topic);
       if (it != subscribers_.end()) {
           it->second.erase(conn_id);
           if (it->second.empty())
               subscribers_.erase(it);  // Clean up empty topic sets
       }
   }
   ```

4. **Implement `unsubscribe_all(conn_id)`**:
   ```cpp
   void WsServer::unsubscribe_all(const std::string& conn_id) {
       std::lock_guard lock(subs_mutex_);
       for (auto it = subscribers_.begin(); it != subscribers_.end(); ) {
           it->second.erase(conn_id);
           if (it->second.empty())
               it = subscribers_.erase(it);
           else
               ++it;
       }
   }
   ```

5. **Wire subscription handling into the read loop** (Task 1.2's `parse_websocket_frames()`):
   - After successfully parsing a TEXT frame as JSON, check if `json["type"]` is `"subscribe"` or `"unsubscribe"`
   - If `"subscribe"`: extract `json["topic"]` (string), call `subscribe(conn_id, topic)`, send acknowledgement: `{"type":"subscribed","topic":"..."}`
   - If `"unsubscribe"`: extract `json["topic"]` (string), call `unsubscribe(conn_id, topic)`, send acknowledgement: `{"type":"unsubscribed","topic":"..."}`
   - For all other `type` values, dispatch to `on_message_(conn_id, json)` as normal
   - **Note**: Subscribe/unsubscribe messages are intercepted at the WsServer layer. They never reach the `RegistryManager`. This matches the plan's architecture where WsServer handles pub/sub internally.

6. **Implement full disconnect cleanup** (extracted as a private method `cleanup_connection(conn_id)`):
   - Remove from `connections_` map
   - Remove from `connection_certs_` map
   - Call `unsubscribe_all(conn_id)`
   - Remove from `read_states_`
   - Fire `on_disconnect_(conn_id)` callback
   - Log the disconnection with client info

7. **Call `cleanup_connection()` from all disconnect paths**:
   - TCP read error / EOF in `handle_read()`
   - Close frame received in `parse_websocket_frames()`
   - HTTP upgrade failure in `try_http_upgrade()`
   - TLS handshake failure (already handled, but verify)
   - Ensure cleanup is idempotent (use a `disconnected_` flag or check if `connections_` still contains `conn_id`)

**Verification**:
- Client subscribes to `tunnel.events` → `subscribe()` adds to `subscribers_`
- `publish("tunnel.events", data)` → client receives the frame
- Client unsubscribes → no more events for that topic
- Client disconnects (TCP close) → auto-unsubscribed from all topics, connection removed, `on_disconnect_` fires

---

### Task 1.4 — Verify End-to-End Registry Manager Message Flow

**Files**: `registry-manager/registry_manager.cpp` (read and verify only — likely no changes needed)

**Current state**: `registry_manager.cpp` properly registers `on_message_`, `on_connect_`, `on_disconnect_` callbacks on the WsServer. But these callbacks were never invoked because the WsServer had no read loop.

**What to do**: After Tasks 1.1–1.3, verify that the following flow works end-to-end:

1. **Service registration flow**:
   - WebSocket client connects (mTLS, cert extracted, upgrade done)
   - Client sends: `{"type":"service.register","service":{"id":"test-1","name":"Test","type":"app"}}`
   - `on_message_` fires → `registry_manager.cpp` line ~289: `on_ws_message()` receives it
   - Dispatches to `handle_service_register()` → `DataStore::register_service()` → SQLite insert
   - `relay_event("services", "service.registered", data)` → logs to DB, publishes to WS topic `services`, fires webhooks
   - Verify other subscribed clients on topic `services` receive the broadcast

2. **Service query flow**:
   - Client sends: `{"type":"query","filter":{"type":"app"}}`
   - `on_ws_message()` → `handle_query()` → `DataStore::list_services("app")` → returns results as direct `send_json(conn_id, ...)` response

3. **Event relay flow**:
   - Tunnel-manager notifies registry via IPC: `tunnel.client.connected`
   - `handle_tunnel_client_connected()` fires → `relay_event("tunnel.events", ...)` → `WsServer::publish("tunnel.events", ...)`
   - Subscribed WS clients receive the event frame

4. **Disconnect flow**:
   - WS client disconnects → WsServer fires `on_disconnect_` → registry logs it
   - Verify the client is auto-unsubscribed and removed from all topic subscriber sets

**Verification**: Run the integration test suite (`deploy/test/test_suite.sh`) sections 3, 4, and 5. These sections test mTLS connections, service discovery, and event propagation — they should now pass where previously the WS connection would have timed out.

---

## Phase 2 — Build System & Dependencies

### Task 2.1 — Declare libsodium CPM Dependency

**Files**: `CMakeLists.txt`, `odc-hook/CMakeLists.txt`, `deploy/linux/Dockerfile`, `deploy/test/Dockerfile`, `deploy/windows/Dockerfile`

**Current state**: `libsodium` is used by the odc-hook VPN subsystem (Noise_IKpsk2 handshake in `wg_noise.cpp`, ChaCha20Poly1305 AEAD in `wg_aead.cpp`) but is never declared as a CPM dependency or `find_package`. It may link via system packages by accident.

**Implementation steps**:

1. **Add to top-level `CMakeLists.txt`** (after the `catch2` block, before `find_package(OpenSSL)`):
   ```cmake
   # libsodium — required by odc-hook VPN subsystem (Noise protocol, AEAD)
   find_package(sodium QUIET)
   if(NOT sodium_FOUND)
       message(STATUS "libsodium not found via find_package — building from source")
       CPMAddPackage(
           NAME libsodium
           GITHUB_REPOSITORY jedisct1/libsodium
           GIT_TAG 1.0.20-RELEASE
           OPTIONS "SODIUM_DISABLE_TESTS ON" "SODIUM_MINIMAL_BUILD ON"
       )
       # libsodium's CMake exports the target as sodium::sodium or sodium
       if(TARGET sodium)
           add_library(sodium::sodium ALIAS sodium)
       elseif(TARGET sodium_static)
           add_library(sodium::sodium ALIAS sodium_static)
       endif()
   endif()
   ```

2. **Link in `odc-hook/CMakeLists.txt`**: Add `sodium::sodium` to `target_link_libraries(odc-hook ...)` (likely already there conditionally behind `BUILD_ODC_HOOK_VPN` — verify).

3. **Add system package to Dockerfiles** (for faster builds):
   - `deploy/linux/Dockerfile`: `libsodium-dev` in `apt-get install`
   - `deploy/test/Dockerfile`: `libsodium-dev` in `apt-get install`
   - `deploy/windows/Dockerfile`: cross-compile from source or use mingw-w64 package

4. **Verify**: `cmake -B build && cmake --build build --target odc-hook -j$(nproc)` succeeds. Check that `-lsodium` appears in the linker command.

---

### Task 2.2 — Remove Dead websocketpp Artifacts

**Files**: `cmake/websocketpp_cxx20.patch`, `docs/ARCHITECTURE.md`

**Current state**: `cmake/websocketpp_cxx20.patch` (102 lines) exists but `websocketpp` is never declared as a CPM dependency in `CMakeLists.txt`. The WsServer uses raw `asio::ssl::stream` with manual WebSocket frame construction — no websocketpp code is compiled anywhere. The patch file is orphaned dead code.

**Implementation steps**:

1. Delete `cmake/websocketpp_cxx20.patch`
2. Remove `cmake/` directory if it becomes empty after deletion
3. Update `docs/ARCHITECTURE.md`: In the "Registry Manager / WsServer" section, remove any mention of websocketpp. Replace with: "Custom RFC 6455 implementation over raw asio::ssl::stream — no external WebSocket library."

---

### Task 2.3 — Clean Build Verification

**Files**: None (verification only)

**Current state**: The codebase has been incrementally built. A from-scratch build may reveal missing headers, link errors, or configuration issues.

**Implementation steps**:

1. **Clean build**:
   ```bash
   rm -rf build
   cmake -B build -DCMAKE_BUILD_TYPE=Debug
   cmake --build build -j$(nproc)
   ```

2. **Check for build errors**: Fix any compile or link errors found.

3. **Run all tests**:
   ```bash
   cd build && ctest --output-on-failure -j$(nproc)
   ```
   Expected: 6/6 tests pass (same as current state, but verifying no regressions).

4. **Check for compiler warnings**: If `-Wall -Wextra` is enabled (check CMakeLists.txt), ensure zero warnings. If warnings exist but are pre-existing, note them for future cleanup.

5. **Verify binary sizes and symbols** (spot-check):
   ```bash
   ls -lh build/tunnel-manager/odc-tunnel-manager
   ls -lh build/registry-manager/odc-registry-manager
   ls -lh build/odc-hook/odc-hook
   ls -lh build/tools/odc-cli/odc-cli
   ```

**Acceptance**: Zero build errors. All 6 test suites pass. No new warnings introduced.

---

## Phase 3 — WireGuard Server Completion

### Context

`wireguard_server.cpp` (332 lines) is a fully functional WireGuard server using Linux kernel netlink:
- `start()`: Creates WG interface, sets private key, brings up, assigns subnet IP
- `stop()`: Deletes interface, clears state
- `add_peer()`: Adds peer to kernel via netlink
- `remove_peer()`: Rebuilds peer list without the removed key
- `allocate_ip()` / `release_ip()` / `is_ip_available()`: Full IP pool management
- `poll_peers()`: Detects connect/disconnect transitions based on handshake timestamps

But `list_peers()` and `get_peer_info()` have commented-out notes saying the verbose netlink receive path is not implemented. They return empty or partial data.

### Task 3.1 — Implement Verbose Netlink Peer Enumeration

**Files**: `tunnel-manager/wireguard_server.cpp`, `tunnel-manager/wireguard_server.hpp`

**Current `list_peers()` state** (approximate): Likely returns `{}` or iterates only the in-memory `known_peers_` set without kernel data.

**What `list_peers()` must return**: A `std::vector<WgPeerInfo>` where each entry contains:
```cpp
struct WgPeerInfo {
    std::string public_key;         // base64
    std::string allowed_ip;         // "10.0.0.2/32"
    std::string endpoint_addr;      // "192.168.1.100:51820"
    int64_t last_handshake_sec;     // seconds since last handshake (0 = never)
    uint64_t bytes_rx;              // kernel rx counter
    uint64_t bytes_tx;              // kernel tx counter
};
```

**Implementation**:

1. **Use `wg_get_device()` or raw netlink** with the `WGDEVICE_F_REPLACE_PEERS` flag to request verbose peer data.

2. **Netlink attribute parsing**: For each peer returned by the kernel, parse the following `WGPEER_A_*` attributes (defined in `thirdparty/wg_netlink.hpp` or `<linux/wireguard.h>`):

   | Attribute | Type | Conversion |
   |---|---|---|
   | `WGPEER_A_PUBLIC_KEY` | 32-byte binary | Base64 encode (use existing base64 implementation in `wireguard_server.cpp`) |
   | `WGPEER_A_ENDPOINT` | `struct sockaddr_in` (16 bytes) | Extract `sin_addr.s_addr` → `inet_ntop` for IPv4. `sin_port` → `ntohs` for port. Format: `"IP:PORT"` |
   | `WGPEER_A_LAST_HANDSHAKE_TIME` | `struct timespec64` or uint64 (seconds) | Boot-relative timestamp. Convert to wall-clock by subtracting from current monotonic time. If architecture uses `CLOCK_MONOTONIC` consistently, use: `last_handshake_sec = now_monotonic_sec - kernel_timespec.tv_sec`. If negative, set to 0 (never connected). |
   | `WGPEER_A_RX_BYTES` | uint64 | Use directly |
   | `WGPEER_A_TX_BYTES` | uint64 | Use directly |
   | `WGPEER_A_ALLOWEDIPS` | Nested array of `struct wg_allowedip` | Extract first allowed IP (CIDR). Convert to `"IP/CIDR"` string. |
   | `WGPEER_A_PERSISTENT_KEEPALIVE_INTERVAL` | uint16 | Seconds. Already tracked internally. |

3. **Time conversion**: The kernel stores `last_handshake_time` in "seconds since boot" (technically `ktime_get_real_seconds()` or `CLOCK_MONOTONIC` equivalent depending on kernel version). The safe approach:
   ```cpp
   // Read /proc/uptime or use clock_gettime(CLOCK_MONOTONIC, ...)
   // If handshake_time > uptime, the kernel might be using wall-clock — use as-is
   // Otherwise, compute: wall_clock_now - (uptime_seconds - handshake_time)
   ```
   Actually, modern kernels (5.4+) use `ktime_get_boottime_seconds()` which is `CLOCK_BOOTTIME`. The safest portable approach: check if the value is within a reasonable range of Unix epoch (e.g., > 1,000,000,000 = year 2001+), and if so treat as wall clock. Otherwise treat as relative.

   Simpler approach used by `wg(8)` tool: the attribute is `WGPEER_A_LAST_HANDSHAKE_TIME` which is `uint64_t` seconds since boot. Compare with `/proc/uptime` or `clock_gettime(CLOCK_BOOTTIME)`:
   ```cpp
   struct timespec boot_ts;
   clock_gettime(CLOCK_BOOTTIME, &boot_ts);
   uint64_t now_boot_sec = boot_ts.tv_sec;
   int64_t ago = now_boot_sec - handshake_time;  // seconds ago
   int64_t wall_clock = time(nullptr) - ago;       // approximate wall clock
   ```

4. **Implement in `list_peers()`**:
   - Open netlink socket
   - Send `WG_CMD_GET_DEVICE` with `WGDEVICE_A_IFNAME` set to `config_.interface_name`
   - Parse response, iterate over nested `WGDEVICE_A_PEERS` attributes
   - For each peer, build `WgPeerInfo` struct
   - Close netlink socket
   - Return the vector

5. **Implement in `get_peer_info(public_key)`**:
   - Call `list_peers()` internally
   - Find the peer with matching `public_key`
   - Return `WgPeerInfo` (or empty optional if not found)

6. **Handle the `known_peers_` vs kernel state discrepancy**:
   - The in-memory `known_peers_` set tracks peers added by our code
   - The kernel may have peers added externally (e.g., manually via `wg set`)
   - `list_peers()` should return ALL peers from the kernel, regardless of `known_peers_`
   - But `remove_peer()` should only remove peers that are in `known_peers_` (safety check)

7. **Reference implementation**: The `wg(8)` command-line tool source at `thirdparty/wg-tools/` contains netlink parsing logic. Study how it handles `WGPEER_A_*` attributes.

**Verification**: 
- Start tunnel-manager with a WireGuard server
- Use `odc-cli tunnel peers add` to add a peer
- Use `odc-cli tunnel peers list` → should return the peer with public key, IP, and stats
- Connect a real WireGuard client to the peer
- Use `odc-cli tunnel peers list` → should show `last_handshake_sec` > 0 and non-zero `bytes_rx`/`bytes_tx`

---

### Task 3.2 — Add sockaddr_in to Endpoint String Helper

**Files**: `tunnel-manager/wireguard_server.cpp`

**Current state**: No helper exists for converting `struct sockaddr_in` from netlink attributes to a human-readable `"IP:port"` string.

**Implementation**:

1. **Add static helper function** in anonymous namespace or as private static method of `WireGuardServer`:
   ```cpp
   static std::string sockaddr_to_string(const struct sockaddr* addr, socklen_t len) {
       if (!addr || len < sizeof(struct sockaddr_in))
           return "(unknown)";
       
       if (addr->sa_family == AF_INET) {
           const auto* sin = reinterpret_cast<const struct sockaddr_in*>(addr);
           char ip[INET_ADDRSTRLEN];
           inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
           return std::string(ip) + ":" + std::to_string(ntohs(sin->sin_port));
       }
       
       if (addr->sa_family == AF_INET6) {
           const auto* sin6 = reinterpret_cast<const struct sockaddr_in6*>(addr);
           char ip[INET6_ADDRSTRLEN];
           inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
           return "[" + std::string(ip) + "]:" + std::to_string(ntohs(sin6->sin6_port));
       }
       
       return "(unknown)";
   }
   ```

2. **Use in `list_peers()`**: When parsing `WGPEER_A_ENDPOINT`, call `sockaddr_to_string(attr_data, attr_len)` and store result in `WgPeerInfo::endpoint_addr`.

3. **Handle NULL endpoint**: If `WGPEER_A_ENDPOINT` is absent or length is 0, set `endpoint_addr` to `"(no endpoint)"` — this means the peer was configured but has never connected.

---

## Phase 4 — OpenVPN Server: Full Implementation

### Task 4.1 — Architecture Overview

The OpenVPN3 vendored library at `thirdparty/minimal-openvpn3/openvpn/server/servproto.hpp` contains `ServerProto::Factory` and `ServerProto::Session` — a **complete, production-grade OpenVPN wire protocol implementation**. The server-side code is header-only C++ (`servproto.hpp` is 27KB with all logic inline).

`ServerProto::Session` implements:
- OpenVPN control channel handshake (TLS/SSL-based)
- Data channel encryption/decryption (AES-256-GCM via `ProtoContext`)
- Keepalive, renegotiation, key rotation
- Client connect/disconnect lifecycle
- Peer statistics tracking
- Certificate-based authentication callbacks

What's missing are three **concrete provider implementations** that the session delegates to:

```
                    ┌─────────────────────────┐
                    │  ServerProto::Session    │  ← already exists (header-only)
                    │  (protocol state machine) │
                    └──┬──────────┬──────────┬─┘
                       │          │          │
              ┌────────▼────┐ ┌───▼────┐ ┌──▼──────────┐
              │ Transport   │ │  TUN   │ │ Management  │  ← MUST IMPLEMENT
              │ (UDP I/O)   │ │(iface) │ │ (auth/IP)   │
              └─────────────┘ └────────┘ └─────────────┘
```

There is precedent in the codebase: `odc-hook/vpn/openvpn_client.cpp` (251 lines) implements the **client-side** equivalents of these interfaces using the same `minimal-openvpn3` library. We can follow the same patterns for the server side.

**New files to create**:
- `tunnel-manager/openvpn_transport.hpp` + `openvpn_transport.cpp`
- `tunnel-manager/openvpn_tun.hpp` + `openvpn_tun.cpp`
- `tunnel-manager/openvpn_management.hpp` + `openvpn_management.cpp`

**Files to modify**:
- `tunnel-manager/openvpn_server.hpp` — add new member variables
- `tunnel-manager/openvpn_server.cpp` — replace stub with real implementation
- `tunnel-manager/CMakeLists.txt` — add new source files
- `tunnel-manager/config/tunnel-manager.yaml` — enable OpenVPN server

---

### Task 4.2 — Implement OvpnTransportUdp (Transport Layer)

**New files**: `tunnel-manager/openvpn_transport.hpp`, `tunnel-manager/openvpn_transport.cpp`

**Purpose**: Provide UDP socket listener and per-client send/receive. This is the network I/O layer between the OpenVPN protocol engine and remote clients.

**Abstract interfaces to implement** (from `openvpn/transport/server/transbase.hpp`):
- `TransportServerFactory::new_server_obj()` — creates a `TransportServer`
- `TransportServer::start()`, `stop()`, `local_endpoint_info()`, `local_endpoint_addr()`
- `TransportClientInstance::Send` — per-client send side (encrypted packets → UDP)
- `TransportClientInstance::Recv` — **NOT NEEDED**; `ServerProto::Session` already implements this
- `TransportClientInstance::Factory` — **NOT NEEDED**; `ServerProto::Factory` already implements this

**Implementation plan**:

1. **`OvpnTransportServer` class** (implements `TransportServer`):
   - **Constructor**: Takes `openvpn_io::io_context&`, `listen_port` (uint16), `protocol` ("udp"/"tcp")
   - **`start()`**: 
     - Create UDP socket: `openvpn_io::ip::udp::socket` bound to `0.0.0.0:listen_port`
     - Start async receive loop: `socket_.async_receive_from(asio::buffer(recv_buf_), remote_endpoint_, ...)`
   - **`stop()`**: 
     - Cancel all async operations
     - Close socket
     - Clear session map
   - **Receive handler** (`handle_receive(ec, bytes)`):
     - On new UDP packet from unknown source address:
       - Create a new `TransportClientInstance::Send` implementation for this client
       - Call `proto_factory_->new_client_instance()` to get a `ServerProto::Session::Ptr`
       - Wire the session to this transport send object
       - Call `session->start(send, peer_addr, next_peer_id++)` to begin protocol handshake
       - Store session in `sessions_` map keyed by peer address
     - On UDP packet from known source address:
       - Look up the session
       - Pass the buffer to `session->transport_recv(buf)`
     - Continue the receive loop
   - **`local_endpoint_info()`**: Return `"UDP 0.0.0.0:1194"`
   - **`local_endpoint_addr()`**: Return `IP::Addr::from_string("0.0.0.0")`

2. **`OvpnTransportClientSend` class** (implements `TransportClientInstance::Send`):
   - Holds reference to the UDP socket and the client's `asio::ip::udp::endpoint`
   - **`transport_send_const(buf)`**: `socket->async_send_to(asio::buffer(buf.c_data(), buf.size()), client_ep_, ...)` — or synchronous `send_to` for simplicity
   - **`transport_send(buf)`**: Same but may modify buffer (move semantics)
   - **`transport_info()`**: Return `"UDP " + client_addr + ":" + client_port + " -> " + server_addr + ":" + server_port`
   - **`stats_pending()` / `stats_poll()`**: Track and return `PeerStats` (bytes sent/received since last poll)
   - **`defined()`**: Return `true` if socket is open
   - **`stop()`**: Remove from session map, no socket close (shared socket)

3. **`OvpnTransportFactory` class** (implements `TransportServerFactory`):
   - Holds `listen_port`, `protocol`, pointer to `ServerProto::Factory`
   - **`new_server_obj(io_context)`**: Create and return `OvpnTransportServer`

4. **Include paths**: The implementation files need:
   ```cpp
   #include <openvpn/transport/server/transbase.hpp>
   #include <openvpn/server/servproto.hpp>
   #include <asio.hpp>
   ```
   Set up via CMake include directories pointing to `thirdparty/minimal-openvpn3/`

5. **Session lifecycle management**:
   - Sessions stored in `std::unordered_map<std::string, ServerProto::Session::Ptr>`
   - Keyed by `peer_addr->to_string()`
   - On session error/destruction, remove from map
   - On `TransportServer::stop()`, stop all sessions

6. **Reference code to study**:
   - `odc-hook/vpn/openvpn_client.cpp` — shows how to use `OpenVPNClient` with transport
   - `thirdparty/minimal-openvpn3/client/ovpncli.hpp` — client-side transport patterns

**Verification**:
- UDP socket binds to configured port
- Sending a packet to the port creates a session (visible in logs)
- An OpenVPN client can initiate a TLS handshake
- Sessions are cleaned up on disconnect

---

### Task 4.3 — Implement OvpnTunFactory (TUN Layer)

**New files**: `tunnel-manager/openvpn_tun.hpp`, `tunnel-manager/openvpn_tun.cpp`

**Purpose**: Create per-client TUN interfaces for routing decrypted IP packets between the OpenVPN data channel and the host networking stack. Reuses the existing `TunDevice` abstraction from `odc-hook/vpn/tun_device.hpp`.

**Abstract interfaces to implement** (from `openvpn/tun/server/tunbase.hpp`):
- `TunClientInstance::Factory::new_tun_obj()` — creates a per-client `Send` object
- `TunClientInstance::Send` — per-client IP packet send to TUN
- `TunClientInstance::Recv` — **NOT NEEDED**; `ServerProto::Session` implements this

**Implementation plan**:

1. **`OvpnTunSend` class** (implements `TunClientInstance::Send`):
   - **Constructor**: Takes `TunClientInstance::Recv* parent`, `TunDevice&&` (move semantics), `const std::string& client_ip`
   - Creates a TUN device for this client: `TunDevice::create()` → get fd
   - Configures the TUN device with the client's assigned IP
   - Adds a route for the client's IP to the main routing table (or uses policy routing)
   - Starts a read thread on the TUN fd: reads IP packets → calls `parent_->tun_recv(buf)` → `ServerProto::Session` encrypts and sends via TransportLink
   - Uses `asio::posix::stream_descriptor` for async TUN I/O integrated with the io_context
   - **`tun_send(buf)`**: Write decrypted IP packet to the TUN fd → kernel routes it to client
   - **`tun_native_handle()`**: Return `{tun_fd, peer_id}`
   - **`relay(target, port)`**: Not implemented (relay mode not planned for Phase 5)
   - **`tun_info()`**: Return `"TUN " + client_ip`
   - **`stop()`**: Close TUN fd, join read thread, remove routes

2. **`OvpnTunFactory` class** (implements `TunClientInstance::Factory`):
   - Holds `subnet` (CIDR string, e.g., `"10.8.0.0/24"`), reference to `CertManager` (for IP-to-client mapping)
   - **`new_tun_obj(parent)`**: Create `TunDevice`, assign IP, return `OvpnTunSend`
   - IP allocation: Track allocated IPs. On creation, pick next available IP from the subnet pool. On `OvpnTunSend` destruction, release IP back to pool.
   - This is analogous to `WireGuardServer::allocate_ip()` / `release_ip()` — but for OpenVPN clients.

3. **TUN device management**:
   - Use `odc::TunDevice` from `odc-hook/vpn/tun_device.hpp` (cross-platform: Linux `/dev/net/tun`, macOS `utun`, Windows `wintun`)
   - On Linux, create persistent or ephemeral tun interface per client: `tun%d` → kernel assigns name like `tun0`
   - Alternative: Use a single shared TUN interface for all OpenVPN clients (like `tun0` with subnet routing) instead of per-client interfaces. This is simpler and matches how production OpenVPN servers work — one `tun0` with `10.8.0.1`, clients get `/30` subnets or use `topology subnet`.
   - **Recommended approach**: Single shared `tun0` interface. `OvpnTunFactory` creates it once in constructor. Per-client `OvpnTunSend` writes to the same fd but packets are routed by destination IP (which is unique per client).

4. **Include paths**:
   ```cpp
   #include <openvpn/tun/server/tunbase.hpp>
   #include "vpn/tun_device.hpp"  // from odc-hook
   ```

**Verification**:
- After OpenVPN handshake completes, a TUN device is created/configured
- IP packet from OpenVPN client appears on the TUN interface
- IP from host routed to client IP appears encrypted and sent via UDP
- On client disconnect, IP is released back to pool

---

### Task 4.4 — Implement OvpnManFactory (Management Layer)

**New files**: `tunnel-manager/openvpn_management.hpp`, `tunnel-manager/openvpn_management.cpp`

**Purpose**: Provide the server-side business logic: client authentication, IP address assignment, push options (routes, DNS, MTU), and session lifecycle tracking. This is where the tunnel-manager integrates OpenVPN into the broader ODC system (SessionTracker, CertManager, registry notifications).

**Abstract interfaces to implement** (from `openvpn/server/manage.hpp`):
- `ManClientInstance::Factory::start()`, `stop()`, `new_man_obj()`
- `ManClientInstance::Send` — per-client management logic (all ~15 methods)
- `ManClientInstance::SendBase` — inherited by `Send`, provides additional methods

**Implementation plan**:

1. **`OvpnManSend` class** (implements `ManClientInstance::Send` + `ManClientInstance::SendBase`):

   **Constructor**: Takes `ManClientInstance::Recv* parent`, `OvpnServerConfig&`, reference to `CertManager`, `SessionTracker`, `VpnCertGenerator`.

   **Key methods**:

   - **`auth_request(auth_creds, auth_cert, peer_addr)`**:
     - Extract client certificate common name from `auth_cert`
     - Extract cert fingerprint from `auth_cert->auth cert X509*`
     - Check CRL: if cert is revoked, call `parent->auth_failed("Certificate revoked", "...")`
     - Verify cert against CA: `cert_mgr_.verify_cert(cert_pem)`
     - If valid:
       - Call `parent->schedule_auth_pending_timeout(0)` to clear pending timeout
       - Authentication is considered passed. Move to push_request.
     - If invalid:
       - Call `parent->auth_failed("Authentication failed", "Invalid certificate")
     - Log: `"Client 'CN' authenticated from UDP address"`

   - **`push_request(pconf)`**:
     - Allocate IP from subnet pool: `OvpnTunFactory::allocate_ip()` or internal IP pool
     - Build push options string (standard OpenVPN `push` directives):
       ```
       "topology subnet"
       "ifconfig 10.8.0.2 255.255.255.0"
       "route-gateway 10.8.0.1"
       "redirect-gateway def1"   (if push_routes includes default)
       "dhcp-option DNS 8.8.8.8"
       "dhcp-option DNS 8.8.4.4"
       "tun-mtu 1500"
       "cipher AES-256-GCM"
       "ping 10"
       "ping-restart 60"
       ```
     - If `pconf` is non-null, can modify the ProtoConfig (e.g., set cipher preferences)
     - Call `parent->push_reply(std::move(push_msgs))` to send push options to client
     - Register client session in `SessionTracker`:
       - `client_id = "ovpn-" + common_name`
       - `tunnel_type = "openvpn"`
       - `assigned_ip = "10.8.0.2"`
       - `cert_fingerprint = "..."`
       - `session_tracker_.register_session(...)`
       - `session_tracker_.mark_connected(client_id, assigned_ip)`
     - Notify registry via callback: `notify_registry("tunnel.client.connected", {...})`

   - **`stats_notify(ps, final)`**:
     - Update `SessionTracker::update_stats(client_id, ps.bytes_in, ps.bytes_out)`
     - If `final`: `session_tracker_.mark_disconnected(client_id)`
     - Notify registry of disconnect if final

   - **`disconnect_user(type, status, reason, client_reason)`**:
     - Call `parent->push_halt_restart_msg(type, reason, client_reason)`
     - Release IP back to pool
     - `session_tracker_.mark_disconnected(client_id)`

   - **`float_notify(addr)`**:
     - Log client roaming event (client IP changed mid-session — normal for mobile clients)

   - **`keepalive_override(keepalive_ping, keepalive_timeout)`**:
     - If configured, override the default keepalive parameters
     - Acknowledge by not modifying the parameters (or modify if configured differently)

   - **`instance_name()`**: Return `"OVPN-" + common_name`
   - **`instance_id()`**: Return sequential ID
   - **`is_stopped()`**: Check internal stop flag
   - **`pre_stop()`**: Prepare for clean shutdown, notify registry
   - **`stop()`**: Set stop flag, release resources
   - **`describe_user()`**: Return JSON with CN, cert fingerprint, assigned IP
   - **`set_acl_index()`**: Not needed for Phase 5 (basic allow-all)
   - **`userprop_local_update()`**: Not needed (no user properties)
   - **`doma_acl()`**: Return empty JSON (no DOMA ACL)
   - **`post_info_user()`**: Log client info (IV_HWADDR, IV_PLAT, etc.)
   - **`app_control(msg)`**: Parse ACC messages for billing/accounting (optional, log for now)

2. **`OvpnManFactory` class** (implements `ManClientInstance::Factory`):
   - Holds `OvpnServerConfig`, reference to `CertManager`, `SessionTracker`, `VpnCertGenerator`
   - **`start()`**: Initialize IP pool from subnet
   - **`stop()`**: Stop all active management sessions
   - **`new_man_obj(parent)`**: Create and return `OvpnManSend` for a new client instance

3. **IP pool management**:
   - Parse `subnet` from config (e.g., `"10.8.0.0/24"`)
   - Reserve `subnet_base + 1` for the server (e.g., `10.8.0.1`)
   - Allocate `subnet_base + 2` through `subnet_base + 254` for clients
   - Track allocated IPs in `std::unordered_set<std::string>`
   - On allocation: pick next available IP
   - On release: return IP to available pool
   - This is identical to the WireGuard IP pool logic in `wireguard_server.cpp` — factor out into a shared `IpPool` utility class in `common/` if DRY is desired, but duplication for Phase 5 is acceptable.

4. **Integration with SessionTracker**: Each connected OpenVPN client must appear in:
   - `odc-cli tunnel status` → OpenVPN server listed with peer counts
   - `odc-cli tunnel peers list` → This command currently queries WireGuard peers only. **Decision needed**: Either add OpenVPN peers to the tunnel-manager IPC response, or add a separate `odc-cli tunnel ovpn list` command. For Phase 5, the simplest approach: `handle_peers_list()` (in `tunnel_manager.cpp`) should iterate both `wg_server_->list_peers()` and `ovpn_server_->list_clients()`, merging results. Add a `tunnel_type` field to the response JSON.

5. **Wire connect/disconnect callbacks**: The `OpenVpnServer` class provides `on_client_connected_` and `on_client_disconnected_` callbacks (already declared in `openvpn_server.hpp`). These should be wired by `OvpnManSend` via the `OpenVpnServer` instance.

6. **Include paths**:
   ```cpp
   #include <openvpn/server/manage.hpp>
   #include <openvpn/server/servproto.hpp>
   #include "odc/cert_manager.hpp"
   #include "session_tracker.hpp"
   ```

**Verification**:
- OpenVPN client connects → certificate validated → IP assigned → push options sent
- Client appears in `SessionTracker` as active
- `odc-cli tunnel status` shows OpenVPN server with connected client count
- Registry receives `tunnel.client.connected` notification
- Client disconnects → IP released → session marked disconnected → registry notified

---

### Task 4.5 — Wire OpenVPN Server into OpenVpnServer Class

**Files**: `tunnel-manager/openvpn_server.hpp`, `tunnel-manager/openvpn_server.cpp`

**Current state**: `openvpn_server.cpp` is a 78-line stub. `start()` only validates config and sets `running_=true`. `list_clients()` returns `{}`. `disconnect_client()` is a no-op.

**What to implement**: Replace the stub with a full implementation that creates and manages the three-layer OpenVPN3 server architecture.

**Implementation steps**:

1. **Update `openvpn_server.hpp`** — Add new member variables:
   ```cpp
   // OpenVPN3 protocol engine
   openvpn_io::io_context io_ctx_;                        // asio event loop
   std::unique_ptr<std::thread> io_thread_;                // runs io_ctx_
   std::unique_ptr<asio::io_context::work> work_guard_;   // keeps io_ctx_ alive
   
   // Three-layer architecture components
   std::unique_ptr<OvpnTransportFactory> transport_factory_;
   std::unique_ptr<OvpnTunFactory> tun_factory_;
   std::unique_ptr<OvpnManFactory> man_factory_;
   ServerProto::Factory::Ptr proto_factory_;
   TransportServer::Ptr transport_server_;
   
   // IP pool
   std::unordered_set<std::string> allocated_ips_;
   
   // Per-client session map (CN → session data)
   std::unordered_map<std::string, OvpnClientInfo> clients_;
   mutable std::mutex clients_mutex_;
   ```

   Also note: the existing private members `on_client_connected_`, `on_client_disconnected_`, `on_error_` are declared in the header. Verify they exist. If not, add them:
   ```cpp
   using ClientCallback = std::function<void(const OvpnClientInfo&)>;
   using ErrorCallback = std::function<void(const std::string&)>;
   ClientCallback on_client_connected_;
   ClientCallback on_client_disconnected_;
   ErrorCallback on_error_;
   ```

2. **Rewrite `start()`**:
   ```cpp
   bool OpenVpnServer::start() {
       if (running_) return true;
       auto logger = get_logger("ovpn_server");
       
       // Validate config (existing code, keep it)
       bool config_valid = true;
       if (config_.ca_cert_path.empty()) { ... config_valid = false; }
       if (config_.server_cert_path.empty()) { ... config_valid = false; }
       if (config_.server_key_path.empty()) { ... config_valid = false; }
       if (!config_valid) {
           if (on_error_) on_error_("OpenVPN server configuration incomplete");
           return false;
       }
       
       // Parse subnet to compute netblock
       // e.g., "10.8.0.0/24" → base=10.8.0.0, netmask=255.255.255.0
       
       // Build ProtoConfig for SSL/TLS
       auto proto_config = std::make_shared<ProtoContext::ProtoConfig>();
       proto_config->set_server_mode(true);
       proto_config->set_ssl_library(SSLAPI::Type::OPENSSL);
       proto_config->load_ca(config_.ca_cert_path, false);   // CA cert
       proto_config->load_cert(config_.server_cert_path);     // Server cert
       proto_config->load_private_key(config_.server_key_path); // Server key
       if (!config_.dh_params_path.empty())
           proto_config->load_dh(config_.dh_params_path);
       if (!config_.tls_auth_key_path.empty())
           proto_config->set_tls_auth_key(config_.tls_auth_key_path);
       proto_config->set_cipher(config_.cipher);  // e.g., "AES-256-GCM"
       proto_config->set_protocol(Protocol::UDP); // or TCP from config_.protocol
       
       // Set server-side TLS params
       proto_config->tls_version_min_override("1.2");
       proto_config->tls_cert_profile_override(TLSCertProfile::PREFERRED);
       
       // Create asio work guard to keep io_context running
       work_guard_ = std::make_unique<asio::io_context::work>(io_ctx_);
       
       // Create factories
       man_factory_ = std::make_unique<OvpnManFactory>(config_, cert_mgr_, session_tracker_, cert_generator_);
       man_factory_->start();
       
       tun_factory_ = std::make_unique<OvpnTunFactory>(config_.subnet);
       
       proto_factory_ = new ServerProto::Factory(io_ctx_, *proto_config);
       proto_factory_->man_factory = man_factory_;
       proto_factory_->tun_factory = tun_factory_;
       proto_factory_->stats = new SessionStats();
       
       // Create transport factory and server
       transport_factory_ = std::make_unique<OvpnTransportFactory>(
           config_.listen_port, config_.protocol, proto_factory_);
       transport_server_ = transport_factory_->new_server_obj(io_ctx_);
       transport_server_->start();
       
       // Start io_context in background thread
       running_ = true;
       io_thread_ = std::make_unique<std::thread>([this]() {
           io_ctx_.run();
       });
       
       SPDLOG_LOGGER_INFO(logger, "OpenVPN server '{}' started on port {} ({})",
                          config_.id, config_.listen_port, config_.protocol);
       return true;
   }
   ```

3. **Rewrite `stop()`**:
   ```cpp
   void OpenVpnServer::stop() {
       if (!running_) return;
       running_ = false;
       
       if (transport_server_) transport_server_->stop();
       if (man_factory_) man_factory_->stop();
       
       work_guard_.reset();          // Allow io_ctx_.run() to exit
       io_ctx_.stop();               // Cancel all pending async operations
       if (io_thread_ && io_thread_->joinable())
           io_thread_->join();
       
       transport_server_.reset();
       proto_factory_.reset();
       tun_factory_.reset();
       man_factory_.reset();
       
       auto logger = get_logger("ovpn_server");
       SPDLOG_LOGGER_INFO(logger, "OpenVPN server '{}' stopped", config_.id);
   }
   ```

4. **Rewrite `list_clients()`**:
   ```cpp
   std::vector<OvpnClientInfo> OpenVpnServer::list_clients() {
       std::lock_guard lock(clients_mutex_);
       std::vector<OvpnClientInfo> result;
       result.reserve(clients_.size());
       for (const auto& [cn, info] : clients_)
           result.push_back(info);
       return result;
   }
   ```
   The `clients_` map is populated by `OvpnManSend::push_request()` when a client authenticates, and entries are removed by `OvpnManSend::stop()` when a client disconnects. Add public methods `add_client(common_name, info)` and `remove_client(common_name)` for the management layer to call.

5. **Rewrite `disconnect_client()`**:
   ```cpp
   bool OpenVpnServer::disconnect_client(const std::string& common_name) {
       // Find the client in management sessions
       if (man_factory_) {
           man_factory_->disconnect_client(common_name);  // Implement in OvpnManFactory
           return true;
       }
       return false;
   }
   ```
   `OvpnManFactory` needs a method that iterates its active `OvpnManSend` objects, finds the one matching `common_name`, and calls `disconnect_user()` on it.

6. **Reference holders**: The `OpenVpnServer` class needs references to `CertManager`, `SessionTracker`, and `VpnCertGenerator`. These are passed from `TunnelManager` which already holds them. Check the constructor in `openvpn_server.hpp` — if it doesn't take these references, add them:
   ```cpp
   OpenVpnServer(const OvpnServerConfig& config,
                 CertManager& cert_mgr,
                 SessionTracker& session_tracker,
                 VpnCertGenerator& cert_generator);
   ```
   Update `TunnelManager::setup_servers()` (in `tunnel_manager.cpp` line ~380 area) to pass these references when constructing `OpenVpnServer`.

**Verification**:
- `start()` creates UDP listener on configured port (verify with `ss -ulnp | grep 1194`)
- `stop()` cleanly shuts down all threads, sockets, and sessions
- `list_clients()` returns connected clients after at least one has authenticated
- `disconnect_client("client-name")` triggers client disconnect

---

### Task 4.6 — Build System Integration

**Files**: `tunnel-manager/CMakeLists.txt`

**Current state**: `tunnel-manager/CMakeLists.txt` compiles `openvpn_server.cpp` into `odc-tunnel-manager` but the stub doesn't need any OpenVPN3 headers.

**Implementation steps**:

1. **Update `tunnel-manager/CMakeLists.txt`**:
   ```cmake
   add_executable(odc-tunnel-manager
       main.cpp
       tunnel_manager.cpp
       wireguard_server.cpp
       openvpn_server.cpp
       openvpn_transport.cpp      # NEW
       openvpn_tun.cpp             # NEW
       openvpn_management.cpp      # NEW
       session_tracker.cpp
       vpn_cert_generator.cpp
   )
   
   target_include_directories(odc-tunnel-manager PRIVATE
       ${CMAKE_SOURCE_DIR}/thirdparty
       ${CMAKE_SOURCE_DIR}/thirdparty/minimal-openvpn3
       ${CMAKE_SOURCE_DIR}/odc-hook    # For tun_device.hpp
       ${CMAKE_CURRENT_SOURCE_DIR}
   )
   
   target_link_libraries(odc-tunnel-manager PRIVATE
       odc-common
       CLI11::CLI11
       asio::asio
       OpenSSL::SSL
       OpenSSL::Crypto
       lz4::lz4
       jsoncpp_static    # Required by OpenVPN3 headers
   )
   
   target_compile_definitions(odc-tunnel-manager PRIVATE
       ASIO_STANDALONE
       ASIO_NO_DEPRECATED
   )
   
   target_compile_features(odc-tunnel-manager PRIVATE cxx_std_20)
   ```

2. **Note on jsoncpp**: The OpenVPN3 `manage.hpp` references `Json::Value` from jsoncpp. The top-level `CMakeLists.txt` already declares `jsoncpp` via CPM. Ensure the `jsoncpp_static` target is correctly exported for linking.

3. **Note on compilability**: The OpenVPN3 headers are template-heavy and include many transitive dependencies. If compile errors arise:
   - Missing `openvpn/` includes: verify the `thirdparty/minimal-openvpn3` include path is correct
   - C++20 compatibility: the `cmake/websocketpp_cxx20.patch` was for websocketpp; OpenVPN3 headers may also have C++20 issues. Check for `volatile` qualifier deprecation, `std::result_of` deprecation, etc. Apply fixes inline or as patches.
   - `openvpn/common/exception.hpp` may need `OPENVPN_EXCEPTION` macro definition
   - `openvpn/buffer/*.hpp` may need `bufstream.hpp`, `safe_buf.hpp` — verify these exist in the vendored tree

4. **Link `sodium::sodium`** if the TUN layer reuses `TunDevice` which may transitively require sodium (via wg_* modules). Check if `TunDevice` alone needs sodium — it should not (pure OS I/O). Only the Noise handshake needs sodium.

**Verification**: `cmake --build build --target odc-tunnel-manager -j$(nproc)` succeeds with zero errors.

---

### Task 4.7 — Enable OpenVPN in Config

**Files**: `tunnel-manager/config/tunnel-manager.yaml`, `deploy/test/docker-compose.yml`

**Implementation steps**:

1. **Update `tunnel-manager/config/tunnel-manager.yaml`**: Change `ovpn-main.enabled` from `false` to `true`:
   ```yaml
   - id: ovpn-main
     type: openvpn
     enabled: true          # was false
     config:
       listen_port: 1194
       protocol: udp
       subnet: 10.8.0.0/24
       cipher: AES-256-GCM
       ...
   ```

2. **Update `deploy/test/docker-compose.yml`**: Add `/dev/net/tun` device if not already present:
   ```yaml
   services:
     odc-test:
       devices:
         - /dev/net/tun:/dev/net/tun
       cap_add:
         - NET_ADMIN
   ```

3. **Update `deploy/test/entrypoint.sh`**: Ensure OpenVPN server cert is generated during test setup (alongside existing CA, registry server cert, hook client cert generation). The cert generation for OpenVPN should use the same CA.

**Verification**: Tunnel-manager starts with OpenVPN server. Port 1194/udp is listening. Integration test suite §6 (tunnel manager operations) should now also show OpenVPN server info.

---

## Phase 5 — Docker Integration Testing & CI

### Task 5.1 — Run Docker Integration Test from Clean State

**Files**: `deploy/test/Dockerfile`, `deploy/test/entrypoint.sh`, `deploy/test/test_suite.sh`, `deploy/test/docker-compose.yml` (verify, fix any issues)

**Current state**: Dockerfiles exist but have never been run in a clean environment (fresh clone, no host `build/` artifacts). The `.dockerignore` was created but not verified.

**Implementation steps**:

1. **Clean environment build**:
   ```bash
   cd odc-root
   docker compose -f deploy/test/docker-compose.yml build --no-cache 2>&1 | tee build.log
   ```
   Watch for:
   - CPM download failures (network access inside Docker)
   - Missing system packages (add to Dockerfile `apt-get install` as needed)
   - CMake configuration errors (missing dependencies)
   - Compile errors (header issues, C++20 compatibility)
   - Link errors (missing libraries)

2. **Fix any build failures**: Common issues in Docker environments:
   - `libcap-dev` missing → tunnel-manager needs `sys/capability.h`
   - `libsodium-dev` missing → add to Dockerfile (from Task 2.1)
   - OpenSSL dev headers version mismatch → pin version in Dockerfile
   - Insufficient memory for parallel compile → limit `-j` flag

3. **Run integration test suite**:
   ```bash
   docker compose -f deploy/test/docker-compose.yml up --abort-on-container-exit
   ```
   Expected: All 8 test sections pass. If not, fix the issues (likely related to the new WsServer code from Phase 1).

4. **Collect and review test reports**: The test framework outputs JSON reports to `deploy/test/output/`. Review for any failing assertions, analyze root cause, fix.

5. **Test via Wine (if applicable)**:
   ```bash
   docker compose -f deploy/test/docker-compose.yml --profile wine up
   ```
   Verify 7 Wine smoke tests pass for Windows PE32+ binaries.

**Verification**: `echo $?` after `docker compose up` returns 0. All test sections report PASS.

---

### Task 5.2 — Create GitHub Actions CI Workflow

**New file**: `.github/workflows/odc-root-ci.yml`

**Current state**: No CI workflow exists. The integration test plan (`docs/ODC_ROOT_INTEGRATION_TEST_PLAN.md` §F.2) describes what to build.

**Implementation steps**:

1. **Create `.github/workflows/odc-root-ci.yml`**:
   ```yaml
   name: odc-root CI
   
   on:
     push:
       branches: [main, master]
       paths:
         - 'odc-root/**'
     pull_request:
       paths:
         - 'odc-root/**'
   
   jobs:
     # Job 1: Linux build + unit tests + integration tests
     build-and-test-linux:
       runs-on: ubuntu-22.04
       steps:
         - uses: actions/checkout@v4
         
         - name: Install system dependencies
           run: |
             sudo apt-get update
             sudo apt-get install -y \
               cmake ninja-build g++-12 \
               libssl-dev libsodium-dev libcap-dev \
               wireguard-tools
         
         - name: Configure
           working-directory: odc-root
           run: |
             cmake -B build -G Ninja \
               -DCMAKE_BUILD_TYPE=Debug \
               -DCMAKE_CXX_COMPILER=g++-12 \
               -DBUILD_TESTING=ON
         
         - name: Build
           working-directory: odc-root
           run: cmake --build build -j$(nproc)
         
         - name: Run unit tests
           working-directory: odc-root
           run: cd build && ctest --output-on-failure -j$(nproc)
         
         - name: Run integration tests (Docker)
           working-directory: odc-root
           run: |
             docker compose -f deploy/test/docker-compose.yml build
             docker compose -f deploy/test/docker-compose.yml up --abort-on-container-exit
         
         - name: Upload test reports
           if: always()
           uses: actions/upload-artifact@v4
           with:
             name: test-reports-linux
             path: odc-root/deploy/test/output/
   
     # Job 2: Windows cross-compile + Wine smoke test
     cross-compile-windows:
       runs-on: ubuntu-22.04
       steps:
         - uses: actions/checkout@v4
         
         - name: Build Windows binaries
           working-directory: odc-root
           run: |
             docker build -f deploy/windows/Dockerfile -t odc-windows .
             docker create --name odc-win odc-windows
             docker cp odc-win:/output ./deploy/output/windows/
         
         - name: Run Wine smoke tests
           working-directory: odc-root
           run: |
             docker compose -f deploy/test/docker-compose.yml \
               --profile wine up --abort-on-container-exit
         
         - name: Upload Windows binaries
           uses: actions/upload-artifact@v4
           with:
             name: odc-windows-binaries
             path: odc-root/deploy/output/windows/
   
     # Job 3: macOS cross-compile + header audit
     cross-compile-macos:
       runs-on: macos-13
       steps:
         - uses: actions/checkout@v4
         
         - name: Install dependencies
           run: |
             brew install cmake ninja openssl@3 libsodium
         
         - name: Configure
           working-directory: odc-root
           run: |
             cmake -B build -G Ninja \
               -DCMAKE_BUILD_TYPE=Debug \
               -DBUILD_TUNNEL_MANAGER=OFF \
               -DBUILD_REGISTRY_MANAGER=OFF \
               -DBUILD_ODC_HOOK=ON \
               -DBUILD_TESTING=ON \
               -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
         
         - name: Build
           working-directory: odc-root
           run: cmake --build build -j$(sysctl -n hw.logicalcpu)
         
         - name: Run tests
           working-directory: odc-root
           run: cd build && ctest --output-on-failure -j$(sysctl -n hw.logicalcpu)
         
         - name: Header portability audit
           working-directory: odc-root
           run: |
             ! grep -rn '<sys/socket.h>\|<sys/un.h>\|<sys/capability.h>\|<linux/' \
               odc-hook/ common/ \
               --include='*.hpp' --include='*.cpp' \
               | grep -v 'platform_linux'
   ```

2. **CI-specific considerations**:
   - GitHub Actions runners don't have kernel WireGuard → WireGuard tests will skip via capability checks
   - Docker-in-Docker: the `ubuntu-22.04` runner supports Docker natively. Use `docker compose` (not `docker-compose`).
   - macOS runner: `macos-13` is Intel, `macos-latest` (macos-14) is Apple Silicon. Use `macos-13` for broader compatibility unless ARM build is tested separately.
   - OpenVPN server requires `/dev/net/tun` → may not be available in all CI environments. Tests that require TUN should be skipped gracefully.

**Verification**: Push a branch, CI runs. All 3 jobs pass green.

---

### Task 5.3 — Update PROGRESS_REPORT.md

**Files**: `odc-root/PROGRESS_REPORT.md`

**Implementation steps**:

1. Add Phase 5 entry documenting what was completed
2. Update "What Remains" section: remove completed items, add any new follow-ups discovered during implementation
3. Update dates, commit ranges, test counts
4. Mark OpenVPN server as "Complete" (was stub)

---

## Phase 6 — CLI Test Coverage

### Task 6.1 — Add CLI Unit Tests

**New file**: `tests/test_cli.cpp`
**Modified file**: `tests/CMakeLists.txt`

**Current state**: The CLI has zero dedicated tests. All 16 subcommands are manually smoke-tested.

**What to test**:

| Test Case | What It Verifies |
|---|---|
| `cli_cert_generate_ca` | `--generate-ca --ca-cert /tmp/ca.crt --ca-key /tmp/ca.key` → files exist, PEM format, `openssl verify` passes |
| `cli_cert_issue` | CA loaded → `--issue --cn test --ca-cert ... --ca-key ... --out-cert ... --out-key ...` → v3 cert, `serverAuth,clientAuth` EKU |
| `cli_cert_verify` | Valid cert → `--verify ... --ca-cert ...` → exit 0; Invalid cert → exit 1 |
| `cli_cert_fingerprint` | `--fingerprint --cert ...` → 47-char SHA-256 colon-separated hex |
| `cli_cert_revoke` | `--revoke ... --crl ...` → CRL file created, PEM format |
| `cli_cert_keypair_wg` | `--keypair-wg` → valid Curve25519 public/private keys |
| `cli_status` | With in-process IpcServer → `--status` → valid JSON response |
| `cli_services_list` | With in-process IpcServer → `--services-list` → JSON array |
| `cli_tunnel_status` | With in-process IpcServer → `--tunnel-status` → JSON with server info |
| `cli_hook_status` | With in-process IpcServer → `--hook-status` → JSON with connected/events fields |
| `cli_help` | `--help` → output contains all 5 command groups |
| `cli_missing_args` | Various subcommands without required args → non-zero exit, error on stderr |
| `cli_invalid_socket` | `--status --socket /nonexistent/path` → non-zero exit, error message |

**Implementation approach**:

1. **Isolate the CLI's `ipc_call()` function**: Extract from `main.cpp` into a testable helper. For testing, the function can be mocked or redirected to an in-process `IpcServer`.

2. **Simpler approach — test via subprocess**: Launch `odc-cli` as a child process with `fork()`/`exec()` or `popen()`. Capture stdout/stderr and exit code. This tests the actual binary end-to-end.

3. **Mixed approach**:
   - Local cert operations: test via subprocess (no IPC needed)
   - IPC operations: start a lightweight in-process `IpcServer` with test handlers, then call CLI with the socket path
   - Help/error: test via subprocess

4. **Add to `tests/CMakeLists.txt`**:
   ```cmake
   add_odc_test(test_cli
       SOURCES test_cli.cpp
       LIBRARIES odc-common catch2
       INCLUDES ${CMAKE_SOURCE_DIR}/tools/odc-cli
   )
   ```
   Note: The CLI source (`tools/odc-cli/main.cpp`) contains `main()` so we can't link it directly into a test. Either:
   - Refactor `main.cpp` to separate CLI logic from `main()`, or
   - Use subprocess testing only

   **Recommendation**: Use subprocess testing. It's the simplest, most realistic approach. The test uses `popen()` or `fork()+exec()` to run `odc-cli` with test arguments, captures output, and asserts on it. No linking needed.

5. **Test structure** (using Catch2):
   ```cpp
   #include <catch2/catch_test_macros.hpp>
   #include <cstdio>
   #include <string>
   #include <cstdlib>
   #include <filesystem>
   
   // Helper to run CLI and capture output
   struct CliResult {
       int exit_code;
       std::string stdout;
       std::string stderr;
   };
   
   CliResult run_cli(const std::string& args) {
       std::string cmd = "./build/tools/odc-cli/odc-cli " + args + " 2>&1";
       // Use popen or fork/exec to capture both stdout and exit code
       // ...
   }
   
   TEST_CASE("cert generate-ca produces valid PEM", "[cli][cert]") {
       auto tmp = std::filesystem::temp_directory_path();
       auto ca_cert = tmp / "test_ca.crt";
       auto ca_key = tmp / "test_ca.key";
       
       auto result = run_cli("cert generate-ca --ca-cert " + ca_cert.string() +
                             " --ca-key " + ca_key.string());
       REQUIRE(result.exit_code == 0);
       REQUIRE(std::filesystem::exists(ca_cert));
       REQUIRE(std::filesystem::exists(ca_key));
       // Verify PEM header
       REQUIRE(result.stdout.find("-----BEGIN CERTIFICATE-----") != std::string::npos);
   }
   
   // ... similar tests for all 13 test cases
   ```

**Verification**: `ctest -R test_cli --output-on-failure` passes all tests.

---

## Phase 7 — Documentation Updates

### Task 7.1 — Update ARCHITECTURE.md

**Files**: `docs/ARCHITECTURE.md`

**Current state**: 113-line document covering the original two-service architecture (tunnel-manager + registry-manager). Missing: odc-hook, CLI expansion, tunnel IPC server, hook IPC server, OpenVPN server architecture, WsServer implementation details.

**Updates needed**:

1. **System overview diagram**: Add odc-hook as a third service. Add odc-cli as a fourth component. Show new IPC sockets (tunnel-ipc.sock, hook.sock).

2. **Component catalog additions**:
   - `odc-hook`: Cross-platform hook client with mTLS WebSocket connection to registry-manager, EventBus, ThreadPool, VPN subsystem (WireGuard userspace + OpenVPN3 client), IPC server for status queries
   - `odc-cli`: Command-line tool with 16 subcommands across 5 groups (cert, tunnel, events, hook, services, status)
   - `OpenVPN3 Server Layer`: Three-tier architecture (Transport/UDP, TUN/interface, Management/auth) wrapping OpenVPN3 header-only protocol implementation
   - `Tunnel IPC Server`: Second IPC socket on tunnel-manager exposing tunnel.status, tunnel.peers.*, tunnel.health
   - `Hook IPC Server`: IPC socket on odc-hook exposing hook.status, hook.ping

3. **IPC protocol additions**:
   - Tunnel-manager IPC methods (5): status, peers.list, peers.add, peers.remove, health
   - Hook IPC methods (2): status, ping
   - JSON-RPC 2.0 notification support documented

4. **WebSocket protocol additions**:
   - Custom RFC 6455 implementation over raw asio::ssl::stream (no external WebSocket library)
   - Pub/sub topic model with subscribe/unsubscribe client messages
   - HTTP upgrade handshake details

5. **Database schema**: Add any new tables created by odc-hook or OpenVPN server (verify with existing schema documentation)

6. **Threading model updates**: Add odc-hook threading (io_context thread, ThreadPool workers, EventBus consumer threads, TUN read threads)

7. **Security model updates**: Add mTLS for hook-to-registry, tunnel-manager IPC authentication, OpenVPN client cert auth

---

### Task 7.2 — Update API.md

**Files**: `docs/API.md`

**Current state**: 137-line document covering original IPC methods and WebSocket API. Missing all new endpoints.

**Updates needed**:

1. **Tunnel IPC API** (new section):
   - `tunnel.status` → request/response with server list, types, peer counts
   - `tunnel.peers.list` → per-peer public key, allowed IP, endpoint, handshake, rx/tx bytes
   - `tunnel.peers.add` → request with public_key, auto-assigned IP in response
   - `tunnel.peers.remove` → request with public_key, confirmation
   - `tunnel.health` → connected/total peer counts per server

2. **Hook IPC API** (new section):
   - `hook.status` → connected state, server address, events_received, events_processed, thread_pool_active
   - `hook.ping` → pong

3. **CLI command reference** (new section): Document all 16 subcommands with invocation syntax, required args, output format

4. **WebSocket API updates**:
   - Subscribe/unsubscribe message format
   - Event stream message format with example payloads
   - `events watch` subscription protocol

5. **Error codes**: Add tunnel-manager specific error codes (-32001 through -32010 for tunnel-manager errors)

---

## Task Execution Order & Dependencies

```
Phase 1 (WsServer) ─────────────────────────────────────────────────────────────┐
  Task 1.1 (HTTP upgrade)                                                        │
    └─► Task 1.2 (read loop + frame parsing)                                     │
          └─► Task 1.3 (subscribe API + cleanup)                                 │
                └─► Task 1.4 (verify registry flow)                              │
                                                                                 │
Phase 2 (Build/deps) ── can run in parallel with Phase 1 ────────────────────────┤
  Task 2.1 (libsodium)                                                           │
  Task 2.2 (remove websocketpp dead code)                                        │
  Task 2.3 (clean build verification) ◄── after 1.1-1.3 and 2.1-2.2             │
                                                                                 │
Phase 3 (WireGuard) ── can run in parallel with Phase 1 & 2 ────────────────────  │
  Task 3.1 (verbose netlink)                                                     │
    └─► Task 3.2 (endpoint string helper)                                        │
                                                                                 │
Phase 4 (OpenVPN server) ── starts after Phase 1 (needs io_context patterns) ────┤
  Task 4.1 (architecture review — no code)                                       │
  Task 4.2 (transport layer)                                                     │
  Task 4.3 (TUN layer)                                                           │
  Task 4.4 (management layer)                                                    │
    └─► Task 4.5 (wire into OpenVpnServer)                                       │
          └─► Task 4.6 (build system)                                            │
                └─► Task 4.7 (enable in config)                                  │
                                                                                 │
Phase 5 (Docker + CI) ── starts after Phases 1-4 complete ───────────────────────┤
  Task 5.1 (Docker build + integration test)                                     │
  Task 5.2 (GitHub Actions CI)                                                   │
  Task 5.3 (update PROGRESS_REPORT.md)                                           │
                                                                                 │
Phase 6 (CLI tests) ── can run in parallel with Phase 4 ────────────────────────  │
  Task 6.1 (CLI unit tests)                                                      │
                                                                                 │
Phase 7 (Documentation) ── last, after all implementation done ──────────────────┘
  Task 7.1 (ARCHITECTURE.md)
  Task 7.2 (API.md)
```

**Parallelism opportunities**:
- Phase 1 + Phase 2 + Phase 3 can all run concurrently (different files, no conflicts)
- Phase 6 can run anytime after Phase 1 (CLI tests don't depend on OpenVPN)
- Phase 4 must start after Phase 1 patterns are established (io_context, async read/write)
- Phase 5 must run after all code changes
- Phase 7 is last

---

## Estimated Effort

| Phase | Tasks | Est. Days | Risk | Notes |
|---|---|---|---|---|
| 1 — WsServer | 4 | 3 days | Medium | RFC 6455 is well-defined but frame parsing is detail-heavy |
| 2 — Build/deps | 3 | 0.5 days | Low | Mostly CMake changes |
| 3 — WireGuard | 2 | 1 day | Low | Netlink API is known, reference in `wg-tools/` |
| 4 — OpenVPN | 7 | 6 days | **High** | 3 concrete class hierarchies, ~1500 lines new code, OpenVPN3 internal API complexity, C++20 compatibility issues |
| 5 — Docker + CI | 3 | 2 days | Medium | Docker networking, privileged mode, CI runner quirks |
| 6 — CLI tests | 1 | 1 day | Low | Subprocess testing is straightforward |
| 7 — Documentation | 2 | 0.5 days | Low | Updating existing docs |
| **Total** | **22** | **14 days** | | |

**Mitigations for Phase 4 risk**:
- The `ServerProto::Session` is already proven (used in production OpenVPN servers)
- The odc-hook `openvpn_client.cpp` provides reference code for integration patterns
- `TunDevice` is already implemented and tested in odc-hook
- Each layer (Transport, TUN, Management) can be developed and tested independently
- If a layer proves too complex, it can be simplified (e.g., single shared TUN instead of per-client TUN)

---

## Verification Checklist

Upon completion of all phases, verify the following:

### Unit Tests
- [ ] `ctest --output-on-failure` → 7/7 test suites pass (6 existing + test_cli)
- [ ] All 457+ original assertions still pass (no regressions)
- [ ] New CLI tests cover all 13 test cases

### Build
- [ ] Clean build from scratch: `rm -rf build && cmake -B build && cmake --build build -j$(nproc)` — zero errors
- [ ] Zero compiler warnings (or no new warnings)
- [ ] All 4 binaries produced: `odc-tunnel-manager`, `odc-registry-manager`, `odc-hook`, `odc-cli`

### WebSocket Server
- [ ] `websocat wss://localhost:9443/ws` connects, receives HTTP 101
- [ ] Send `{"type":"subscribe","topic":"test"}` → receive `{"type":"subscribed","topic":"test"}`
- [ ] `odc-cli events watch --timeout 5` connects, shows events
- [ ] Client disconnect → connection cleaned up, subscriptions removed

### WireGuard Server
- [ ] `odc-cli tunnel peers list` → returns full peer data with keys, IPs, stats
- [ ] `odc-cli tunnel peers add` → peer appears with auto-allocated IP
- [ ] `odc-cli tunnel peers remove` → peer removed, IP released
- [ ] `odc-cli tunnel health` → shows connected/total counts

### OpenVPN Server
- [ ] `odc-tunnel-manager` starts with OpenVPN on port 1194/udp
- [ ] `ss -ulnp | grep 1194` shows listening UDP socket
- [ ] OpenVPN client can connect, TLS handshake completes
- [ ] Client receives IP from subnet pool
- [ ] `odc-cli tunnel status` shows OpenVPN server with connected client
- [ ] Registry receives `tunnel.client.connected` notification
- [ ] Client disconnect → IP released, registry notified

### Integration Tests
- [ ] `docker compose -f deploy/test/docker-compose.yml up --abort-on-container-exit` → exit 0
- [ ] All 8 test sections pass (more may be added for OpenVPN)
- [ ] Tunnel manager section (§6) shows both WireGuard and OpenVPN data

### CI
- [ ] GitHub Actions push triggers workflow
- [ ] Linux job: build + unit tests + integration tests → green
- [ ] Windows job: cross-compile + Wine smoke tests → green
- [ ] macOS job: cross-compile/native build + header audit + ctest → green

### CLI
- [ ] All 16 subcommands produce valid output
- [ ] `odc-cli --help` lists all command groups with descriptions
- [ ] Error cases produce non-zero exit code and meaningful message

### Documentation
- [ ] ARCHITECTURE.md reflects all components and their relationships
- [ ] API.md documents all IPC methods, WebSocket messages, and CLI commands
- [ ] PROGRESS_REPORT.md updated with Phase 5 completion status
