# odc-root — Architecture

## System Overview

odc-root provides three services for VPN server management, real-time event streaming, and generic service registry with mTLS WebSocket communication, plus a unified CLI tool.

```
                              ┌──────────────────────────────┐
                              │       SQLite Database         │
                              │  /var/lib/odc-root/odc-root.db│
                              └───┬──────────────┬────────────┘
                                  │              │
┌─────────────────────┐    ┌──────▼──────┐  ┌────▼───────────────┐
│    odc-hook         │    │  odc-tunnel- │  │  odc-registry-     │
│                     │    │  manager     │  │  manager           │
│ WsClient (mTLS) ◄───┼────┤              ├──┤                    │
│ EventBus            │ WS │ WireGuard    │IP│ WsServer (TLS/mTLS)│
│ ThreadPool          │    │ OpenVPN      │C │ WebhookClient      │
│ VPN subsystem       │    │ SessionTracker│  │ DataStore          │
│ IpcServer           │    │ CertGenerator │  │ CertGenerator      │
│                     │    │ IpcServer ◄───┼──┤ IpcServer          │
│ hook.sock ◄─────────┼───►│tunnel-ipc.sock│  │ odc-ipc.sock       │
└─────────────────────┘    └───────────────┘  └────────┬───────────┘
                                                       │
                                                  ┌────▼────┐
                                                  │ External │
                                                  │ Clients  │
                                                  │ (WS/TLS) │
                                                  └─────────┘

┌─────────────────────────────────────────────────────────────┐
│                        odc-cli                               │
│  cert │ tunnel │ events │ hook │ services │ status           │
│  (all services via IPC sockets + WS for events watch)        │
└─────────────────────────────────────────────────────────────┘
```

## Component Catalog

### libodc-common
| Component   | Purpose                                                |
|-------------|--------------------------------------------------------|
| Logging     | spdlog wrapper with runtime log level switching         |
| YamlConfig  | YAML load/save with atomic write (yaml-cpp)             |
| SqliteDb    | Thread-safe SQLite wrapper with prepared statements      |
| CertManager | OpenSSL-based CA + client cert generation (X509/curve25519) |
| IpcServer   | JSON-RPC 2.0 server over Unix domain socket             |
| IpcClient   | JSON-RPC 2.0 client over Unix domain socket              |
| HttpClient  | asio-based HTTP/HTTPS client with TLS support           |
| Platform    | Cross-platform daemonization, signal handling           |

### odc-tunnel-manager
| Component       | Purpose                                           |
|-----------------|---------------------------------------------------|
| WireGuardServer | Server-side WireGuard via kernel netlink. Creates WG interfaces, manages peers, allocates IPs from subnet pools, polls kernel for connect/disconnect detection with real handshake timestamps and rx/tx byte counters |
| OpenVpnServer   | In-process OpenVPN3 `ServerProto` on a dedicated `io_context` thread (UDP). Facade: start/stop/list/disconnect. Transport demux + Man (auth/IP/PUSH) + shared TUN mux. No classic `openvpn` child. |
| SessionTracker  | SQLite-backed VPN client session tracking with in-memory cache |
| VpnCertGenerator| VPN client credential generation (X509 / WG keys)  |
| IpcServer       | Secondary IPC socket (`tunnel-ipc.sock`) for CLI commands: status, peers list/add/remove, health |

### odc-registry-manager
| Component       | Purpose                                           |
|-----------------|---------------------------------------------------|
| WsServer        | Custom RFC 6455 WebSocket server over raw asio::ssl::stream (no external WebSocket library). Full HTTP upgrade handshake, frame parsing (all length categories, unmasking, fragmentation), ping/pong, close handling, pub/sub topic management, auto-cleanup on disconnect |
| WebhookClient   | HTTP POST delivery with HMAC-SHA256 signing, exponential backoff retry, auto-disable after max failures |
| DataStore       | SQLite CRUD for services, clients, webhooks, events |
| WsCertGenerator | TLS client certificate generation for WS clients   |
| CertRevocationList | CRL loading and fingerprint-based deny/allow checks |

### odc-hook
| Component       | Purpose                                           |
|-----------------|---------------------------------------------------|
| HookService     | Central orchestrator: loads config, manages lifecycle |
| WsClient        | mTLS WebSocket client with auto-reconnect, frame encode/decode, ping/pong, full TLS handshake with client cert verification |
| EventBus        | Thread-safe bounded deque for JSON events with backpressure, shutdown support, atomic counters |
| ThreadPool      | Worker threads consuming from EventBus, exception-safe handler invocation, active count tracking |
| IpcServer       | IPC socket (`hook.sock`) exposing status (connected, server, event counts, thread pool state) |
| VPN subsystem   | Full WireGuard (kernel netlink + userspace Noise IKpsk2 handshake) and OpenVPN3 client support |

### odc-cli
16 subcommands across 6 groups:

