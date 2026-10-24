# odc-root: Real-Time Integration Test Suite — Implementation Plan

**Goal**: A comprehensive integration test suite that validates the entire odc-root system (tunnel-manager, registry-manager, odc-hook, odc-cli) running with real networking inside Docker containers. Tests cover mTLS handshakes, VPN peer lifecycle, event propagation flow, CLI-driven operations, service discovery, webhook delivery, and cross-platform binary validation for Linux, Windows, and macOS.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Phase A: Prerequisites — CLI Expansion](#phase-a-prerequisites--cli-expansion)
   - [Task A.1 — `cert` subcommands in odc-cli](#task-a1--cert-subcommands-in-odc-cli)
   - [Task A.2 — `tunnel` subcommands in odc-cli + tunnel‑manager IPC server](#task-a2--tunnel-subcommands-in-odc-cli--tunnel-manager-ipc-server)
   - [Task A.3 — `events` subcommand in odc-cli](#task-a3--events-subcommand-in-odc-cli)
   - [Task A.4 — `hook` subcommand in odc-cli](#task-a4--hook-subcommand-in-odc-cli)
3. [Phase B: Docker Test Infrastructure](#phase-b-docker-test-infrastructure)
   - [Task B.1 — Linux runtime Docker image](#task-b1--linux-runtime-docker-image)
   - [Task B.2 — Docker Compose with networking](#task-b2--docker-compose-with-networking)
   - [Task B.3 — Service startup entrypoint](#task-b3--service-startup-entrypoint)
   - [Task B.4 — Bash test framework](#task-b4--bash-test-framework)
   - [Task B.5 — Windows Wine Docker image](#task-b5--windows-wine-docker-image)
   - [Task B.6 — macOS cross‑compile verification Docker image](#task-b6--macos-cross-compile-verification-docker-image)
4. [Phase C: Linux Integration Test Suite](#phase-c-linux-integration-test-suite)
   - [C.1 — Certificate Lifecycle Tests](#c1--certificate-lifecycle-tests)
   - [C.2 — mTLS Connection Scenarios](#c2--mtls-connection-scenarios)
   - [C.3 — VPN Peer Lifecycle (WireGuard)](#c3--vpn-peer-lifecycle-wireguard)
   - [C.4 — Event Propagation Flow](#c4--event-propagation-flow)
   - [C.5 — Webhook Delivery Tests](#c5--webhook-delivery-tests)
   - [C.6 — Service Discovery & Registry Tests](#c6--service-discovery--registry-tests)
   - [C.7 — CLI-Driven Operations](#c7--cli-driven-operations)
   - [C.8 — Resiliency & Recovery](#c8--resiliency--recovery)
   - [C.9 — Concurrent Operations Stress](#c9--concurrent-operations-stress)
5. [Phase D: Windows Wine‑Based Smoke Tests](#phase-d-windows-wine-based-smoke-tests)
   - [D.1 — Wine smoke test suite](#d1--wine-smoke-test-suite)
   - [D.2 — Windows header audit](#d2--windows-header-audit)
6. [Phase E: macOS Cross‑Compile Validation](#phase-e-macos-cross-compile-validation)
   - [E.1 — macOS binary verification](#e1--macos-binary-verification)
   - [E.2 — macOS header audit](#e2--macos-header-audit)
7. [Phase F: Makefile & CI Orchestration](#phase-f-makefile--ci-orchestration)
   - [F.1 — Top‑level Makefile for test orchestration](#f1--top-level-makefile-for-test-orchestration)
   - [F.2 — GitHub Actions CI workflow](#f2--github-actions-ci-workflow)
8. [File Manifest](#file-manifest)
9. [Task Execution Order & Dependencies](#task-execution-order--dependencies)
10. [Estimated Effort](#estimated-effort)

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────────────────┐
│  Docker Container: odc-root-test                                         │
│  (CAP_NET_ADMIN, /dev/net/tun, bridge network 10.100.0.0/24)            │
│                                                                          │
│  ┌──────────────────────┐   IPC (Unix)     ┌──────────────────────────┐ │
│  │  odc-tunnel-manager   │◄───────────────►│  odc-registry-manager     │ │
│  │                       │                 │                          │ │
│  │  - WireGuardServer    │                 │  - WsServer (mTLS :9443) │ │
│  │    • wg0: 10.0.0.1/24│                 │  - WebhookClient         │ │
│  │    • Netlink RTNL     │                 │  - IpcServer             │ │
│  │  - SessionTracker     │                 │  - IpcClient (tunnel)    │ │
│  │  - IpcClient → reg    │                 │  - DataStore (SQLite)    │ │
│  │  - IpcServer ← CLI    │                 │  - CertRevocationList    │ │
│  └──────────────────────┘                 └──────────┬───────────────┘ │
│                                                       │                 │
│                                         WSS (mTLS)    │                 │
│                                         10.100.0.x:9443                │
│                                                       │                 │
│                                          ┌────────────▼───────────────┐ │
│                                          │  odc-hook                   │ │
│                                          │  - WsClient (mTLS)          │ │
│                                          │  - EventBus                 │ │
│                                          │  - ThreadPool               │ │
│                                          │  - HookService              │ │
│                                          └────────────────────────────┘ │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │  Test Orchestrator (bash)                                          │ │
│  │                                                                    │ │
│  │  odc-cli cert generate-ca   →   CA + server + client certs        │ │
│  │  odc-cli cert issue         →   Additional certs on demand        │ │
│  │  odc-cli tunnel peers add   →   WireGuard peer operations         │ │
│  │  odc-cli services register  →   Trigger service events            │ │
│  │  odc-cli events watch       →   Monitor real-time event stream    │ │
│  │  odc-cli status             →   Health checks                     │ │
│  │                                                                    │ │
│  │  Verifies: cert fingerprints, event payloads, log output,          │ │
│  │  process state, WireGuard kernel state, SQLite DB content,         │ │
│  │  IPC socket connectivity, WebSocket subscription delivery          │ │
│  └────────────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────┐
│  Docker Container: odc-root-test-wine (bridge network)                   │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  Wine runtime                                                    │    │
│  │  - wine odc-cli.exe cert generate-ca ...                         │    │
│  │  - wine odc-cli.exe cert issue ...                               │    │
│  │  - wine odc-hook.exe --config wine-hook.yaml                     │    │
│  │                 │                                                │    │
│  │                 ▼  WSS (mTLS over Docker bridge)                 │    │
│  │            Linux registry-manager:9443                           │    │
│  │  Verifies: Windows PE32+ binary functions with real TLS          │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────────────┘
```

**Key design decisions**:

| Decision | Choice | Rationale |
|---|---|---|
| Container topology | Single container for all services | Matches real deployment (colocated on one host), simplifies IPC via Unix sockets |
| WireGuard peer testing | Second WG interface (`wg-test`) on same host with matching keys | Simulates remote peer handshake without external machines |
| CLI expansion | Extend odc-cli with `cert`, `tunnel`, `events`, `hook` subcommands | Enables "artifacted CLI operation" testing as required |
| Tunnel-manager commands | Add second IPC socket (`tunnel-ipc.sock`) for inbound CLI commands | Keeps notification path (→ registry) separate from command path (← CLI) |
| Windows testing | Wine runtime on Linux Docker | Executes real PE32+ binaries, verifies cross-compiled code works with real TLS/WS |
| macOS testing | Cross-compile + binary verification only | No macOS runtime exists on Linux; CI uses GitHub Actions macOS runner |
| Test framework | bash (matching existing `tests/test_vpn_monitor_extender.sh` pattern) | Zero additional dependencies, runs in bare Docker container |
| Reporting | Colored pass/fail/skip counters, exit code 0 on all-pass | Compatible with CI/CD pipelines |

---

## Phase A: Prerequisites — CLI Expansion

**Current state**: `odc-cli` supports only `status`, `services list`, `services register`, and `services unregister`. The tunnel-manager is a pure IPC client (cannot receive commands). Both must be expanded to enable CLI-driven integration testing.

### Task A.1 — `cert` subcommands in odc-cli

**File**: `tools/odc-cli/main.cpp`

Add a `cert` CLI11 subcommand group that operates on X.509 certificates directly (linked into the CLI binary via `odc-common`'s `CertManager`). All operations are local (no IPC to registry-manager needed).

| Subcommand | Parameters | Implementation |
|---|---|---|
| `cert generate-ca` | `--cert <path>` (required), `--key <path>` (required), `--org <str>` (default `"odc-root"`), `--cn <str>` (default `"odc-root CA"`), `--country <str>` (default `"XX"`), `--bits <int>` (default 2048), `--days <int>` (default 365) | Constructs `CaConfig`, calls `CertManager::create_ca()`, writes PEM cert and key to specified paths. Prints JSON `{"status":"ok","cert_path":"...","key_path":"..."}` on success. |
| `cert issue` | `--ca-cert <path>` (required), `--ca-key <path>` (required), `--cn <name>` (required), `--cert-out <path>` (required), `--key-out <path>` (required), `--bits <int>` (default 2048), `--days <int>` (default 365) | Calls `CertManager::load_ca()` then `issue_client_cert()`, writes PEM cert and key to output paths. Prints JSON with `common_name`, `fingerprint`, `serial`, `not_before`, `not_after`. |
| `cert verify` | `--ca-cert <path>` (required), `--cert <path>` (required) | Calls `CertManager::load_ca()` then `verify_cert()`. Exit 0 on valid, exit 1 on failure. Prints `{"valid": true/false}`. |
| `cert fingerprint` | `--cert <path>` (required) | Reads PEM file, parses with `PEM_read_bio_X509()`, computes SHA-256 fingerprint via `X509_digest()`, prints colon-separated hex. Exit 0 on success. |
| `cert revoke` | `--ca-cert <path>` (required), `--ca-key <path>` (required), `--fingerprint <hex>` (required), `--crl-out <path>` (required) | Calls `CertManager::load_ca()` then `revoke_cert()`, writes CRL PEM to output. |
| `cert keypair-wg` | `--key-out <path>` | Generates Curve25519 keypair via `CertManager::generate_curve25519_private_key()` / `generate_curve25519_public_key()`, writes to file. |

**Verification**:
```
odc-cli cert generate-ca --cert /tmp/ca.crt --key /tmp/ca.key --cn "Test Root CA"
# → {"status":"ok","cert_path":"/tmp/ca.crt","key_path":"/tmp/ca.key"}

openssl verify -CAfile /tmp/ca.crt /tmp/ca.crt
# → /tmp/ca.crt: OK

odc-cli cert issue --ca-cert /tmp/ca.crt --ca-key /tmp/ca.key --cn "test-client" \
    --cert-out /tmp/client.crt --key-out /tmp/client.key
# → {"common_name":"test-client","fingerprint":"AA:BB:CC:...","serial":"1",...}

openssl verify -CAfile /tmp/ca.crt /tmp/client.crt
# → /tmp/client.crt: OK
```

### Task A.2 — `tunnel` subcommands in odc-cli + tunnel‑manager IPC server

**Files**: `tunnel-manager/tunnel_manager.hpp`, `tunnel-manager/tunnel_manager.cpp`, `tools/odc-cli/main.cpp`

**Problem**: The tunnel-manager currently creates only an `IpcClient` (outbound notifications to registry). It cannot receive commands from the CLI. This task adds a second `IpcServer` on a dedicated socket for inbound commands.

**Step 1 — Add IpcServer to TunnelManager** (`tunnel-manager/tunnel_manager.hpp`):
- Add `std::unique_ptr<IpcServer> ipc_server_` member
- Add `std::string tunnel_ipc_socket_path_` member (configurable via YAML, default `/run/odc-root/tunnel-ipc.sock`)
- Add private method `void setup_tunnel_ipc()` that creates the server and registers method handlers

**Step 2 — Register tunnel command handlers** (`tunnel-manager/tunnel_manager.cpp`):

| IPC Method | Handler | Parameters | Response |
|---|---|---|---|
| `tunnel.status` | `handle_status` | none | `{"ok":true,"servers":[{"id":"wg-main","type":"wireguard","interface":"wg0","subnet":"10.0.0.0/24","peer_count":3}],"total_peers":3}` |
| `tunnel.peers.list` | `handle_peers_list` | `{"server_id":"wg-main"}` (optional) | `{"peers":[{"public_key":"...","allowed_ip":"10.0.0.2","last_handshake_sec":15,"endpoint":"..."}]}` |
| `tunnel.peers.add` | `handle_peers_add` | `{"public_key":"<base64>","allowed_ip":"<ip>"}` (optional IP, auto-allocated if omitted) | `{"ok":true,"assigned_ip":"10.0.0.2"}` or `{"ok":false,"error":"..."}` |
| `tunnel.peers.remove` | `handle_peers_remove` | `{"public_key":"<base64>"}` | `{"ok":true}` or `{"ok":false,"error":"..."}` |
| `tunnel.health` | `handle_health` | none | `{"ok":true,"servers":[{"id":"wg-main","running":true,"peers_connected":2,"peers_total":5}]}` |

**Step 3 — Start IPC server in TunnelManager::start()**:
- After creating WireGuard/OpenVPN servers, call `ipc_server_->start()` to begin listening on `tunnel_ipc_socket_path_`
- Add `ipc_server_->run()` call after the main poll loop (runs in a dedicated thread or via a tick function)

**Step 4 — Add `tunnel` subcommands to odc-cli** (`tools/odc-cli/main.cpp`):

| Subcommand | IPC method | Parameters |
|---|---|---|
| `tunnel status` | `tunnel.status` | `-s/--socket <path>` (default `/run/odc-root/tunnel-ipc.sock`) |
| `tunnel peers list` | `tunnel.peers.list` | `-s/--socket <path>`, `--server <id>` (optional) |
| `tunnel peers add` | `tunnel.peers.add` | `-s/--socket <path>`, `--public-key <key>` (required), `--ip <ip>` (optional) |
| `tunnel peers remove` | `tunnel.peers.remove` | `-s/--socket <path>`, `--public-key <key>` (required) |
| `tunnel health` | `tunnel.health` | `-s/--socket <path>` |

**Step 5 — Update tunnel-manager YAML config** (`tunnel-manager/config/tunnel-manager.yaml`):
- Add `tunnel_ipc_socket_path: /run/odc-root/tunnel-ipc.sock` under `ipc:` section
- Add `tunnel_ipc_socket_path` parsing in `TunnelManager::load_config()`

**Verification**:
```
odc-cli tunnel status
# → {"ok":true,"servers":[{"id":"wg-main","type":"wireguard",...}]}

odc-cli tunnel peers add --public-key "base64keyhere"
# → {"ok":true,"assigned_ip":"10.0.0.2"}

wg show wg0
# → peer: base64keyhere  allowed ips: 10.0.0.2/32

odc-cli tunnel peers list
# → {"peers":[{"public_key":"base64keyhere","allowed_ip":"10.0.0.2","last_handshake_sec":0}]}

odc-cli tunnel peers remove --public-key "base64keyhere"
# → {"ok":true}
```

### Task A.3 — `events` subcommand in odc-cli

**File**: `tools/odc-cli/main.cpp`

Add an `events watch` command that subscribes to WebSocket events from the registry-manager and streams them to stdout. This uses `WsClient` directly (not IPC), enabling real-time event monitoring during tests.

| Subcommand | Parameters | Description |
|---|---|---|
| `events watch` | `--url <wss://host:port/ws>` (required), `--ca-cert <path>` (required for mTLS), `--client-cert <path>` (required for mTLS), `--client-key <path>` (required for mTLS), `--topic <topic>` (repeatable, default `"*"` subscribes to all), `--timeout <sec>` (default 30, 0 = wait forever), `--format <json\|text>` (default `json`) | Connects to registry WebSocket via mTLS, sends `subscribe` frames for each `--topic`, prints received events line-by-line (one JSON object per line), exits after timeout or SIGINT. |

**Implementation details**:
- Uses `asio::io_context` with `WsClient` (same class used by odc-hook)
- `WsClient::setup_ssl_context()` handles mTLS client cert + CA cert loading
- On connect: iterates `--topic` values, sends `{"type":"subscribe","topic":"<topic>"}` for each
- On message: prints `event.dump()` to stdout, flushes immediately for pipe consumers
- On timeout: cancels all async operations, exits with code 0
- On SIGINT: clean disconnect, exits with code 0

**Verification**:
```
# Terminal 1: Start event watcher
odc-cli events watch --url wss://127.0.0.1:9443/ws \
    --ca-cert /etc/odc-root/ca/ca.crt \
    --client-cert /etc/odc-root/hook/client.crt \
    --client-key /etc/odc-root/hook/client.key \
    --topic tunnel.events --timeout 10 > /tmp/events.log &

# Terminal 2: Trigger an event
odc-cli services register --id s1 --name test --type drone

# Terminal 1 output contains:
# {"type":"event","topic":"services","data":{"event":"service.registered",...}}

# Test pipe integration:
odc-cli events watch ... --timeout 5 | grep -c "tunnel.client.connected"
# → returns count of tunnel events during the 5-second window
```

### Task A.4 — `hook` subcommand in odc-cli

**File**: `tools/odc-cli/main.cpp`

Add a `hook status` command that queries the hook service for its current state.

| Subcommand | Parameters | Description |
|---|---|---|
| `hook status` | `--socket <path>` (default `/run/odc-root/hook.sock`) | Connects to a lightweight IPC server in odc-hook (new), returns connection state and event statistics |

**Implied change**: odc-hook's `HookService` must expose a small Unix socket IPC server (reuse `IpcServer` from `odc-common`) with methods:

| Method | Response |
|---|---|
| `hook.status` | `{"connected":true/false,"server":"host:port","events_received":N,"events_processed":N,"thread_pool_active":M}` |
| `hook.ping` | `{"pong":true}` |

This is a minimal addition to `hook_service.cpp`: create `IpcServer`, register two handlers, start it in `HookService::start()`.

**Verification**:
```
odc-cli hook status
# → {"connected":true,"server":"127.0.0.1:9443","events_received":142,"events_processed":142,"thread_pool_active":0}
```

---

## Phase B: Docker Test Infrastructure

### Task B.1 — Linux runtime Docker image

**New file**: `deploy/test/Dockerfile`

A single container image that builds all odc-root components and provides a complete test environment.

```dockerfile
FROM ubuntu:22.04

# System dependencies + WireGuard userspace tools + test utilities
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git ca-certificates \
    g++-12 gcc-12 libssl-dev libsqlite3-dev \
    wireguard-tools iproute2 net-tools curl jq xxd \
    && rm -rf /var/lib/apt/lists/*

# Build all components from source (CPM fetches dependencies)
COPY . /src/odc-root
WORKDIR /src/odc-root
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -GNinja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_TUNNEL_MANAGER=ON \
    -DBUILD_REGISTRY_MANAGER=ON \
    -DBUILD_ODC_CLI=ON \
    -DBUILD_ODC_HOOK=ON \
    -DBUILD_TESTING=ON \
    && cmake --build build -j$(nproc) \
    && cmake --install build --prefix /usr

# Runtime directories
RUN mkdir -p /run/odc-root /var/lib/odc-root /etc/odc-root/{ca,hook,ws,wg,ovpn}

# Test suite
COPY deploy/test/entrypoint.sh /entrypoint.sh
COPY deploy/test/test_suite.sh /test_suite.sh
COPY deploy/test/helpers.sh /helpers.sh
COPY deploy/test/configs/ /etc/odc-root/
RUN chmod +x /entrypoint.sh /test_suite.sh /helpers.sh

ENTRYPOINT ["/entrypoint.sh"]
```

**Config templates** (`deploy/test/configs/`):

- `tunnel-manager.yaml` — pre-configured with a WireGuard server on `wg0`, subnet `10.0.0.0/24`, CA paths pointing to `/etc/odc-root/ca/`
- `registry-manager.yaml` — pre-configured with mTLS on port 9443, IPC socket path, CA paths
- `odc-hook.yaml` — pre-configured to connect to `127.0.0.1:9443` with mTLS certs

### Task B.2 — Docker Compose with networking

**New file**: `deploy/test/docker-compose.yml`

```yaml
version: "3.9"

services:
  odc-test:
    build:
      context: ../..
      dockerfile: deploy/test/Dockerfile
    container_name: odc-root-test
    hostname: odc-root-test
    cap_add:
      - NET_ADMIN
      - NET_RAW
      - SYS_ADMIN      # Required for WireGuard kernel module operations
    devices:
      - /dev/net/tun   # WireGuard TUN device
    sysctls:
      - net.ipv4.ip_forward=1
      - net.ipv6.conf.all.forwarding=1
    volumes:
      - ./output:/output
    networks:
      - odc-test-net

  odc-test-wine:
    build:
      context: ../..
      dockerfile: deploy/test/wine/Dockerfile
    container_name: odc-root-test-wine
    hostname: odc-root-test-wine
    depends_on:
      - odc-test
    networks:
      - odc-test-net
    environment:
      - REGISTRY_HOST=odc-test
      - REGISTRY_PORT=9443
    volumes:
      - ./output:/output
      - ./certs:/certs:ro   # Shared certs from Linux container

networks:
  odc-test-net:
    driver: bridge
    ipam:
      config:
        - subnet: 10.100.0.0/24
```

### Task B.3 — Service startup entrypoint

**New file**: `deploy/test/entrypoint.sh`

Orchestrates service startup in the correct order with proper wait-for-readiness checks:

```
1. Create runtime directories (mkdir -p /run/odc-root /var/lib/odc-root)
2. Generate CA + certs using odc-cli cert generate-ca + cert issue
   → CA: /etc/odc-root/ca/ca.crt + ca.key
   → Server: /etc/odc-root/ws/server.crt + server.key (CN=odc-registry)
   → Client: /etc/odc-root/hook/client.crt + client.key (CN=odc-hook)
3. Generate WireGuard server keypair for tunnel-manager config
4. Wait for WireGuard kernel module to be available
5. Start odc-tunnel-manager --config /etc/odc-root/tunnel-manager.yaml --daemon
6. Wait for: (a) wg0 interface exists, (b) tunnel-ipc.sock appears
7. Start odc-registry-manager --config /etc/odc-root/registry-manager.yaml --daemon
8. Wait for TCP port 9443 to be listening (curl --insecure check)
9. Start odc-hook --config /etc/odc-root/odc-hook.yaml --daemon
10. Wait for odc-hook to connect (check hook IPC socket or registry logs)
11. Run /test_suite.sh with all positional arguments forwarded
12. Capture exit code from test_suite.sh
13. Stop services (SIGTERM), wait for graceful exit
14. Collect logs from /var/log/odc-root/ into /output/logs/
15. Exit with test_suite.sh's exit code
```

### Task B.4 — Bash test framework

**New files**: `deploy/test/helpers.sh`, `deploy/test/test_suite.sh`

`helpers.sh` provides a reusable assertion framework matching the project's existing pattern from `odc-onboard/tests/test_vpn_monitor_extender.sh`:

```bash
# Global counters
PASS=0; FAIL=0; SKIP=0
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'; NC='\033[0m'

pass() { echo -e "  ${GREEN}PASS${NC}: $1"; ((PASS++)); }
fail() { echo -e "  ${RED}FAIL${NC}: $1"; ((FAIL++)); }
skip() { echo -e "  ${YELLOW}SKIP${NC}: $1"; ((SKIP++)); }

test_section() { echo; echo "--- $1 ---"; }

# Assert helpers
assert() {
    if eval "$1"; then pass "$2"; else fail "$2 (expected: true, got: false)"; fi
}

assert_eq() {
    local actual; actual=$(eval "$1")
    if [ "$actual" = "$2" ]; then pass "$3";
    else fail "$3 (expected: '$2', got: '$actual')"; fi
}

assert_contains() {
    if echo "$2" | grep -qF "$1"; then pass "$3";
    else fail "$3 (expected to contain: '$1')"; fi
}

assert_file_exists()  { if [ -f "$1" ]; then pass "$2"; else fail "$2 (file not found: $1)"; fi; }
assert_port_open()    { if nc -z 127.0.0.1 "$1" 2>/dev/null; then pass "$2"; else fail "$2 (port $1 not open)"; fi; }
assert_process_running() { if pgrep -f "$1" >/dev/null; then pass "$2"; else fail "$2 (process not running: $1)"; fi; }
assert_json_field()   { local v; v=$(echo "$2" | jq -r "$1" 2>/dev/null); if [ "$v" = "$3" ]; then pass "$4"; else fail "$4 (field $1: expected '$3', got '$v')"; fi; }

print_summary() {
    echo; echo "==============================================="
    echo "Results: ${GREEN}PASS: $PASS${NC}  ${RED}FAIL: $FAIL${NC}  ${YELLOW}SKIP: $SKIP${NC}"
    if [ $FAIL -gt 0 ]; then echo "TESTS FAILED"; return 1; fi
    echo "ALL TESTS PASSED"; return 0
}
```

### Task B.5 — Windows Wine Docker image

**New file**: `deploy/test/wine/Dockerfile`

```dockerfile
FROM ubuntu:22.04

RUN dpkg --add-architecture i386 && apt-get update && DEBIAN_FRONTEND=noninteractive \
    apt-get install -y --no-install-recommends \
    g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64 mingw-w64-tools \
    cmake ninja-build git ca-certificates perl make \
    wine64 wine32 wine libwine libwine:i386 \
    curl jq grep \
    && rm -rf /var/lib/apt/lists/*

ENV CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++
ENV WINEDEBUG=-all WINEPREFIX=/root/.wine

# Cross-compile OpenSSL for Windows (static)
RUN cd /tmp && git clone --depth 1 --branch openssl-3.2.1 https://github.com/openssl/openssl.git && \
    cd openssl && \
    ./Configure mingw64 no-shared no-tests no-docs no-apps \
        --prefix=/opt/cross/x86_64-w64-mingw32 \
        --cross-compile-prefix=x86_64-w64-mingw32- CFLAGS="-O2" && \
    make -j$(nproc) && make install_sw && rm -rf /tmp/openssl

ENV OPENSSL_ROOT_DIR=/opt/cross/x86_64-w64-mingw32
ENV CMAKE_PREFIX_PATH=/opt/cross/x86_64-w64-mingw32

COPY . /src/odc-root
WORKDIR /src/odc-root

# Cross-compile odc-hook + odc-cli for Windows
RUN cmake -B build-win -DCMAKE_BUILD_TYPE=Release -GNinja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_TOOLCHAIN_FILE=deploy/windows/toolchain.cmake \
    -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR}" \
    -DCMAKE_FIND_ROOT_PATH="/opt/cross/x86_64-w64-mingw32;/usr/x86_64-w64-mingw32" \
    -DBUILD_TUNNEL_MANAGER=OFF -DBUILD_REGISTRY_MANAGER=OFF \
    -DBUILD_ODC_CLI=ON -DBUILD_ODC_HOOK=ON -DBUILD_TESTING=ON \
    && cmake --build build-win --target odc-hook odc-cli test_event_bus test_thread_pool test_ws_client -j$(nproc) \
    && x86_64-w64-mingw32-strip build-win/odc-hook/odc-hook.exe \
    && x86_64-w64-mingw32-strip build-win/tools/odc-cli/odc-cli.exe

COPY deploy/test/wine/smoke_test.sh /smoke_test.sh
RUN chmod +x /smoke_test.sh
ENTRYPOINT ["/smoke_test.sh"]
```

### Task B.6 — macOS cross‑compile verification Docker image

**Existing file to extend**: `deploy/macos/Dockerfile`

The existing macOS cross-compile Dockerfile already builds `odc-hook`. Extend it to also cross-compile `odc-cli` and test targets, and add validation steps:

```
# After cmake --build:
RUN file build/odc-hook/odc-hook | grep "Mach-O 64-bit executable x86_64" || \
    (echo "ERROR: Expected Mach-O binary" && false)

RUN file build/tools/odc-cli/odc-cli | grep "Mach-O 64-bit executable x86_64" || \
    (echo "ERROR: Expected Mach-O binary" && false)

# Verify test targets link (can't execute on Linux)
RUN cmake --build build --target test_event_bus test_thread_pool test_ws_client -j$(nproc)

# Header audit (same as Windows)
RUN (! grep -rn --include='*.hpp' --include='*.cpp' \
    -E '<sys/socket\.h>|<sys/un\.h>|<sys/capability\.h>|<linux/' \
    odc-hook/ common/include/ common/src/ | grep -v 'platform_linux') \
    || (echo "ERROR: POSIX-only headers found in cross-platform code" && false)
```

---

## Phase C: Linux Integration Test Suite

All tests run inside the Docker container with all services running, real network interfaces, real TLS connections, and real WireGuard kernel operations. Each test section is a bash function in `test_suite.sh`.

### C.1 — Certificate Lifecycle Tests

Verify the CA/cert generation pipeline and CRL operations using `odc-cli cert`.

| # | Test | Steps |
|---|---|---|
| C1.1 | `generate_ca_creates_valid_cert` | Run `odc-cli cert generate-ca`, verify file exists, run `openssl verify -CAfile ca.crt ca.crt`, check PEM contains `BEGIN CERTIFICATE` |
| C1.2 | `ca_cert_has_correct_extensions` | Run `openssl x509 -in ca.crt -noout -text`, verify `CA:TRUE`, `Certificate Sign, CRL Sign` in key usage |
| C1.3 | `issue_client_cert_signed_by_ca` | Run `odc-cli cert issue` with CA, verify `openssl verify -CAfile ca.crt client.crt` returns OK |
| C1.4 | `client_cert_has_correct_extensions` | Verify `CA:FALSE`, `Digital Signature, Key Encipherment`, `TLS Web Client Authentication, TLS Web Server Authentication` |
| C1.5 | `cert_fingerprint_consistent` | Run `odc-cli cert fingerprint`, verify output is colon-separated hex matching `openssl x509 -fingerprint -sha256` output |
| C1.6 | `issue_cert_increments_serial` | Issue two certs, verify serial numbers are different (2, 4, 6, ...) |
| C1.7 | `verify_valid_cert_succeeds` | Run `odc-cli cert verify --ca-cert ca.crt --cert client.crt`, exit 0 |
| C1.8 | `verify_wrong_ca_fails` | Run `odc-cli cert verify` with different CA cert, exit 1 |
| C1.9 | `verify_self_signed_fails` | Run `odc-cli cert verify` with self-signed cert passed as both CA and cert, exit 1 |
| C1.10 | `revoke_cert_produces_crl` | Run `odc-cli cert revoke`, verify CRL file exists, `openssl crl -in crl.pem -noout -text` shows revoked serial |
| C1.11 | `revoked_cert_fails_verify` | After revocation, `openssl verify -CAfile ca.crt -CRLfile crl.pem client.crt` returns failure |
| C1.12 | `wg_keypair_valid_curve25519` | Run `odc-cli cert keypair-wg`, verify output is base64, 44 chars, derive public from private matches |

### C.2 — mTLS Connection Scenarios

Verify the full mTLS WebSocket handshake between odc-hook and odc-registry-manager under various scenarios.

| # | Test | Setup | Verification |
|---|---|---|---|
| C2.1 | `mtls_valid_cert_connects` | Hook uses valid client cert, registry has matching CA | `odc-cli hook status` returns `"connected":true`. Registry log contains `Client ws-... authenticated: CN=odc-hook` |
| C2.2 | `mtls_self_signed_cert_rejected` | Generate self-signed cert with wrong CA, reconfigure hook, restart | Hook logs disconnect, never shows "Connected to registry". `odc-cli hook status` returns `"connected":false` |
| C2.3 | `mtls_no_client_cert_rejected` | Remove client cert paths from hook config, restart hook, registry has `require_client_cert: true` | Hook never connects. Registry logs `TLS handshake failed` |
| C2.4 | `mtls_revoked_cert_rejected` | Revoke hook client cert fingerprint, add CRL to registry config, restart registry | Hook disconnected. New connection attempt rejected. Registry logs `Client ... certificate revoked — closing` |
| C2.5 | `mtls_client_verifies_server` | Configure hook with wrong CA cert for server verification | Hook fails TLS handshake. `odc-cli hook status` returns `"connected":false` |
| C2.6 | `mtls_cert_identity_extracted` | Valid connection established | Registry log contains fingerprint, CN. `connection_certs_` map contains entry |
| C2.7 | `mtls_reconnect_on_registry_restart` | Valid connection → `killall odc-registry-manager` → wait for hook disconnect → restart registry | Hook reconnects within `reconnect_interval_sec` seconds. `odc-cli hook status` shows `"connected":true` after restart window |
| C2.8 | `mtls_hook_resubscribes_on_reconnect` | Hook subscribed to `tunnel.events` → registry restart → hook reconnects | After reconnect, trigger a tunnel event → hook receives it (verified via hook log) |
| C2.9 | `mtls_multiple_hook_instances` | Start second odc-hook on different port with same cert | Both hooks connect, both receive broadcast events, both listed in registry connection count |
| C2.10 | `mtls_tls_version_enforced` | Attempt connection with `openssl s_client -tls1_1` | Connection rejected. With `-tls1_2`, accepted |

### C.3 — VPN Peer Lifecycle (WireGuard)

Uses real WireGuard kernel interfaces. Two WireGuard interfaces on the same host simulate peer communication: `wg0` (server, managed by tunnel-manager) and `wg-test` (peer, managed by test script).

| # | Test | Steps | Verification |
|---|---|---|---|
| C3.1 | `wg_server_interface_created` | Start tunnel-manager with WireGuard config | `ip link show wg0` exists, state UP, has IP 10.0.0.1/24 |
| C3.2 | `wg_server_listen_port_set` | Check `wg show wg0` | Listen port matches config (51820) |
| C3.3 | `wg_server_notifies_registry` | Check registry event log after tunnel-manager startup | `tunnel.server.started` event in registry DB or log |
| C3.4 | `wg_add_peer_via_cli` | `odc-cli tunnel peers add --public-key <test_peer_pubkey>` | Returns `{"ok":true,"assigned_ip":"10.0.0.2"}` |
| C3.5 | `wg_peer_appears_in_kernel` | After C3.4 | `wg show wg0` lists peer with matching public key and allowed IP 10.0.0.2/32 |
| C3.6 | `wg_add_multiple_peers` | Add 5 peers | All 5 appear in `wg show wg0`, IPs are 10.0.0.2–10.0.0.6 |
| C3.7 | `wg_peer_handshake_detected` | Configure matching peer on `wg-test` interface, bring up, initiate handshake (`wg set wg-test peer <wg0_pubkey> endpoint 127.0.0.1:51820`) | Registry receives `tunnel.client.connected` event within poll interval (5s default). Hook receives event via WS subscription |
| C3.8 | `wg_connected_event_payload_correct` | Capture the `tunnel.client.connected` event | JSON contains `client_id`, `tunnel_type: "wireguard"`, `assigned_ip`, `public_key` |
| C3.9 | `wg_peer_disconnect_detected` | Bring down `wg-test` interface or remove peer | Registry receives `tunnel.client.disconnected` event within `stale_timeout_sec` |
| C3.10 | `wg_peer_remove_via_cli` | `odc-cli tunnel peers remove --public-key <key>` | `wg show wg0` no longer lists peer. `odc-cli tunnel peers list` excludes it |
| C3.11 | `wg_ip_pool_auto_allocation` | Add peer without `--ip` flag | Peer gets next available IP from pool (10.0.0.2, then .3, etc.) |
| C3.12 | `wg_ip_pool_release_on_remove` | Remove peer, add new peer without `--ip` | Reuses released IP |
| C3.13 | `wg_ip_pool_exhaustion` | Add peers until subnet full (253 usable hosts in /24) | Last add returns `{"ok":false,"error":"no available IPs in pool"}` |
| C3.14 | `wg_tunnel_status_reflects_state` | `odc-cli tunnel status` during active session | Response shows `peer_count` matching active peers, `running: true` |
| C3.15 | `wg_tunnel_health_check` | `odc-cli tunnel health` after peer connect/disconnect | Returns `{"ok":true,"servers":[{"peers_connected":1,"peers_total":1}]}` |

**WireGuard peer simulation technique**:
```bash
# Server side: tunnel-manager manages wg0 (10.0.0.1/24)
# Test side: create wg-test interface for peer simulation

# Generate keys
wg genkey | tee /tmp/wg-test.key | wg pubkey > /tmp/wg-test.pub
PEER_KEY=$(cat /tmp/wg-test.pub)

# Add peer to tunnel-manager
odc-cli tunnel peers add --public-key "$PEER_KEY" --ip 10.0.0.2

# Create test interface for peer
ip link add wg-test type wireguard
ip addr add 10.0.0.2/24 dev wg-test
wg set wg-test private-key /tmp/wg-test.key
wg set wg-test peer "$(wg show wg0 public-key)" endpoint 127.0.0.1:51820 allowed-ips 10.0.0.1/32
ip link set wg-test up

# Initiate handshake
ping -c 1 -W 1 10.0.0.1  # Triggers WireGuard handshake

# Wait for tunnel-manager to detect (poll interval)
sleep 6
# Verify: registry has tunnel.client.connected event
```

### C.4 — Event Propagation Flow

End-to-end event flow: tunnel-manager → registry-manager (IPC) → odc-hook (WSS) → EventBus → ThreadPool.

| # | Test | Trigger | Verification |
|---|---|---|---|
| C4.1 | `event_flow_tunnel_connected` | C3.7 (WG handshake) | Start `odc-cli events watch --topic tunnel.events` in background. Verify event JSON appears in watch output |
| C4.2 | `event_flow_service_registered` | `odc-cli services register` | Event watcher sees `service.registered` event |
| C4.3 | `event_flow_service_unregistered` | `odc-cli services unregister` | Event watcher sees `service.unregistered` event |
| C4.4 | `event_flow_multiple_topics` | Subscribe to `tunnel.events` AND `services` | Both topic events received by same watcher |
| C4.5 | `event_flow_hook_receives` | Any tunnel event triggered | Hook log contains `"Event received: topic=tunnel.events event=..."` |
| C4.6 | `event_flow_thread_pool_processed` | Trigger events, check hook IPC | `odc-cli hook status` shows `events_processed` counter incremented |
| C4.7 | `event_flow_order_preserved` | Trigger events A, B, C sequentially | Events received in order A→B→C by hook (verified via event watcher output ordering) |
| C4.8 | `event_flow_database_logged` | Trigger event, query SQLite DB | `sqlite3 /var/lib/odc-root/odc-root.db "SELECT * FROM events_log ORDER BY id DESC LIMIT 1"` shows matching event |
| C4.9 | `event_flow_unsubscribe` | Hook sends `{"type":"unsubscribe","topic":"tunnel.events"}` | Subsequent tunnel event NOT delivered to that hook |
| C4.10 | `event_flow_broadcast_to_all` | 2 hooks subscribed to same topic | Both receive the same event |

### C.5 — Webhook Delivery Tests

| # | Test | Steps | Verification |
|---|---|---|---|
| C5.1 | `webhook_configured_and_delivers` | Add webhook config to registry-manager.yaml, start simple HTTP listener (`nc -l 8888`), trigger event | HTTP listener receives POST with JSON body containing event data |
| C5.2 | `webhook_hmac_signature` | Check `X-ODC-Signature` header on received POST | Compute HMAC-SHA256 of body with shared secret, matches header |
| C5.3 | `webhook_retry_on_failure` | Webhook target returns 500, check after retry delays | Delivery retried up to `max_retries` (default 5) with exponential backoff |
| C5.4 | `webhook_auto_disable_after_max_failures` | Target continues returning 500 | After `max_retries` consecutive failures, webhook marked `enabled: false` in config |
| C5.5 | `webhook_event_filtering` | Configure webhook with `events: ["tunnel.*"]`, trigger `services.register` event | Webhook NOT called for non-matching event |
| C5.6 | `webhook_wildcard_matches_all` | Configure webhook with `events: ["*"]` | All events delivered |

### C.6 — Service Discovery & Registry Tests

| # | Test | Steps | Verification |
|---|---|---|---|
| C6.1 | `service_register` | `odc-cli services register --id s1 --name "Drone 1" --type drone` | JSON response `{"ok":true}`, service appears in DB |
| C6.2 | `service_list_empty` | `odc-cli services list` with no registered services | Returns empty array `[]` or `{"services":[]}` |
| C6.3 | `service_list_with_filter` | Register 3 services of different types, `odc-cli services list --type drone` | Only drone-typed services returned |
| C6.4 | `service_unregister` | `odc-cli services unregister --id s1` | Success, `services list` excludes it |
| C6.5 | `service_unregister_nonexistent` | `odc-cli services unregister --id nonexistent` | Returns error, exit non-zero |
| C6.6 | `service_register_duplicate_id` | Register same `--id` twice | Second call returns error or updates existing |
| C6.7 | `service_metadata_stored` | Register service with `--metadata '{"version":"1.0"}'` | Query DB, metadata column contains JSON |
| C6.8 | `status_returns_health` | `odc-cli status` | `{"pong":true,"timestamp":...}` |

### C.7 — CLI-Driven Operations

Test the CLI tool itself for correctness, error handling, and flags.

| # | Test | Command | Verification |
|---|---|---|---|
| C7.1 | `cli_help` | `odc-cli --help` | Exit 0, stdout contains "odc-cli", lists all subcommands |
| C7.2 | `cli_help_subcommand` | `odc-cli cert --help`, `odc-cli tunnel --help`, `odc-cli events --help`, `odc-cli hook --help` | Each prints relevant sub-subcommands |
| C7.3 | `cli_required_arg_missing` | `odc-cli cert issue --cn test` (missing required `--ca-cert` and `--ca-key`) | Exit non-zero, stderr contains "required option" |
| C7.4 | `cli_invalid_flag` | `odc-cli --nonexistent` | Exit non-zero, error message |
| C7.5 | `cli_custom_ipc_socket` | `odc-cli -s /tmp/custom.sock status` | Connects to custom socket path, or fails gracefully if not listening |
| C7.6 | `cli_json_output_valid` | `odc-cli status \| jq .` | jq parses successfully (valid JSON) |
| C7.7 | `cli_cert_full_lifecycle` | Run `cert generate-ca → cert issue → cert verify → cert fingerprint → cert revoke` in sequence | All commands succeed in order. Fingerprint matches issued cert. Revoked cert fails verify |
| C7.8 | `cli_tunnel_full_lifecycle` | Run `tunnel status → peers list → peers add → peers list (verify added) → peers remove → peers list (verify removed)` | Each step returns expected data |

### C.8 — Resiliency & Recovery

| # | Test | Steps | Verification |
|---|---|---|---|
| C8.1 | `registry_restart_services_recover` | Kill registry (`killall odc-registry-manager`), wait 5s, restart | Tunnel-manager reconnects IPC, hook reconnects WSS. New events propagate correctly |
| C8.2 | `tunnel_restart_notifications_resume` | Kill tunnel-manager, wait, restart | Registry detects reconnection, events resume |
| C8.3 | `ipc_client_auto_reconnect` | Kill registry IPC server socket (`rm /run/odc-root/odc-ipc.sock`), wait, restart registry | Tunnel IpcClient reconnects within `reconnect_interval_sec` |
| C8.4 | `graceful_shutdown_no_orphans` | SIGTERM all services | All process exit cleanly, no `defunct` processes, lock/socket files cleaned |
| C8.5 | `hook_reconnect_exponential_backoff` | Repeatedly restart registry at short intervals | Hook reconnect interval increases (check log timestamps) up to max 60s, then resets on success |
| C8.6 | `database_integrity_after_crash` | `kill -9` registry, restart | `sqlite3 ... "PRAGMA integrity_check"` returns `ok`, no corruption |

### C.9 — Concurrent Operations Stress

| # | Test | Steps | Verification |
|---|---|---|---|
| C9.1 | `concurrent_peer_operations` | Run `tunnel peers add` in 5 parallel processes | All succeed, 5 peers added, no IP conflicts |
| C9.2 | `concurrent_service_registration` | Run `services register` in 10 parallel processes with unique IDs | All 10 succeed, all visible in `services list` |
| C9.3 | `concurrent_event_delivery` | Trigger 50 events while hook is subscribed | All 50 events received by hook (count via `odc-cli hook status` before/after) |
| C9.4 | `event_bus_backpressure` | Configure hook EventBus with capacity 8, publish 100 events rapidly | No crashes, `events_processed` < `events_received` (some dropped by try_publish), but system stable |

---

## Phase D: Windows Wine‑Based Smoke Tests

### D.1 — Wine smoke test suite

**New file**: `deploy/test/wine/smoke_test.sh`

Runs inside the Wine Docker container. Connects to the Linux registry-manager across the Docker bridge network (`odc-test` hostname).

| # | Test | Command | Verification |
|---|---|---|---|
| D1.1 | `wine_odc_hook_help` | `wine build-win/odc-hook/odc-hook.exe --help` | Exit 0, stdout contains "Usage" |
| D1.2 | `wine_odc_cli_help` | `wine build-win/tools/odc-cli/odc-cli.exe --help` | Exit 0, lists subcommands |
| D1.3 | `wine_cert_generate_ca` | `wine build-win/tools/odc-cli/odc-cli.exe cert generate-ca --cert /certs/ca.crt --key /certs/ca.key` | Exit 0, cert file created |
| D1.4 | `wine_cert_issue` | `wine odc-cli.exe cert issue --ca-cert /certs/ca.crt --ca-key /certs/ca.key --cn wine-client --cert-out /tmp/wc.crt --key-out /tmp/wc.key` | Exit 0 |
| D1.5 | `wine_cert_verify` | `wine odc-cli.exe cert verify --ca-cert /certs/ca.crt --cert /tmp/wc.crt` | Exit 0 |
| D1.6 | `wine_hook_connect_mtls` | Copy certs to expected paths, write hook config with `server.host: odc-test`, run `wine odc-hook.exe --config /tmp/hook-wine.yaml` in background | Hook connects to Linux registry via mTLS. Log shows "Connected to registry" |
| D1.7 | `wine_hook_receives_event` | Trigger event from Linux container, check Wine hook log | Hook log contains received event JSON |
| D1.8 | `wine_hook_disconnect_clean` | SIGTERM to Wine hook process | Clean shutdown log message, exit 0 |

### D.2 — Windows header audit

Same audit as in `deploy/windows/Dockerfile`:

```bash
! grep -rn --include='*.hpp' --include='*.cpp' \
    -E '<sys/socket\.h>|<sys/un\.h>|<sys/capability\.h>|<linux/' \
    odc-hook/ common/include/ common/src/ | grep -v 'platform_linux'
```
Must return zero matches (no POSIX-only headers in cross-platform code).

---

## Phase E: macOS Cross‑Compile Validation

### E.1 — macOS binary verification

| # | Check | How |
|---|---|---|
| E1.1 | `odc-hook binary type` | `file build/odc-hook/odc-hook` → `Mach-O 64-bit executable x86_64` |
| E1.2 | `odc-cli binary type` | `file build/tools/odc-cli/odc-cli` → `Mach-O 64-bit executable x86_64` |
| E1.3 | `test targets link` | `cmake --build build --target test_event_bus test_thread_pool test_ws_client` succeeds |
| E1.4 | `static analysis` | Add `-Wall -Wextra -Wpedantic` to macOS CXXFLAGS, verify zero warnings from odc-hook and common code |

### E.2 — macOS header audit

Same POSIX header audit as Windows (Phase D.2). Zero matches must be found outside `platform_linux.cpp`.

**Limitation**: macOS binaries cannot execute on Linux. Full macOS runtime testing requires a macOS host or GitHub Actions `macos-13`/`macos-14` runner.

---

## Phase F: Makefile & CI Orchestration

### Task F.1 — Top‑level Makefile for test orchestration

**Existing file to extend**: `deploy/Makefile`

Add targets:

| Target | Description |
|---|---|
| `test-linux` | Build Linux Docker image, run container, execute test suite, capture exit code |
| `test-wine` | Build Linux runtime + Wine containers, run Linux services, run Wine smoke tests against them |
| `test-macos` | (Placeholder) Runs on macOS host via `cmake --build build-macos` + `ctest` |
| `test-all` | Runs `test-linux` then `test-wine` |
| `test-report` | Collects test output from `deploy/test/output/`, produces summary |

### Task F.2 — GitHub Actions CI workflow

**New file**: `.github/workflows/odc-root-integration.yml`

Multiple jobs:

| Job | Runner | Steps |
|---|---|---|
| `build-and-test-linux` | `ubuntu-22.04` | `make -f deploy/Makefile test-linux` |
| `build-and-test-wine` | `ubuntu-22.04` | `make -f deploy/Makefile test-wine` (depends on `build-and-test-linux`) |
| `cross-compile-windows` | `ubuntu-22.04` | Docker MinGW cross-compile + header audit |
| `cross-compile-macos` | `macos-13` | osxcross or native Clang cross-compile + `ctest` |
| `run-macos-tests` | `macos-13` | Build natively, run full test suite |

---

## File Manifest

### New files (17)

| File | Phase |
|---|---|
| `deploy/test/Dockerfile` | B.1 |
| `deploy/test/docker-compose.yml` | B.2 |
| `deploy/test/entrypoint.sh` | B.3 |
| `deploy/test/helpers.sh` | B.4 |
| `deploy/test/test_suite.sh` | B.4 |
| `deploy/test/configs/tunnel-manager.yaml` | B.1 |
| `deploy/test/configs/registry-manager.yaml` | B.1 |
| `deploy/test/configs/odc-hook.yaml` | B.1 |
| `deploy/test/wine/Dockerfile` | B.5 |
| `deploy/test/wine/smoke_test.sh` | D.1 |
| `deploy/test/output/.gitkeep` | B.4 |
| `.github/workflows/odc-root-integration.yml` | F.2 |

### Modified files (6)

| File | Phase | Changes |
|---|---|---|
| `tools/odc-cli/main.cpp` | A.1, A.2, A.3, A.4 | Add `cert`, `tunnel`, `events`, `hook` subcommands |
| `tunnel-manager/tunnel_manager.hpp` | A.2 | Add `IpcServer` member + private handler methods |
| `tunnel-manager/tunnel_manager.cpp` | A.2 | Implement IPC server, register method handlers, wire peer callbacks |
| `tunnel-manager/config/tunnel-manager.yaml` | A.2 | Add `tunnel_ipc_socket_path` |
| `tunnel-manager/CMakeLists.txt` | A.2 | Ensure IpcServer compiles into binary |
| `deploy/Makefile` | F.1 | Add `test-linux`, `test-wine`, `test-all` targets |

---

## Task Execution Order & Dependencies

```
A.1 (cert CLI) ──────────┐
                          ├──► B.1 ──► B.2 ──► B.3 ──► B.4
A.2 (tunnel IPC+CLI) ────┤                                    │
                          │                                    ▼
A.3 (events CLI) ────────┤                              C.1 → C.2 → C.3 → C.4
                          │                              C.5 → C.6 → C.7 → C.8 → C.9
A.4 (hook CLI) ──────────┘

B.5 (Wine Docker) ──► D.1 ──► D.2
B.6 (macOS Docker) ─► E.1 ──► E.2

F.1 (Makefile) ──► depends on all B tasks
F.2 (GitHub CI) ─► depends on F.1
```

- **A.1–A.4** are independent of each other (different subcommands)
- **B.1** depends on A.1–A.4 (needs CLI for cert generation in entrypoint)
- **B.2–B.4** depend on B.1
- **C.1–C.9** depend on B.1–B.4
- **B.5 and D** can run in parallel with C (needs Linux services running)
- **B.6 and E** are independent (cross-compile only)
- **F.1–F.2** wrap everything into CI

---

## Estimated Effort

| Phase | Tasks | Est. Days | Notes |
|---|---|---|---|
| A.1 — cert CLI | 1 task | 0.5 | Reuses CertManager, straightforward CLI11 integration |
| A.2 — tunnel IPC + CLI | 1 task | 1.5 | Requires new IpcServer in tunnel-manager, 5 method handlers, 5 CLI subcommands |
| A.3 — events CLI | 1 task | 0.5 | Uses existing WsClient, CLI11 integration |
| A.4 — hook CLI | 1 task | 0.25 | Minimal IpcServer in HookService, 2 methods |
| B.1 — Linux Docker | 1 task | 0.5 | Dockerfile + 3 config templates |
| B.2 — Docker Compose | 1 task | 0.25 | Compose file with network + capabilities |
| B.3 — entrypoint | 1 task | 0.25 | Bash script for service orchestration |
| B.4 — test framework | 1 task | 0.25 | helpers.sh with assert functions |
| B.5 — Wine Docker | 1 task | 0.5 | Dockerfile + smoke_test.sh |
| B.6 — macOS Docker | 0.5 task | 0.25 | Extend existing Dockerfile |
| C.1–C.9 — Linux tests | 9 sections | 2.5 | ~80 individual test cases in bash |
| D.1–D.2 — Wine tests | 2 sections | 0.5 | ~8 smoke tests |
| E.1–E.2 — macOS validation | 2 sections | 0.25 | Binary checks + header audit |
| F.1 — Makefile | 1 task | 0.25 | Add 4 targets |
| F.2 — GitHub CI | 1 task | 0.25 | Workflow YAML |
| **Total** | **~22 tasks** | **~9 days** | |
