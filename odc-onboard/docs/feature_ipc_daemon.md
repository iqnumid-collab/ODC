# Feature: IPC Daemon Architecture

## Overview

odc-onboard implements a daemon + client architecture using JSON-RPC 2.0 over Unix domain sockets. The daemon runs as a systemd service, and CLI instances attach to it for real-time management and event streaming.

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    odc-onboard (daemon)                       │
│                                                              │
│  ThreadOrchestrator (worker pool, 2-8 threads)               │
│  ├── Worker: IpcServer accept() loop                         │
│  ├── Worker: IpcSession (CLI client #1)                      │
│  ├── Worker: IpcSession (CLI client #2)                      │
│  ├── Worker: EventBus handler delivery                       │
│  └── ... (dynamic scaling)                                   │
│                                                              │
│  Unix Socket: /run/odc-onboard/odc-onboard.sock (0660)       │
└──────────────────────────────────────────────────────────────┘
                            │
                JSON-RPC 2.0 over Unix socket
                            │
┌──────────────────────────────────────────────────────────────┐
│              odc-onboard --cli (client process)               │
│                                                              │
│  select() loop on {stdin, socket_fd}                         │
│  ├── stdin ready  → encode command as JSON-RPC → send       │
│  └── socket ready → decode response/event → display          │
│                                                              │
│  If socket not available → fallback to standalone mode       │
└──────────────────────────────────────────────────────────────┘
```

## Binary Behavior

| Invocation | Behavior |
|------------|----------|
| `odc-onboard` | Daemon mode: bind socket, start IPC server via orchestrator |
| `odc-onboard --daemon` | Explicit daemon mode (same as above) |
| `odc-onboard --cli` | Try to attach to daemon; fallback to standalone if not running |
| `odc-onboard --check-privileges` | Standalone, no socket |
| `odc-onboard --encrypt-creds ...` | Standalone, no socket |
| `odc-onboard --decrypt-creds ...` | Standalone, no socket |

## Single Instance Enforcement

The daemon binds to a Unix domain socket. If the socket is already in use, another daemon is running and the new instance exits with an error.

```
ERROR: Failed to start IPC server on /run/odc-onboard/odc-onboard.sock
```

The socket file is removed on clean shutdown via `ExecStopPost` in the systemd service.

## JSON-RPC 2.0 Protocol

All communication is newline-delimited JSON over the Unix socket.

### Commands (CLI → Daemon)

```json
{"jsonrpc": "2.0", "method": "vpn.list", "id": 1}
{"jsonrpc": "2.0", "method": "vpn.start", "params": {"id": "my-wg"}, "id": 2}
{"jsonrpc": "2.0", "method": "subscribe", "id": 3}
```

### Responses (Daemon → CLI)

```json
{"jsonrpc": "2.0", "result": [...], "id": 1}
{"jsonrpc": "2.0", "error": {"code": -32601, "message": "Connection not found"}, "id": 2}
```

### Event Notifications (Daemon → CLI, streaming)

```json
{"jsonrpc": "2.0", "method": "vpn.state_change", "params": {"connection_id": "my-wg", "old_state": "Idle", "new_state": "Connecting"}}
{"jsonrpc": "2.0", "method": "vpn.stats", "params": {"connection_id": "my-wg", "bytes_in": 1048576, ...}}
{"jsonrpc": "2.0", "method": "vpn.health_check", "params": {"connection_id": "my-wg", "healthy": true, "latency_ms": 12}}
{"jsonrpc": "2.0", "method": "log", "params": {"level": "INFO", "message": "Connection established"}}
```

## Method Registry

| Method | Description |
|--------|-------------|
| `vpn.list` | List all VPN connections |
| `vpn.status` | Get connection details |
| `vpn.start` | Start a connection |
| `vpn.stop` | Stop a connection |
| `vpn.restart` | Restart a connection |
| `vpn.remove` | Remove a connection |
| `vpn.add` | Add a new connection (wireguard/openvpn) |
| `vpn.config.validate` | Validate a config file |
| `log.level` | Change log level |
| `metrics` | Get orchestrator metrics |
| `privileges.check` | Check CAP_NET_ADMIN status |
| `subscribe` | Subscribe to event stream |
| `unsubscribe` | Unsubscribe from events |

## IPC Server as Orchestrator Task

The IPC server runs as a task submitted to the thread orchestrator -- no dedicated `std::thread`:

```cpp
orchestrator_->submit_simple([this, token]() mutable {
    ipc_server_->run(token);  // blocking accept() loop on a worker
});
```

Each accepted client connection spawns a new orchestrator task:

```cpp
int client_fd = accept(listen_fd, ...);
orch_.submit_simple([client_fd, this]() {
    IpcSession session(client_fd, ...);
    session.run();  // blocking read/write on a worker
});
```

## Authentication

Socket file permissions: `0660`, owned by `root:odc-onboard`. Only users in the `odc-onboard` group can connect.

```bash
sudo groupadd -r odc-onboard
sudo usermod -aG odc-onboard $USER
```

## CLI Backend Abstraction

The CLI uses a backend interface that supports two modes:

| Backend | Mode | Description |
|---------|------|-------------|
| `CliLocalBackend` | Standalone | Direct access to VpnManager, LogControl, Orchestrator |
| `CliRemoteBackend` | Attached | JSON-RPC over Unix socket to running daemon |

The `CliApp` class is backend-agnostic -- it routes all commands through the `CliBackend` interface.

## Real-Time Event Display

When attached to the daemon, the CLI uses `select()` to multiplex between stdin and the socket:

```
odc-onboard> vpn list
ID                  Type        State         Enabled   Name
----------------------------------------------------------------------------
my-wg               wireguard   Connected     yes       My WireGuard

odc-onboard> [event] my-wg: Connected -> Disconnecting
odc-onboard> [event] my-wg: Disconnecting -> Disconnected
odc-onboard> [event] my-wg: Disconnected -> Reconnecting
odc-onboard> [event] my-wg: Reconnecting -> Connecting
odc-onboard> [event] my-wg: Connecting -> Connected
odc-onboard> vpn status my-wg
Connection: my-wg
  State:     Connected
  Latency:   12ms
```

## systemd Integration

```ini
[Service]
ExecStartPre=/usr/bin/install -d -m 0750 -o root -g odc-onboard /run/odc-onboard
ExecStart=/usr/bin/odc-onboard --daemon --config /etc/odc-onboard/config.json
ExecStopPost=/usr/bin/rm -f /run/odc-onboard/odc-onboard.sock /run/odc-onboard/odc-onboard.pid

RuntimeDirectory=odc-onboard
RuntimeDirectoryMode=0750
```

## Source Files

| File | Purpose |
|------|---------|
| `src/ipc/jsonrpc.hpp` | JSON-RPC 2.0 message types, serialize/deserialize |
| `src/ipc/ipc_server.hpp` | Unix socket bind/listen/accept, submitted as orchestrator task |
| `src/ipc/ipc_session.hpp` | Per-client session: read JSON-RPC, dispatch, write response, event subscription |
| `src/ipc/ipc_client.hpp` | CLI-side: connect, send_request, poll_events (select-based) |
| `src/ipc/method_registry.hpp` | Handler registration: `register_method(name, handler)` |
| `src/cli/cli_backend.hpp` | Abstract interface: `vpn_list()`, `vpn_start()`, `poll_event()`, etc. |
| `src/cli/cli_local_backend.hpp` | Standalone mode backend (direct component access) |
| `src/cli/cli_remote_backend.hpp` | IPC client mode backend (JSON-RPC over socket) |
| `src/cli/cli_app.hpp` | CLI REPL, uses `CliBackend&` for all operations |
| `src/core/app_lifecycle.hpp` | Launches IPC server via `orchestrator.submit_simple()`, registers methods |
| `src/main.cpp` | `--daemon` flag, `--cli` → try remote first, fallback to local |
