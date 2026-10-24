# Feature: VPN Manager

## Overview

The `VpnManager` is the central registry for all VPN connections. It provides thread-safe CRUD operations, config loading/persistence, and connection lifecycle management.

## Thread Safety

Uses `std::shared_mutex` for concurrent reads and exclusive writes. Multiple threads can query connection state simultaneously, but mutations (add/remove/start/stop) require exclusive access.

## API

### Connection Management

| Method | Description |
|--------|-------------|
| `start_connection(id)` | Start a VPN connection |
| `stop_connection(id)` | Stop a VPN connection |
| `restart_connection(id)` | Stop, wait 2s, start |
| `remove_connection(id)` | Remove and persist config change |
| `add_wireguard(config)` | Add a WireGuard connection |
| `add_openvpn(config)` | Add an OpenVPN connection |

### Queries

| Method | Description |
|--------|-------------|
| `list_connections()` | Returns all connections with current state, stats, and health |
| `get_connection(id)` | Returns a single connection's info (or `std::nullopt`) |

### Lifecycle

| Method | Description |
|--------|-------------|
| `load_config(app_config)` | Creates connections from application config |
| `save_config()` | Persists current connections back to JSON file |
| `stop_all()` | Stops all connections (used during shutdown) |

## Config Persistence

When connections are added or removed, the manager automatically saves the updated config to the JSON file:

```cpp
vpn_mgr.remove_connection("my-wg");  // Automatically persists
```

## Connection Info Structure

```cpp
struct VpnConnectionInfo {
    std::string id;
    VpnType type;
    VpnState state;
    bool enabled;
    std::string name;
    std::string config_path;
    InterfaceStats stats;
    HealthStatus health;
};
```

## Startup Behavior

During `AppLifecycle::startup()`, the manager loads all connections from the config file. Connections with `auto_start: true` are started automatically.

## Source Files

- `src/vpn/vpn_manager.hpp`
- `src/vpn/vpn_config.hpp`
