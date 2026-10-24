# odc-root — Implementation Plan

**Goal**: Two standalone C++20 binaries (`odc-tunnel-manager`, `odc-registry-manager`) sharing a common library (`libodc-common`), communicating via Unix domain socket IPC, persisting to a shared SQLite database, configured via YAML. Zero dependency on `odc-onboard-v2`.

---

## Table of Contents

1. [Phase 0: Project Scaffolding](#phase-0-project-scaffolding)
   - [Task 0.1 — Create directory structure](#task-01--create-directory-structure)
   - [Task 0.2 — Top-level CMakeLists.txt and CPM dependencies](#task-02--top-level-cmakeliststxt-and-cpm-dependencies)
   - [Task 0.3 — `.gitignore`](#task-03--gitignore)
2. [Phase 1: Common Library (`libodc-common`)](#phase-1-common-library-libodc-common)
   - [Task 1.1 — `Logging` module](#task-11--logging-module)
   - [Task 1.2 — `YamlConfig` module](#task-12--yamlconfig-module)
   - [Task 1.3 — `SqliteDb` module](#task-13--sqlitedb-module)
   - [Task 1.4 — `CertManager` module](#task-14--certmanager-module)
   - [Task 1.5 — `IpcServer` module](#task-15--ipcserver-module)
   - [Task 1.6 — `IpcClient` module](#task-16--ipcclient-module)
   - [Task 1.7 — Unit tests for common library](#task-17--unit-tests-for-common-library)
3. [Phase 2: Tunnel Manager](#phase-2-tunnel-manager)
   - [Task 2.1 — Vendored dependencies](#task-21--vendored-dependencies)
   - [Task 2.2 — `WireGuardServer` module](#task-22--wireguardserver-module)
   - [Task 2.3 — `OpenVpnServer` module](#task-23--openvpnserver-module)
   - [Task 2.4 — `SessionTracker` module](#task-24--sessiontracker-module)
   - [Task 2.5 — `CertGenerator` module (VPN)](#task-25--certgenerator-module-vpn)
   - [Task 2.6 — YAML config schema](#task-26--yaml-config-schema)
   - [Task 2.7 — `TunnelManager` service core](#task-27--tunnelmanager-service-core)
   - [Task 2.8 — `main.cpp` entry point](#task-28--maincpp-entry-point)
   - [Task 2.9 — CMakeLists.txt for tunnel-manager](#task-29--cmakeliststxt-for-tunnel-manager)
   - [Task 2.10 — systemd unit file](#task-210--systemd-unit-file)
   - [Task 2.11 — Integration tests](#task-211--integration-tests)
4. [Phase 3: Registry Manager](#phase-3-registry-manager)
   - [Task 3.1 — Vendored dependency (websocketpp)](#task-31--vendored-dependency-websocketpp)
   - [Task 3.2 — `WsServer` module](#task-32--wsserver-module)
   - [Task 3.3 — `WebhookClient` module](#task-33--webhookclient-module)
   - [Task 3.4 — `DataStore` module](#task-34--datastore-module)
   - [Task 3.5 — `CertGenerator` module (WS)](#task-35--certgenerator-module-ws)
   - [Task 3.6 — YAML config schema](#task-36--yaml-config-schema)
   - [Task 3.7 — `RegistryManager` service core](#task-37--registrymanager-service-core)
   - [Task 3.8 — `main.cpp` entry point](#task-38--maincpp-entry-point)
   - [Task 3.9 — CMakeLists.txt for registry-manager](#task-39--cmakeliststxt-for-registry-manager)
   - [Task 3.10 — systemd unit file](#task-310--systemd-unit-file)
   - [Task 3.11 — Integration tests](#task-311--integration-tests)
5. [Phase 4: Cross-Service Integration](#phase-4-cross-service-integration)
   - [Task 4.1 — End-to-end IPC integration test](#task-41--end-to-end-ipc-integration-test)
   - [Task 4.2 — CLI tool (`odc-cli`)](#task-42--cli-tool-odc-cli)
   - [Task 4.3 — Documentation](#task-43--documentation)
6. [Build & Test Reference](#build--test-reference)

---

## Phase 0: Project Scaffolding

### Task 0.1 — Create directory structure

Create the following directory tree under `odc-root/`:

```
odc-root/
├── CMakeLists.txt
├── .gitignore
├── common/
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── odc/
│   │       ├── logging.hpp
│   │       ├── yaml_config.hpp
│   │       ├── sqlite_db.hpp
│   │       ├── cert_manager.hpp
│   │       ├── ipc_server.hpp
│   │       └── ipc_client.hpp
│   └── src/
│       ├── logging.cpp
│       ├── yaml_config.cpp
│       ├── sqlite_db.cpp
│       ├── cert_manager.cpp
│       ├── ipc_server.cpp
│       └── ipc_client.cpp
├── tunnel-manager/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── tunnel_manager.hpp
│   ├── tunnel_manager.cpp
│   ├── wireguard_server.hpp
│   ├── wireguard_server.cpp
│   ├── openvpn_server.hpp
│   ├── openvpn_server.cpp
│   ├── session_tracker.hpp
│   ├── session_tracker.cpp
│   ├── vpn_cert_generator.hpp
│   ├── vpn_cert_generator.cpp
│   └── config/
│       └── tunnel-manager.yaml
├── registry-manager/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── registry_manager.hpp
│   ├── registry_manager.cpp
│   ├── ws_server.hpp
│   ├── ws_server.cpp
│   ├── webhook_client.hpp
│   ├── webhook_client.cpp
│   ├── data_store.hpp
│   ├── data_store.cpp
│   ├── ws_cert_generator.hpp
│   ├── ws_cert_generator.cpp
│   └── config/
│       └── registry-manager.yaml
├── thirdparty/
│   ├── minimal-openvpn3/
│   │   └── openvpn/
│   │       ├── server/
│   │       │   ├── vpnservpool.hpp
│   │       │   ├── vpnservpool.cpp
│   │       │   ├── servproto.hpp
│   │       │   ├── servproto.cpp
│   │       │   ├── manage.hpp
│   │       │   ├── manage.cpp
│   │       │   ├── servhalt.hpp
│   │       │   ├── listenlist.hpp
│   │       │   ├── peeraddr.hpp
│   │       │   └── peerstats.hpp
│   │       └── (supporting crypto/ssl/transport headers)
│   └── wg_netlink.hpp
├── tests/
│   ├── test_common.cpp
│   ├── test_tunnel_manager.cpp
│   ├── test_registry_manager.cpp
│   └── test_ipc_integration.cpp
├── tools/
│   └── odc-cli/
│       ├── CMakeLists.txt
│       └── main.cpp
├── docs/
│   ├── ARCHITECTURE.md
│   └── API.md
└── configs/
    ├── systemd/
    │   ├── odc-tunnel-manager.service
    │   └── odc-registry-manager.service
    └── examples/
        ├── tunnel-manager.yaml
        └── registry-manager.yaml
```

**Verification**: `find . -type d | sort` shows all directories exist.

---

### Task 0.2 — Top-level CMakeLists.txt and CPM dependencies

Write `odc-root/CMakeLists.txt` with:

- `cmake_minimum_required(VERSION 3.18)`
- `project(odc-root VERSION 0.1.0 LANGUAGES CXX C)`
- `set(CMAKE_CXX_STANDARD 20)`
- `set(CMAKE_CXX_STANDARD_REQUIRED ON)`
- `include(FetchContent)` for CPM (same pattern as how `odc-onboard-v2` bootstraps CPM)
- CPM-declared dependencies:
  - `yaml-cpp` (`jbeder/yaml-cpp@0.8.0`)
  - `jsoncpp` (`open-source-parsers/jsoncpp@1.9.6` — needed by OpenVPN3)
  - `asio` (`chriskohlhoff/asio@asio-1-30-2`)
  - `lz4` (`lz4/lz4@v1.10.0` — needed by OpenVPN3)
  - `websocketpp` (`zaphoyd/websocketpp@0.8.2`)
- `find_package` for system packages:
  - `spdlog`, `CLI11`, `nlohmann_json`, `fmt`
  - `OpenSSL`, `libsodium`
  - `sqlite3` (`find_package(SQLite3 REQUIRED)`)
- `add_subdirectory(common)`
- `add_subdirectory(tunnel-manager)`
- `add_subdirectory(registry-manager)`
- `add_subdirectory(tools/odc-cli)` — conditional on CLI11 found

**Verification**: `cmake -B build && cmake --build build -j$(nproc)` configures without errors (targets may not link yet, but CMake configures).

---

### Task 0.3 — `.gitignore`

Write `odc-root/.gitignore`:

```
build/
cmake-build-*/
*.o
*.so
*.a
*.db
*.db-journal
*.db-wal
*.db-shm
*~
*.swp
.DS_Store
```

---

## Phase 1: Common Library (`libodc-common`)

### Task 1.1 — `Logging` module

**Files**: `common/include/odc/logging.hpp`, `common/src/logging.cpp`, `common/CMakeLists.txt`

Write a thin spdlog wrapper:

- `namespace odc`
- `using LoggerPtr = std::shared_ptr<spdlog::logger>;`
- `LoggerPtr get_logger(const std::string& name);` — returns named logger or creates one
- `void set_log_level(spdlog::level::level_enum level);` — global log level
- `void init_logging(spdlog::level::level_enum level, bool console_only);` — initialize sinks

`common/CMakeLists.txt`:

```cmake
add_library(odc-common STATIC
    src/logging.cpp
    src/yaml_config.cpp
    src/sqlite_db.cpp
    src/cert_manager.cpp
    src/ipc_server.cpp
    src/ipc_client.cpp
)
target_include_directories(odc-common PUBLIC include)
target_link_libraries(odc-common PUBLIC
    spdlog::spdlog
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json
    OpenSSL::SSL OpenSSL::Crypto
    SQLite::SQLite3
)
target_compile_features(odc-common PUBLIC cxx_std_20)
```

**Verification**: `cmake --build build --target odc-common` compiles `logging.cpp` without errors.

---

### Task 1.2 — `YamlConfig` module

**Files**: `common/include/odc/yaml_config.hpp`, `common/src/yaml_config.cpp`

Write a generic YAML config load/save utility:

- `YAML::Node load_yaml(const std::filesystem::path& path);`
- `void save_yaml(const YAML::Node& root, const std::filesystem::path& path);` — atomic write: write to `.tmp`, backup existing to `.bak`, rename `.tmp` to target
- `std::optional<T> get_value(const YAML::Node& node, const std::string& key);` — templated accessor
- `bool has_key(const YAML::Node& node, const std::string& key);`

**Verification**: `cmake --build build --target odc-common` compiles.

---

### Task 1.3 — `SqliteDb` module

**Files**: `common/include/odc/sqlite_db.hpp`, `common/src/sqlite_db.cpp`

Write a thread-safe SQLite wrapper:

**Public API**:

```cpp
namespace odc {

class SqliteDb {
public:
    bool open(const std::string& path);
    void close();
    bool is_open() const;

    // Execute a write statement (INSERT, UPDATE, DELETE, CREATE)
    bool execute(const std::string& sql);

    // Prepared statement for parameterized queries
    class Statement {
    public:
        bool bind(int index, int value);
        bool bind(int index, int64_t value);
        bool bind(int index, double value);
        bool bind(int index, const std::string& value);
        bool bind(int index, std::nullptr_t);
        bool execute();

        // Row iteration for SELECT
        bool step();
        int column_int(int index);
        int64_t column_int64(int index);
        double column_double(int index);
        std::string column_text(int index);
        bool column_is_null(int index);
        void reset();
    };
    Statement prepare(const std::string& sql);

    // Transaction support
    bool begin_transaction();
    bool commit();
    bool rollback();

    // Schema migration
    int schema_version();
    bool ensure_schema(int target_version, const std::string& migration_sql);

    // Utility
    int64_t last_insert_rowid();

private:
    sqlite3* db_{nullptr};
    std::recursive_mutex mutex_;
};

} // namespace odc
```

**Implementation notes**:
- Open database with `SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX`
- Enable WAL mode: `PRAGMA journal_mode=WAL;`
- Enable foreign keys: `PRAGMA foreign_keys=ON;`
- `Statement` holds a `sqlite3_stmt*` and calls `sqlite3_prepare_v2` / `sqlite3_finalize`
- `ensure_schema()` uses a `schema_version` pragma table or a dedicated `schema_migrations` table: if current < target, run migration SQL inside a transaction

**Verification**: `cmake --build build --target odc-common` compiles.

---

### Task 1.4 — `CertManager` module

**Files**: `common/include/odc/cert_manager.hpp`, `common/src/cert_manager.cpp`

Write an OpenSSL-based certificate authority and client certificate generator:

**Public API**:

```cpp
namespace odc {

struct CaConfig {
    std::string ca_cert_path;
    std::string ca_key_path;
    std::string country{"XX"};
    std::string organization{"odc-root"};
    int key_bits{2048};
    int validity_days{365};
};

struct ClientCert {
    std::string cert_pem;
    std::string key_pem;
    std::string fingerprint;   // SHA256 hex
    std::string common_name;
    std::string serial;
    int64_t not_before;         // unix timestamp
    int64_t not_after;          // unix timestamp
};

class CertManager {
public:
    // CA lifecycle
    bool create_ca(const CaConfig& config);
    bool load_ca(const std::string& cert_path, const std::string& key_path);
    bool ca_loaded() const;

    // Client certificate lifecycle
    ClientCert issue_client_cert(const std::string& common_name);
    bool revoke_cert(const std::string& fingerprint, const std::string& crl_path);
    bool verify_cert(const std::string& cert_pem);

    // Key generation (for WireGuard-style keypairs — raw curve25519 keys)
    static std::string generate_curve25519_private_key();
    static std::string generate_curve25519_public_key(const std::string& private_key);

private:
    EVP_PKEY* ca_key_{nullptr};
    X509* ca_cert_{nullptr};
    std::mutex mutex_;
    int64_t next_serial_{1};
};

} // namespace odc
```

**Implementation notes**:
- CA creation: generate RSA/EC key (`EVP_PKEY_new`, `EVP_PKEY_keygen`), create self-signed X509 cert (`X509_new`, `X509_sign`)
- Client cert: generate key, create CSR or directly create cert signed by CA
- Fingerprint: `X509_digest()` with `EVP_sha256()`, convert to hex
- Curve25519 key generation: use `EVP_PKEY_Q_keygen` or `EVP_PKEY_keygen` with `SN_X25519`, or if libsodium is available use `crypto_scalarmult_curve25519_base` / `crypto_box_keypair`

**Verification**: `cmake --build build --target odc-common` compiles.

---

### Task 1.5 — `IpcServer` module

**Files**: `common/include/odc/ipc_server.hpp`, `common/src/ipc_server.cpp`

Write a JSON-RPC 2.0 server over Unix domain socket:

**Public API**:

```cpp
namespace odc {

using MethodHandler = std::function<nlohmann::json(const nlohmann::json& params)>;

class IpcServer {
public:
    IpcServer(const std::string& socket_path);
    ~IpcServer();

    bool start();
    void run();
    void stop();

    // Method registration
    void register_method(const std::string& method, MethodHandler handler);

    // Callbacks for connection lifecycle (optional)
    using ConnectCallback = std::function<void(int client_fd)>;
    using DisconnectCallback = std::function<void(int client_fd)>;
    void set_on_connect(ConnectCallback cb);
    void set_on_disconnect(DisconnectCallback cb);

private:
    // ... accept loop, session handling, JSON-RPC dispatch
};

} // namespace odc
```

**Implementation notes**:
- Single-threaded accept + read/write loop using non-blocking I/O or asio
- JSON-RPC 2.0: request `{"jsonrpc":"2.0","method":"...","params":{...},"id":1}`, response `{"jsonrpc":"2.0","result":{...},"id":1}`, error `{"jsonrpc":"2.0","error":{"code":...,"message":"..."},"id":1}`
- Message framing: each message is a single line of JSON terminated by `\n`
- Support for notifications (`"id": null` — no response sent)
- Clean up socket file on stop

**Verification**: `cmake --build build --target odc-common` compiles.

---

### Task 1.6 — `IpcClient` module

**Files**: `common/include/odc/ipc_client.hpp`, `common/src/ipc_client.cpp`

Write a JSON-RPC 2.0 client over Unix domain socket:

**Public API**:

```cpp
namespace odc {

class IpcClient {
public:
    IpcClient(const std::string& socket_path);
    ~IpcClient();

    bool connect();
    void disconnect();
    bool is_connected() const;

    // Synchronous call (blocks until response)
    nlohmann::json call(const std::string& method, const nlohmann::json& params = {}, int timeout_ms = 5000);

    // Fire-and-forget notification
    void notify(const std::string& method, const nlohmann::json& params = {});

    // Auto-reconnect configuration
    void set_reconnect_interval(int seconds);
    void set_on_disconnect(std::function<void()> callback);

private:
    // ... socket management, reconnect loop
};

} // namespace odc
```

**Implementation notes**:
- Blocking I/O with `SO_RCVTIMEO` / `SO_SNDTIMEO` (simple, works for infrequent IPC messages)
- `call()`: write JSON-RPC request, read response line, parse and return result
- `notify()`: write JSON-RPC request with `"id": null`, don't wait for response
- `reconnect_interval`: if connection drops, retry connect() every N seconds

**Verification**: `cmake --build build --target odc-common` compiles.

---

### Task 1.7 — Unit tests for common library

**File**: `tests/test_common.cpp`

Write unit tests covering:

| Test | Module | What it verifies |
|---|---|---|
| `sqlite_open_close` | SqliteDb | Open in-memory DB, close it |
| `sqlite_create_table` | SqliteDb | CREATE TABLE, insert row, SELECT back |
| `sqlite_prepared_statement` | SqliteDb | Parameterized INSERT/SELECT with Statement |
| `sqlite_transactions` | SqliteDb | BEGIN/COMMIT/ROLLBACK |
| `sqlite_migrations` | SqliteDb | `ensure_schema()` upgrades from 0 to 3 |
| `sqlite_wal_mode` | SqliteDb | Verify WAL journal files created |
| `cert_create_ca` | CertManager | Generate CA cert+key, verify load back |
| `cert_issue_client` | CertManager | Issue client cert, verify fingerprint |
| `cert_curve25519_keys` | CertManager | Generate keypair, verify public from private |
| `yaml_load_save` | YamlConfig | Load YAML, modify, save, load again, verify equality |
| `yaml_atomic_write` | YamlConfig | Save produces .bak and .tmp cleanup |
| `ipc_server_start_stop` | IpcServer | Start server, connect client, stop, cleanup socket |
| `ipc_request_response` | IpcServer/IpcClient | Register method, call it, get response |
| `ipc_notification` | IpcServer/IpcClient | Send notification, handler invoked, no response |
| `ipc_invalid_json` | IpcServer | Malformed JSON → error response |
| `ipc_unknown_method` | IpcServer | Unregistered method → error response |
| `ipc_reconnect` | IpcClient | Server down → client reconnects when server restarts |
| `logging_levels` | Logging | Set different log levels, verify output |

Use a test framework consistent with the project — if `odc-onboard-v2` tests use a specific pattern (bash-based integration tests or Catch2/gtest), follow that pattern. For unit tests, recommend Catch2 (header-only, fetched via CPM).

**Verification**: `cmake --build build --target test_common && ./build/tests/test_common` — all tests pass.

---

## Phase 2: Tunnel Manager

### Task 2.1 — Vendored dependencies

Copy `wg_netlink.hpp` from `odc-onboard-v2/src/core/wg_netlink.hpp` into `odc-root/thirdparty/wg_netlink.hpp`.

Copy the `openvpn/` subtree from `odc-onboard-v2/thirdparty/minimal-openvpn3/openvpn/` into `odc-root/thirdparty/minimal-openvpn3/openvpn/`, **keeping only**:
- `common/`, `crypto/`, `ssl/`, `transport/`, `tun/` (base infrastructure)
- `server/` (server protocol: `servproto.*`, `vpnservpool.*`, `manage.*`, `servhalt.*`, `listenlist.*`, `peeraddr.*`, `peerstats.*`)

Exclude `client/` directory entirely.

**Verification**: `find thirdparty/minimal-openvpn3 -name '*.hpp' | wc -l` > 0. `find thirdparty/minimal-openvpn3 -name client -type d` returns nothing.

---

### Task 2.2 — `WireGuardServer` module

**Files**: `tunnel-manager/wireguard_server.hpp`, `tunnel-manager/wireguard_server.cpp`

Write a server-side WireGuard management wrapper using `wg_netlink.hpp`:

**Public API**:

```cpp
namespace odc {

struct WgServerConfig {
    std::string interface_name;
    uint16_t listen_port;
    std::string private_key;        // base64 encoded
    std::string subnet;             // "10.0.0.0/24"
    int mtu{1420};
    uint16_t keepalive_sec{25};
};

struct WgPeerInfo {
    std::string public_key;
    std::string allowed_ip;         // assigned IP from subnet pool
    std::string endpoint_addr;      // remote IP:port
    int64_t last_handshake_sec;     // seconds since last handshake
    uint64_t bytes_rx;
    uint64_t bytes_tx;
};

class WireGuardServer {
public:
    explicit WireGuardServer(const WgServerConfig& config);
    ~WireGuardServer();

    bool start();
    void stop();
    bool is_running() const;

    // Peer management
    bool add_peer(const std::string& public_key, const std::string& allowed_ip);
    bool remove_peer(const std::string& public_key);
    WgPeerInfo get_peer_info(const std::string& public_key);
    std::vector<WgPeerInfo> list_peers();

    // IP pool management
    std::string allocate_ip();
    void release_ip(const std::string& ip);
    bool is_ip_available(const std::string& ip) const;

    // Callbacks
    using PeerCallback = std::function<void(const WgPeerInfo& peer)>;
    void set_on_peer_connected(PeerCallback cb);
    void set_on_peer_disconnected(PeerCallback cb);

private:
    void poll_peers();  // Periodic netlink query to detect handshake state changes
    // ...
};

} // namespace odc
```

**Implementation notes**:
- `start()`: calls `WgNetlink::create_interface()`, `WgNetlink::set_device()` with private key and listen port, `WgNetlink::bring_interface_up()`, assigns subnet address to interface
- `add_peer()`: adds peer to the WireGuard device via `WgNetlink::set_device()` with `WGDEVICE_F_REPLACE_PEERS`. Updates the full peer list atomically
- Peer detection: poll every N seconds (configurable, default 5s) using netlink to read interface status. If `last_handshake_sec` goes from 0 to non-zero → new connection. If previously connected peer drops from the kernel peer list → disconnection
- IP pool: track allocated/released IPs in an `std::unordered_set<std::string>` with a pre-computed pool from the subnet range (excluding network address, broadcast, and the server's own IP)

**Verification**: Build compiles. To test, requires root and kernel WireGuard module.

---

### Task 2.3 — `OpenVpnServer` module

**Files**: `tunnel-manager/openvpn_server.hpp`, `tunnel-manager/openvpn_server.cpp`

Write a server-side OpenVPN3 wrapper:

**Public API**:

```cpp
namespace odc {

struct OvpnServerConfig {
    uint16_t listen_port;
    std::string protocol;           // "udp" or "tcp"
    std::string subnet;             // "10.8.0.0/24"
    std::string cipher;             // "AES-256-GCM"
    std::string ca_cert_path;
    std::string server_cert_path;
    std::string server_key_path;
    std::string dh_params_path;
    std::string tls_auth_key_path;  // optional
    std::vector<std::string> push_routes;
    std::vector<std::string> dns_servers;
};

struct OvpnClientInfo {
    std::string common_name;
    std::string assigned_ip;
    std::string remote_addr;
    int64_t connected_at;
    uint64_t bytes_rx;
    uint64_t bytes_tx;
};

class OpenVpnServer {
public:
    explicit OpenVpnServer(const OvpnServerConfig& config);
    ~OpenVpnServer();

    bool start();
    void stop();
    bool is_running() const;

    std::vector<OvpnClientInfo> list_clients();
    bool disconnect_client(const std::string& common_name);

    using ClientCallback = std::function<void(const OvpnClientInfo& client)>;
    void set_on_client_connected(ClientCallback cb);
    void set_on_client_disconnected(ClientCallback cb);

private:
    // ...
};

} // namespace odc
```

**Implementation notes**:
- Uses OpenVPN3 server-side classes: `ServerProto::Factory`, `VPNServerPool`, `Listen::List`
- SSL factory configured with `Mode::SERVER` and server certificates
- Runs in a dedicated I/O thread (asio-based event loop)
- Client connect/disconnect events forwarded to callbacks
- IP pool managed by `VPNServerPool` internally

**Verification**: Build compiles with server-side sources linked. Runtime testing requires root + real OpenVPN client.

---

### Task 2.4 — `SessionTracker` module

**Files**: `tunnel-manager/session_tracker.hpp`, `tunnel-manager/session_tracker.cpp`

Tracks active VPN client sessions in memory and persists to SQLite:

**Public API**:

```cpp
namespace odc {

struct ClientSession {
    std::string client_id;
    std::string server_id;
    std::string tunnel_type;        // "wireguard" or "openvpn"
    std::string common_name;
    std::string cert_fingerprint;
    std::string assigned_ip;
    std::string public_key;         // Wg only
    std::string remote_endpoint;
    bool connected{false};
    int64_t connected_at{0};
    int64_t disconnected_at{0};
    uint64_t bytes_rx{0};
    uint64_t bytes_tx{0};
};

class SessionTracker {
public:
    explicit SessionTracker(SqliteDb& db);
    bool initialize();  // Create tables if not exist

    // Session lifecycle
    bool register_session(const ClientSession& session);
    bool mark_connected(const std::string& client_id, const std::string& assigned_ip);
    bool mark_disconnected(const std::string& client_id);
    bool update_stats(const std::string& client_id, uint64_t rx, uint64_t tx);

    // Queries
    std::optional<ClientSession> get_session(const std::string& client_id);
    std::vector<ClientSession> list_active_sessions();
    std::vector<ClientSession> list_sessions_by_server(const std::string& server_id);

private:
    SqliteDb& db_;
    std::mutex mutex_;
    std::unordered_map<std::string, ClientSession> active_;  // In-memory cache
};

} // namespace odc
```

**Implementation notes**:
- On `initialize()`: create `vpn_servers` and `vpn_clients` tables
- On session register/update: write to SQLite AND update in-memory cache atomically under mutex
- `list_active_sessions()`: returns from cache (fast), no DB query

**Verification**: Build compiles.

---

### Task 2.5 — `CertGenerator` module (VPN)

**Files**: `tunnel-manager/vpn_cert_generator.hpp`, `tunnel-manager/vpn_cert_generator.cpp`

Thin wrapper around `CertManager` (common lib) with VPN-specific naming:

**Public API**:

```cpp
namespace odc {

struct VpnClientCredentials {
    std::string client_id;
    std::string common_name;
    std::string cert_pem;
    std::string key_pem;
    std::string fingerprint;
    // For WireGuard: raw curve25519 keys
    std::string wg_private_key;     // base64
    std::string wg_public_key;      // base64
};

class VpnCertGenerator {
public:
    explicit VpnCertGenerator(CertManager& cert_mgr, SqliteDb& db);

    VpnClientCredentials generate_for_client(
        const std::string& client_id,
        const std::string& tunnel_type,  // "wireguard" or "openvpn"
        const std::string& common_name);

    bool revoke(const std::string& client_id, const std::string& crl_path);

private:
    CertManager& cert_mgr_;
    SqliteDb& db_;
};

} // namespace odc
```

**Implementation notes**:
- For OpenVPN clients: generate X509 cert via `CertManager::issue_client_cert()`
- For WireGuard clients: generate curve25519 keypair via `CertManager::generate_curve25519_*()`
- Store generated credentials in SQLite `vpn_clients` table

**Verification**: Build compiles.

---

### Task 2.6 — YAML config schema

**File**: `tunnel-manager/config/tunnel-manager.yaml`

Write default config template:

```yaml
version: 1

general:
  log_level: info
  pid_file: /run/odc-root/tunnel-manager.pid

ipc:
  socket_path: /run/odc-root/odc-ipc.sock
  reconnect_interval_sec: 5

database:
  path: /var/lib/odc-root/odc-root.db

ca:
  cert_path: /etc/odc-root/ca/ca.crt
  key_path: /etc/odc-root/ca/ca.key
  key_bits: 2048
  validity_days: 365

servers:
  - id: wg-main
    type: wireguard
    enabled: true
    config:
      interface_name: wg-root
      listen_port: 51820
      private_key_path: /etc/odc-root/wg/server.key
      subnet: 10.0.0.0/24
      mtu: 1420
      keepalive_sec: 25

  - id: ovpn-main
    type: openvpn
    enabled: false
    config:
      listen_port: 1194
      protocol: udp
      subnet: 10.8.0.0/24
      cipher: AES-256-GCM
      ca_cert_path: /etc/odc-root/ca/ca.crt
      server_cert_path: /etc/odc-root/ovpn/server.crt
      server_key_path: /etc/odc-root/ovpn/server.key
      dh_params_path: /etc/odc-root/ovpn/dh.pem
      tls_auth_key_path: /etc/odc-root/ovpn/ta.key
      push_routes: []
      dns_servers: []
```

**Verification**: YAML parses successfully via yaml-cpp.

---

### Task 2.7 — `TunnelManager` service core

**Files**: `tunnel-manager/tunnel_manager.hpp`, `tunnel-manager/tunnel_manager.cpp`

Orchestrates all tunnel-manager modules:

**Public API**:

```cpp
namespace odc {

class TunnelManager {
public:
    TunnelManager(const std::string& config_path);
    ~TunnelManager();

    bool initialize();
    bool start();
    void stop();
    void run();     // Blocking event loop

    // Accessors for CLI/testing
    const YamlConfig& config() const;
    SessionTracker& sessions();
    WireGuardServer* wg_server();
    OpenVpnServer* ovpn_server();

private:
    void load_config();
    void setup_ipc();
    void setup_servers();
    void notify_registry(const std::string& method, const nlohmann::json& params);

    // ...
};

} // namespace odc
```

**Implementation notes**:
- `initialize()`: load YAML config, open SQLite, initialize SessionTracker, create CA if missing, create IPC client connection to registry-manager
- `start()`: for each enabled server in config, create and start `WireGuardServer` or `OpenVpnServer`, wire peer/client callbacks to SessionTracker, wire IPC notifications
- `run()`: main loop — poll WireGuard peers periodically, handle signals (SIGTERM/SIGINT for graceful shutdown), keep alive
- Peer connected callback: `SessionTracker::mark_connected()` → `IpcClient::notify("tunnel.client.connected", {...})`
- Peer disconnected callback: `SessionTracker::mark_disconnected()` → `IpcClient::notify("tunnel.client.disconnected", {...})`
- Graceful shutdown: stop all servers, disconnect IPC, close DB

**IPC methods the tunnel-manager receives** (via `IpcClient`, from registry-manager):
- `tunnel.get_status` — return server configs and active client list
- `tunnel.add_client` — generate credentials for a new client
- `tunnel.remove_client` — revoke credentials and disconnect client
- `tunnel.disconnect_client` — force-disconnect a specific client
- `ping` — health check

**Verification**: Build compiles.

---

### Task 2.8 — `main.cpp` entry point

**File**: `tunnel-manager/main.cpp`

Write the entry point:

```cpp
#include <CLI/CLI.hpp>
#include "tunnel_manager.hpp"

int main(int argc, char* argv[]) {
    CLI::App app{"odc-tunnel-manager — VPN server management daemon"};

    std::string config_path = "/etc/odc-root/tunnel-manager.yaml";
    bool daemon_mode = false;
    bool check_privileges = false;

    app.add_option("-c,--config", config_path, "Path to YAML config file");
    app.add_flag("-d,--daemon", daemon_mode, "Run as daemon (fork to background)");
    app.add_flag("--check-privileges", check_privileges, "Verify CAP_NET_ADMIN");

    CLI11_PARSE(app, argc, argv);

    if (check_privileges) { /* ... check CAP_NET_ADMIN ... */ }

    // Daemonize if requested
    if (daemon_mode) { /* fork, setsid, redirect stdio ... */ }

    // Signal handling
    // SIGTERM / SIGINT → stop TunnelManager gracefully

    odc::TunnelManager mgr(config_path);
    if (!mgr.initialize()) return 1;
    if (!mgr.start()) return 1;
    mgr.run();

    return 0;
}
```

**Verification**: `./build/tunnel-manager/odc-tunnel-manager --help` prints usage.

---

### Task 2.9 — CMakeLists.txt for tunnel-manager

**File**: `tunnel-manager/CMakeLists.txt`

```cmake
add_executable(odc-tunnel-manager
    main.cpp
    tunnel_manager.cpp
    wireguard_server.cpp
    openvpn_server.cpp
    session_tracker.cpp
    vpn_cert_generator.cpp
)

target_include_directories(odc-tunnel-manager PRIVATE
    ${CMAKE_SOURCE_DIR}/thirdparty
    ${CMAKE_SOURCE_DIR}/thirdparty/minimal-openvpn3
)

target_link_libraries(odc-tunnel-manager PRIVATE
    odc-common
    CLI11::CLI11
    # OpenVPN3 server static library (built from minimal-openvpn3)
    openvpn3_server
)

# Install
install(TARGETS odc-tunnel-manager RUNTIME DESTINATION bin)
install(FILES config/tunnel-manager.yaml DESTINATION /etc/odc-root)
```

**Build `openvpn3_server` static library** (in top-level or tunnel-manager CMakeLists):

```cmake
add_library(openvpn3_server STATIC
    thirdparty/minimal-openvpn3/openvpn/server/vpnservpool.cpp
    thirdparty/minimal-openvpn3/openvpn/server/servproto.cpp
    thirdparty/minimal-openvpn3/openvpn/server/manage.cpp
    # + any other server-side .cpp files
    # + crypto/transport common sources
)
target_link_libraries(openvpn3_server PUBLIC
    OpenSSL::SSL OpenSSL::Crypto
    asio::asio
    lz4::lz4
)
```

**Verification**: `cmake --build build --target odc-tunnel-manager` produces binary.

---

### Task 2.10 — systemd unit file

**File**: `configs/systemd/odc-tunnel-manager.service`

```ini
[Unit]
Description=odc-tunnel-manager — VPN server management daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/odc-tunnel-manager --config /etc/odc-root/tunnel-manager.yaml --daemon
Restart=on-failure
RestartSec=5
User=root
CapabilityBoundingSet=CAP_NET_ADMIN CAP_NET_RAW
AmbientCapabilities=CAP_NET_ADMIN CAP_NET_RAW
RuntimeDirectory=odc-root
RuntimeDirectoryMode=0755

[Install]
WantedBy=multi-user.target
```

---

### Task 2.11 — Integration tests

**File**: `tests/test_tunnel_manager.cpp`

Core test scenarios:

| Test | What it verifies |
|---|---|
| Config load | Valid YAML loads without errors |
| Config reject | Invalid YAML (bad keys, missing required fields) returns false |
| WireGuard server start | Creates interface, sets listen port, interface is up |
| WireGuard peer add | Adds peer to interface, peer appears in kernel list |
| WireGuard peer remove | Removes peer, peer gone from kernel list |
| WireGuard peer detection | Poll detects new peer handshake, callback fires |
| Session tracker register | New session → SQLite row exists |
| Session tracker connect/disconnect | Mark connected/disconnected → state updated |
| Cert generator wireguard | Generates curve25519 keypair, verify public from private |
| Cert generator openvpn | Issues X509 cert, fingerprint matches |
| IPC notify on connect | Mock IPC client, peer connected → notification sent |
| Graceful shutdown | Stop → interfaces removed, DB closed, IPC disconnected |

Tests requiring root/CAP_NET_ADMIN are SKIP'ed when privileges not available.

**Verification**: All non-privileged tests pass. Privileged tests SKIP or PASS depending on environment.

---

## Phase 3: Registry Manager

### Task 3.1 — Vendored dependency (websocketpp)

websocketpp is fetched via CPM (declared in top-level CMakeLists.txt Task 0.2). It's header-only — no build step needed. Confirm `<websocketpp/config/asio.hpp>` is findable after CPM fetch.

**Verification**: `#include <websocketpp/config/asio.hpp>` compiles.

---

### Task 3.2 — `WsServer` module

**Files**: `registry-manager/ws_server.hpp`, `registry-manager/ws_server.cpp`

Write a WebSocket server using websocketpp with asio transport and TLS:

**Public API**:

```cpp
namespace odc {

struct WsServerConfig {
    uint16_t listen_port{9443};
    std::string listen_address{"0.0.0.0"};
    std::string tls_cert_path;
    std::string tls_key_path;
    std::string url_path{"/ws"};
    int max_connections{1000};
    int idle_timeout_sec{300};
    bool verify_client_cert{false};
    std::string ca_cert_path;       // for client cert verification
};

class WsServer {
public:
    explicit WsServer(const WsServerConfig& config);
    ~WsServer();

    bool start();
    void stop();
    void run();     // Blocking asio event loop
    bool is_running() const;

    // Send to specific client
    bool send(const std::string& connection_id, const std::string& message);
    bool send_json(const std::string& connection_id, const nlohmann::json& data);

    // Broadcast to all connected clients
    void broadcast(const std::string& message);
    void broadcast_json(const nlohmann::json& data);

    // Broadcast to subscribers of a topic
    void publish(const std::string& topic, const nlohmann::json& data);

    // Connection management
    size_t connection_count() const;

    // Callbacks
    using MessageCallback = std::function<void(const std::string& connection_id,
                                                const nlohmann::json& data)>;
    using ConnectCallback = std::function<void(const std::string& connection_id)>;
    using DisconnectCallback = std::function<void(const std::string& connection_id)>;

    void set_on_message(MessageCallback cb);
    void set_on_connect(ConnectCallback cb);
    void set_on_disconnect(DisconnectCallback cb);

private:
    void handle_message(const std::string& connection_id, const std::string& raw);
    void handle_subscribe(const std::string& connection_id, const std::string& topic);
    void handle_unsubscribe(const std::string& connection_id, const std::string& topic);

    // Internal pub/sub: topic → set of connection_ids
    std::unordered_map<std::string, std::unordered_set<std::string>> subscribers_;
    std::mutex subscribers_mutex_;
};

} // namespace odc
```

**Implementation notes**:
- Use `websocketpp::server<websocketpp::config::asio_tls>` as the server endpoint type
- TLS: set `set_tls_init_handler` to load server cert+key from files using `asio::ssl::context`
- Message handler: parse JSON, dispatch based on `type` field:
  - `"subscribe"` → add connection_id to topic subscribers
  - `"unsubscribe"` → remove from topic
  - All other types → forward to `on_message` callback for `RegistryManager` to handle
- `publish()`: send JSON to all subscribers of a topic (under mutex)
- Run asio I/O context in a dedicated thread via `server.run()` in `WsServer::run()`
- Thread safety: websocketpp handlers run on asio strand. Subscriber map protected by mutex.

**Verification**: Build compiles. Runtime tests require a WebSocket client (e.g., `websocat` or custom test client).

---

### Task 3.3 — `WebhookClient` module

**Files**: `registry-manager/webhook_client.hpp`, `registry-manager/webhook_client.cpp`

Write an outbound HTTP POST client for webhook delivery:

**Public API**:

```cpp
namespace odc {

struct WebhookConfig {
    std::string id;
    std::string url;
    std::string secret;             // HMAC-SHA256 signing key
    std::vector<std::string> events; // event types to deliver (empty = all)
    bool enabled{true};
    int max_retries{5};
    int retry_base_sec{1};          // exponential backoff base
    int timeout_sec{10};
};

class WebhookClient {
public:
    WebhookClient();
    ~WebhookClient();

    void add_webhook(const WebhookConfig& config);
    void remove_webhook(const std::string& id);
    void update_webhook(const WebhookConfig& config);
    std::vector<WebhookConfig> list_webhooks() const;
    bool is_enabled(const std::string& id) const;

    // Fire an event to all matching webhooks (async, non-blocking)
    void deliver(const std::string& event_type, const nlohmann::json& payload);

    // Callback for delivery results
    using DeliveryCallback = std::function<void(const std::string& webhook_id,
                                                 bool success,
                                                 int http_status,
                                                 const std::string& error)>;
    void set_on_delivery(DeliveryCallback cb);

private:
    void do_deliver(const WebhookConfig& webhook, const nlohmann::json& payload, int attempt);
    std::string compute_hmac(const std::string& secret, const std::string& body);

    // Thread safety
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, WebhookConfig> webhooks_;
    std::unordered_map<std::string, int> consecutive_failures_;
    DeliveryCallback delivery_callback_;
};

} // namespace odc
```

**Implementation notes**:
- HTTP POST using asio (already a dependency)
- Build JSON body: `{"event":"...","timestamp":"...","payload":{...}}`
- Sign with HMAC-SHA256 computed from `<secret>` and body, set as `X-ODC-Signature` header
- On failure: retry with exponential backoff (1s, 2s, 4s, 8s, 16s, 32s) up to max_retries
- Run delivery in background threads/asio strands — `deliver()` is non-blocking
- After `max_retries` consecutive failures for a webhook, optionally auto-disable it and fire callback

**Verification**: Build compiles.

---

### Task 3.4 — `DataStore` module

**Files**: `registry-manager/data_store.hpp`, `registry-manager/data_store.cpp`

SQLite-backed registry data access layer:

**Public API**:

```cpp
namespace odc {

struct ServiceEntry {
    std::string id;
    std::string name;
    std::string type;
    nlohmann::json metadata;
    std::string registered_at;
    std::string updated_at;
};

struct ClientEntry {
    std::string id;
    std::string name;
    std::string cert_fingerprint;
    bool ws_connected{false};
    std::string last_seen;
};

struct WebhookEntry {
    std::string id;
    std::string url;
    std::string secret;
    std::vector<std::string> events;
    bool enabled{true};
    int consecutive_failures{0};
};

struct EventEntry {
    int64_t id;
    std::string service_id;
    std::string type;
    nlohmann::json payload;
    std::string created_at;
};

class DataStore {
public:
    explicit DataStore(SqliteDb& db);
    bool initialize();  // Create tables + run migrations

    // Services
    bool register_service(const ServiceEntry& service);
    bool unregister_service(const std::string& service_id);
    bool update_service(const std::string& service_id, const nlohmann::json& metadata);
    std::optional<ServiceEntry> get_service(const std::string& service_id);
    std::vector<ServiceEntry> list_services(const std::string& type_filter = "");
    std::vector<ServiceEntry> search_services(const std::string& query);  // Search by name/type

    // Clients
    bool register_client(const ClientEntry& client);
    bool mark_client_connected(const std::string& client_id);
    bool mark_client_disconnected(const std::string& client_id);
    std::optional<ClientEntry> get_client(const std::string& client_id);
    std::vector<ClientEntry> list_clients();

    // Webhooks
    bool add_webhook(const WebhookEntry& webhook);
    bool remove_webhook(const std::string& webhook_id);
    bool update_webhook(const WebhookEntry& webhook);
    std::vector<WebhookEntry> list_webhooks();
    bool record_webhook_failure(const std::string& webhook_id);
    bool reset_webhook_failures(const std::string& webhook_id);

    // Events
    bool log_event(const EventEntry& event);
    std::vector<EventEntry> query_events(const std::string& type_filter = "",
                                          int limit = 100, int64_t since_id = 0);

private:
    SqliteDb& db_;
};

} // namespace odc
```

**Implementation notes**:
- `initialize()`: create `services`, `clients`, `webhooks`, `events_log` tables if not exist
- All methods are simple SQL CRUD via `SqliteDb::prepare()` and `Statement`
- `metadata` stored as JSON text in `metadata_json` column — use `nlohmann::json::dump()` / `nlohmann::json::parse()`
- Thread-safe via `SqliteDb`'s internal mutex

**Verification**: Build compiles.

---

### Task 3.5 — `CertGenerator` module (WS)

**Files**: `registry-manager/ws_cert_generator.hpp`, `registry-manager/ws_cert_generator.cpp`

Wrapper around `CertManager` for WebSocket client TLS certificates:

**Public API**:

```cpp
namespace odc {

class WsCertGenerator {
public:
    explicit WsCertGenerator(CertManager& cert_mgr);

    ClientCert generate_client_cert(const std::string& common_name);
    bool revoke(const std::string& fingerprint, const std::string& crl_path);

private:
    CertManager& cert_mgr_;
};

} // namespace odc
```

**Implementation notes**:
- Thin wrapper — just calls `CertManager::issue_client_cert()` / `CertManager::revoke_cert()`
- Could add rate limiting or audit logging in future

**Verification**: Build compiles.

---

### Task 3.6 — YAML config schema

**File**: `registry-manager/config/registry-manager.yaml`

Write default config template:

```yaml
version: 1

general:
  log_level: info
  pid_file: /run/odc-root/registry-manager.pid

ipc:
  socket_path: /run/odc-root/odc-ipc.sock

database:
  path: /var/lib/odc-root/odc-root.db

websocket:
  listen_port: 9443
  listen_address: 0.0.0.0
  tls_cert_path: /etc/odc-root/ws/server.crt
  tls_key_path: /etc/odc-root/ws/server.key
  url_path: /ws
  max_connections: 1000
  idle_timeout_sec: 300
  verify_client_cert: false
  ca_cert_path: /etc/odc-root/ca/ca.crt

ca:
  cert_path: /etc/odc-root/ca/ca.crt
  key_path: /etc/odc-root/ca/ca.key
  key_bits: 2048
  validity_days: 365

webhooks:
  # - id: example-hook
  #   url: https://hooks.example.com/webhook
  #   secret: your-hmac-secret
  #   events: ["*"]
  #   enabled: true
```

**Verification**: YAML parses.

---

### Task 3.7 — `RegistryManager` service core

**Files**: `registry-manager/registry_manager.hpp`, `registry-manager/registry_manager.cpp`

Orchestrates all registry-manager modules:

**Public API**:

```cpp
namespace odc {

class RegistryManager {
public:
    RegistryManager(const std::string& config_path);
    ~RegistryManager();

    bool initialize();
    bool start();
    void stop();
    void run();     // Blocking event loop

    // Accessors for testing
    DataStore& store();
    WsServer* ws_server();
    WebhookClient* webhook_client();

private:
    void load_config();
    void setup_ipc();               // IpcServer to receive tunnel-manager events
    void setup_websocket();         // WsServer with message handlers
    void setup_webhooks();          // Load webhook configs into WebhookClient

    // IPC message handlers
    nlohmann::json handle_tunnel_client_connected(const nlohmann::json& params);
    nlohmann::json handle_tunnel_client_disconnected(const nlohmann::json& params);
    nlohmann::json handle_tunnel_cert_issued(const nlohmann::json& params);
    nlohmann::json handle_get_status(const nlohmann::json& params);
    nlohmann::json handle_ping(const nlohmann::json& params);

    // WebSocket message handlers
    void on_ws_message(const std::string& connection_id, const nlohmann::json& data);

    // Relay events to WebSocket + webhooks
    void relay_event(const std::string& topic, const std::string& event_type,
                     const nlohmann::json& data);

    // ...
};

} // namespace odc
```

**Implementation notes**:
- `initialize()`: load YAML config, open SQLite, initialize DataStore, initialize CertManager CA, create IpcServer (this service IS the IPC server), create WsServer, create WebhookClient
- `setup_ipc()`: Register IPC method handlers:
  - `tunnel.client.connected` → log event → broadcast to WS topic `tunnel.events` → fire webhooks
  - `tunnel.client.disconnected` → same
  - `tunnel.server.started` → same
  - `tunnel.server.stopped` → same
  - `tunnel.cert.issued` → same
  - `ping` → `{"pong": true}`
- `setup_websocket()`: Register WS message handlers:
  - `"service.register"` → `DataStore::register_service()` → broadcast `"service.registered"` → fire webhooks
  - `"service.unregister"` → `DataStore::unregister_service()` → broadcast `"service.unregistered"` → fire webhooks
  - `"query"` → `DataStore::search_services()` or `DataStore::list_services()` → send result to client
  - `"subscribe"` / `"unsubscribe"` → handled internally by WsServer
- `setup_webhooks()`: Load webhook configs from YAML into `WebhookClient::add_webhook()`
- `relay_event()`: WsServer::publish() + WebhookClient::deliver() + DataStore::log_event()
- `run()`: start IPC server accept loop in one thread, start WsServer asio loop in another thread, wait for shutdown signal

**IPC methods the registry-manager exposes** (server-side):

| Method | Direction | Purpose |
|---|---|---|
| `tunnel.client.connected` | ← tunnel-mgr | Client connected to a VPN server |
| `tunnel.client.disconnected` | ← tunnel-mgr | Client disconnected |
| `tunnel.server.started` | ← tunnel-mgr | VPN server started |
| `tunnel.server.stopped` | ← tunnel-mgr | VPN server stopped |
| `tunnel.cert.issued` | ← tunnel-mgr | Certificate issued to a client |
| `ping` | ← tunnel-mgr or CLI | Health check |

**Verification**: Build compiles.

---

### Task 3.8 — `main.cpp` entry point

**File**: `registry-manager/main.cpp`

Identical pattern to tunnel-manager's `main.cpp` but with `RegistryManager`:

```cpp
#include <CLI/CLI.hpp>
#include "registry_manager.hpp"

int main(int argc, char* argv[]) {
    CLI::App app{"odc-registry-manager — Service registry with WebSocket engine"};

    std::string config_path = "/etc/odc-root/registry-manager.yaml";
    bool daemon_mode = false;

    app.add_option("-c,--config", config_path, "Path to YAML config file");
    app.add_flag("-d,--daemon", daemon_mode, "Run as daemon");

    CLI11_PARSE(app, argc, argv);

    if (daemon_mode) { /* fork, setsid, redirect stdio ... */ }

    odc::RegistryManager mgr(config_path);
    if (!mgr.initialize()) return 1;
    if (!mgr.start()) return 1;
    mgr.run();

    return 0;
}
```

**Verification**: `./build/registry-manager/odc-registry-manager --help` prints usage.

---

### Task 3.9 — CMakeLists.txt for registry-manager

**File**: `registry-manager/CMakeLists.txt`

```cmake
add_executable(odc-registry-manager
    main.cpp
    registry_manager.cpp
    ws_server.cpp
    webhook_client.cpp
    data_store.cpp
    ws_cert_generator.cpp
)

target_link_libraries(odc-registry-manager PRIVATE
    odc-common
    CLI11::CLI11
    websocketpp::websocketpp
)

install(TARGETS odc-registry-manager RUNTIME DESTINATION bin)
install(FILES config/registry-manager.yaml DESTINATION /etc/odc-root)
```

**Verification**: `cmake --build build --target odc-registry-manager` produces binary.

---

### Task 3.10 — systemd unit file

**File**: `configs/systemd/odc-registry-manager.service`

```ini
[Unit]
Description=odc-registry-manager — Service registry with WebSocket engine
After=network-online.target odc-tunnel-manager.service
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/odc-registry-manager --config /etc/odc-root/registry-manager.yaml --daemon
Restart=on-failure
RestartSec=5
User=odc-root
Group=odc-root
RuntimeDirectory=odc-root
RuntimeDirectoryMode=0755
StateDirectory=odc-root

[Install]
WantedBy=multi-user.target
```

---

### Task 3.11 — Integration tests

**File**: `tests/test_registry_manager.cpp`

Core test scenarios:

| Test | What it verifies |
|---|---|
| Config load | Valid YAML loads without errors |
| Config reject | Invalid YAML returns false |
| DataStore register service | Register service → query returns it |
| DataStore unregister | Unregister → query returns empty |
| DataStore search | Register 3 services with different types, filter by type returns subset |
| DataStore webhooks | Add webhook, query returns it, remove removes it |
| DataStore events | Log event, query returns it, filter by type |
| WsServer start/stop | Server starts on configured port, stops cleanly |
| WsServer TLS | Clients require TLS to connect |
| WsServer pub/sub | Client subscribes to topic, publish sends to that client |
| WsServer broadcast | Broadcast sends to all connected clients |
| WebhookClient delivery | Mock HTTP endpoint receives POST with correct body + HMAC |
| WebhookClient retry | HTTP endpoint returns 500 → retries with backoff |
| WebhookClient disable | N consecutive failures → webhook auto-disabled |
| IPC server accept | IPC server accepts connection from test IpcClient |
| IPC tunnel event handling | Simulated tunnel-manager notification → event logged to DB → WS broadcast → webhook fired |
| Cert generator WS | Issue client cert, verify fingerprint |

**Verification**: All tests pass.

---

## Phase 4: Cross-Service Integration

### Task 4.1 — End-to-end IPC integration test

**File**: `tests/test_ipc_integration.cpp`

**Test scenario** (both services running):

```
1. Start odc-registry-manager (IPC server)
2. Start odc-tunnel-manager (IPC client → connects to registry)
3. WireGuard server starts → tunnel-manager notifies "tunnel.server.started"
4. Registry logs event, broadcasts to WS topic, fires webhook
5. Client connects to WireGuard server → "tunnel.client.connected" with IP
6. Registry: event logged → broadcasted to WS subscribers → webhook fired
7. WebSocket client connects to registry, subscribes to "tunnel.events"
8. WS client receives "tunnel.client.connected" event in real time
9. VPN client disconnects → "tunnel.client.disconnected"
10. WS client receives disconnect event
11. Graceful shutdown: both services stop, no crashes, DB intact
```

**Verification**: All steps pass in sequence.

---

### Task 4.2 — CLI tool (`odc-cli`)

**File**: `tools/odc-cli/main.cpp`

Write a CLI tool that connects to the registry-manager's IPC socket to query and manage both services interactively. Use CLI11 for argument parsing.

**Commands**:

| Command | IPC Method | Description |
|---|---|---|
| `odc-cli status` | `ping` | Check if registry-manager is running |
| `odc-cli services list` | Custom IPC handler | List all registered services |
| `odc-cli services register <id> <name> <type>` | Custom IPC handler | Register a new service |
| `odc-cli services unregister <id>` | Custom IPC handler | Unregister a service |
| `odc-cli clients list` | Custom IPC handler | List all registry clients |
| `odc-cli clients cert <name>` | Custom IPC handler | Generate TLS client certificate |
| `odc-cli webhooks list` | Custom IPC handler | List configured webhooks |
| `odc-cli webhooks test <id>` | Custom IPC handler | Send test event to a webhook |
| `odc-cli tunnel status` | Forwarded to tunnel-mgr via registry | Get VPN server/client status |
| `odc-cli tunnel clients` | Forwarded to tunnel-mgr via registry | List connected VPN clients |
| `odc-cli tunnel add-client <id> <name> <type>` | Forwarded to tunnel-mgr via registry | Generate creds for new VPN client |
| `odc-cli tunnel remove-client <id>` | Forwarded to tunnel-mgr via registry | Revoke and disconnect a client |

**Implementation notes**:
- `odc-cli` is an IPC client — it connects to the registry-manager's Unix socket
- Tunnel-manager commands are forwarded: CLI → registry-manager (relays via its IPC client) → tunnel-manager (responds) → registry-manager (relays) → CLI
- Output is formatted JSON by default, with `--pretty` flag for human-readable

**CMakeLists.txt**: `tools/odc-cli/CMakeLists.txt`

```cmake
add_executable(odc-cli main.cpp)
target_link_libraries(odc-cli PRIVATE odc-common CLI11::CLI11)
install(TARGETS odc-cli RUNTIME DESTINATION bin)
```

**Verification**: `./build/tools/odc-cli/odc-cli --help` prints usage.

---

### Task 4.3 — Documentation

**File**: `docs/ARCHITECTURE.md`

Write a comprehensive architecture document covering:
- System overview with Mermaid diagrams
- Component catalog (every module in all three binaries)
- IPC protocol specification
- WebSocket protocol specification
- Database schema
- YAML config schema
- Threading model
- Security model (certificate management, TLS, HMAC)

**File**: `docs/API.md`

Write API reference documentation covering:
- All IPC methods with request/response schemas
- All WebSocket message types
- All CLI commands
- Error codes

---

## Build & Test Reference

### Build all targets

```bash
cd odc-root
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

### Run unit tests

```bash
cmake --build build --target test_common
./build/tests/test_common

cmake --build build --target test_tunnel_manager
./build/tests/test_tunnel_manager

cmake --build build --target test_registry_manager
./build/tests/test_registry_manager

cmake --build build --target test_ipc_integration
./build/tests/test_ipc_integration
```

### Run with sudo (privileged tests)

```bash
sudo ./build/tests/test_tunnel_manager
sudo ./build/tunnel-manager/odc-tunnel-manager --config tunnel-manager/config/tunnel-manager.yaml --daemon
sudo ./build/tests/test_ipc_integration
```

### Install

```bash
sudo cmake --install build
sudo systemctl daemon-reload
sudo systemctl start odc-tunnel-manager odc-registry-manager
sudo systemctl status odc-tunnel-manager odc-registry-manager
```

### Quick smoke test (both services running)

```bash
# Check registry
./build/tools/odc-cli/odc-cli status

# Check tunnel manager status
./build/tools/odc-cli/odc-cli tunnel status

# Register a service via WebSocket (requires websocat or similar)
echo '{"type":"service.register","service":{"id":"test-1","name":"Test Service","type":"app"}}' \
  | websocat wss://localhost:9443/ws --header "Authorization: Bearer ..."
```

---

## Task Summary

| Phase | Tasks | Est. File Count |
|---|---|---|
| Phase 0 — Scaffolding | 3 | ~5 files |
| Phase 1 — Common Library | 7 | ~14 files |
| Phase 2 — Tunnel Manager | 11 | ~18 files |
| Phase 3 — Registry Manager | 11 | ~18 files |
| Phase 4 — Integration | 3 | ~5 files |
| **Total** | **35 tasks** | **~60 files** |
