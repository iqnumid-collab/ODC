# odc-onboard Architecture

A single-binary C++20 service that manages drone connectivity: VPN tunnels, MAVLink telemetry routing, and cellular modem control.

---

## 1. High-Level Architecture

All major subsystems and how they connect.

![High-Level Architecture](diagrams/rendered/high-level.svg)

| Component | Role |
|-----------|------|
| **VpnManager** | Create, start, stop, and monitor WireGuard + OpenVPN connections |
| **VpnInterfaceMonitor** | Watches Linux netlink for TUN interface create/delete/up/down events |
| **MavRouter** | Primary MAVLink router — bridges flight controller to ground station |
| **MavExtenderManager** | Auto-spawns extender threads when VPN tunnels get IP addresses |
| **QmiModemController** | Manages 4G/5G cellular modems via QMI protocol |
| **EventBus** | Async pub/sub bus connecting all subsystems |
| **IPCServer** | JSON-RPC 2.0 API over Unix domain sockets |
| **CLI** | Interactive REPL console for local management |

---

## 2. Application Lifecycle

From startup to graceful shutdown.

![Lifecycle](diagrams/rendered/lifecycle.svg)

- Parses CLI flags (`--config`, `--daemon`, `--cli`)
- Verifies `CAP_NET_ADMIN` capability for TUN/netlink operations
- Loads JSON configuration and auto-starts configured VPN connections
- Spawns CAF actors for async command handling
- On shutdown: drains extenders, disconnects VPNs, tears down actors

---

## 3. VPN Connection Lifecycle

Every VPN connection (WireGuard or OpenVPN) follows this state machine.

![VPN Lifecycle](diagrams/rendered/vpn-lifecycle.svg)

- **Idle** → **Connecting** → **Connected** on success
- Automatic reconnection with configurable retry delay
- Health check thread monitors interface every N seconds
- Falls through **Error** state on failure, publishes events to EventBus

---

## 4. VPN Management Commands

CLI and IPC API for managing VPN connections.

![VPN Commands](diagrams/rendered/vpn-commands.svg)

Supported operations:
- `vpn add wireguard --id <id> --config <path>`
- `vpn add openvpn --id <id> --config <path> [--creds <path>]`
- `vpn start|stop|restart|remove|list|status`

Thread-safe registry with `shared_mutex` for concurrent access.

---

## 5. MAVLink Extender Auto-Spawn

When a VPN connects, an extender is automatically created to tunnel MAVLink.

![Extender Flow](diagrams/rendered/extender-flow.svg)

- VpnInterfaceMonitor detects a TUN interface getting an IP address
- EventBus notifies MavExtenderManager
- A new extender thread starts: UDP client → primary router over the VPN tunnel
- On interface down/address removed: extender is torn down automatically
- Manual start/stop/remove also available via CLI and IPC

---

## 6. Event-Driven Architecture

All subsystems communicate through a shared EventBus.

![Event Bus](diagrams/rendered/event-bus.svg)

| Event Type | Produced By |
|------------|-------------|
| VPN state change, stats, health | VpnConnection |
| Interface created/deleted, up/down, IP assigned/removed | VpnInterfaceMonitor |
| Router status, endpoint stats, routing stats | MavRouter |
| Extender started/stopped | MavExtenderManager |
| System metrics (CPU, memory, thread pool) | SystemMonitorActor |
| Modem state, connection, telemetry | QmiModemController |

All events are dispatched asynchronously via the thread pool to registered handlers (log output, CLI display, CAF actors).

---

## 7. IPC API

JSON-RPC 2.0 over Unix domain socket (`/run/odc-onboard/odc-onboard.sock`).

| Method | Description |
|--------|-------------|
| `vpn.list` | List all connections |
| `vpn.status` | Get detailed status for a connection |
| `vpn.start` | Start a connection |
| `vpn.stop` | Stop a connection |
| `vpn.add_wireguard` | Add a WireGuard connection |
| `vpn.add_openvpn` | Add an OpenVPN connection |
| `vpn.remove` | Remove a connection |
| `mavlink.status` | Get MAVLink router status |
| `mavlink.extender.list` | List all MAVLink extenders |
| `mavlink.extender.start` | Manually start an extender |
| `mavlink.extender.stop` | Stop an extender |
| `system.metrics` | Get thread pool metrics |
| `log.level` | Get/set runtime log level |