| Group    | Subcommands                            | Target          |
|----------|----------------------------------------|-----------------|
| cert     | generate-ca, issue, verify, fingerprint, revoke, keypair-wg | Local (no IPC) |
| tunnel   | status, peers list, peers add, peers remove, health | tunnel-ipc.sock |
| events   | watch                                  | wss:// registry |
| hook     | status                                 | hook.sock |
| services | list, register, unregister             | odc-ipc.sock |
| status   | —                                      | odc-ipc.sock |

## IPC Protocol

JSON-RPC 2.0 over Unix domain sockets, newline-delimited JSON. Three IPC sockets:

### odc-ipc.sock (registry-manager)
**tunnel-manager → registry-manager** (notifications):
- `tunnel.server.started` — VPN server started
- `tunnel.server.stopped` — VPN server stopped
- `tunnel.client.connected` — Client connected to VPN
- `tunnel.client.disconnected` — Client disconnected
- `tunnel.cert.issued` — Certificate issued to client
- `tunnel.error` — Tunnel-level error

**External CLI → registry-manager** (requests):
- `ping` — Health check
- `status` — Get registry status (ws_connections, services count)
- `services.list` — List services (with optional type filter)
- `services.register` — Register a new service
- `services.unregister` — Unregister a service

### tunnel-ipc.sock (tunnel-manager)
**CLI → tunnel-manager** (requests):
- `tunnel.status` — Get server list with types and peer counts
- `tunnel.peers.list` — List all WG peers + OVPN clients with keys, IPs, stats
- `tunnel.peers.add` — Add WG peer, auto-allocate IP from subnet pool
- `tunnel.peers.remove` — Remove WG peer, release IP back to pool
- `tunnel.health` — Connected/total peer counts per server

### hook.sock (odc-hook)
**CLI → odc-hook** (requests):
- `hook.status` — Returns connected state, server address, events_received, events_processed, thread_pool_active count
- `hook.ping` — Pong response

## WebSocket Protocol

WebSocket clients connect via `wss://host:9443/ws` (TLS). Custom RFC 6455 implementation over raw `asio::ssl::stream`.

### HTTP Upgrade
1. Client sends TLS ClientHello → server responds with TLS 1.2+ handshake (hardened cipher suite: ECDHE+AESGCM, no SSLv2/3, no TLSv1/1.1, no renegotiation, no session tickets)
2. Client sends HTTP GET /ws with `Upgrade: websocket`, `Sec-WebSocket-Key`, etc.
3. Server responds HTTP 101 with `Sec-WebSocket-Accept` = base64(sha1(key + magic GUID))

### Frame Protocol
- Client→Server frames MUST be masked (RFC 6455 §5.1)
- Server→Client frames are NOT masked
- Supported opcodes: text(1), binary(2), close(8), ping(9), pong(10)
- Full fragmentation support (continuation frames)
- 125-byte, 16-bit, and 64-bit extended payload lengths

### Pub/Sub Model
- Subscriptions are managed at the WsServer layer (intercepted before application dispatch)
- On disconnect, all subscriptions are auto-cleaned up

### Messages

Client → Server:
- `{"type":"subscribe","topic":"..."}`
- `{"type":"unsubscribe","topic":"..."}`
- `{"type":"service.register","service":{...}}`
- `{"type":"service.unregister","service_id":"..."}`
- `{"type":"query","filter":{"type":"..."},"limit":50}`

Server → Client:
- `{"type":"subscribed","topic":"..."}`
- `{"type":"unsubscribed","topic":"..."}`
- `{"type":"service.registered","service":{...}}`
- `{"type":"service.unregistered","service_id":"..."}`
- `{"type":"query_result","services":[...],"total":42}`
- `{"type":"event","topic":"...","data":{...}}`
- `{"type":"error","code":"...","message":"..."}`

## Database Schema

Tables (shared SQLite): `services`, `clients`, `webhooks`, `events_log`, `vpn_servers`, `vpn_clients`, `schema_migrations`

## Threading Model
- Common library: `SqliteDb` uses `recursive_mutex` for all operations
- IpcServer: accept loop on main thread, one thread per client connection
- WireGuardServer: operations under `mutex`, polling on main loop
- OpenVpnServer: subprocess management, status file parsing on main loop poll
- WsServer: asio io_context thread for accept + reads, synchronous writes
- odc-hook: asio io_context thread for WsClient, ThreadPool workers for event processing, EventBus consumer threads, optional TUN read threads
- WebhookClient: each delivery in a detached thread
- TunnelManager: main loop polls WG peers + OVPN status every N seconds
- RegistryManager: IPC server thread + WsServer asio thread

## Security
- YAML config files: filesystem permissions
- CA + certs: OpenSSL-based PKI, X.509 v3, PEM file storage
- IPC sockets: filesystem permissions on /run/odc-root/
- WebSocket: TLS 1.2+ encryption with hardened cipher suite, optional mTLS with client cert verification + CRL checks
- Webhooks: HMAC-SHA256 signing header (X-ODC-Signature)
- WireGuard: Curve25519 key exchange via kernel netlink
- OpenVPN: TLS-based control channel, AES-256-GCM data channel
