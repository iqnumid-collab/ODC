# Feature: CAF Actor System

## Overview

odc-onboard uses the C++ Actor Framework (CAF) for concurrent, message-passing-based components. Three actors handle logging, system monitoring, and VPN management.

## Actor Version

CAF 0.17.6 (installed as system package).

## Actors

### Log Actor

**Purpose**: Centralized log routing via the event bus.

- Subscribes to `LogEvent` and `SetLogLevelEvent`
- Routes messages through `LogControl` with `[actor]` prefix
- Responds to `mon_tick` with periodic log flush (debug-level)

### System Monitor Actor

**Purpose**: Periodic metrics collection and publishing.

- Polls thread orchestrator metrics every 5 seconds
- Publishes `MetricsSnapshotEvent` to the event bus
- Control messages: `mon_start` (start polling), `mon_stop` (stop polling)
- Auto-starts on spawn

### VPN Manager Actor

**Purpose**: Message-based VPN command handling.

- Wraps `VpnManager` for actor-safe access
- Handles string-based commands: `start`, `stop`, `restart`, `remove`
- Returns `VpnCommandResult` (success + message)
- Responds to `mon_tick` with periodic connection status checks

## CAF Atoms

| Atom | Purpose |
|------|---------|
| `mon_tick` | Periodic monitoring tick |
| `mon_start` | Start monitoring |
| `mon_stop` | Stop monitoring |

## Actor Lifecycle

Actors are spawned during `AppLifecycle::startup()`:

```cpp
auto log_actor_hdl = odc::make_log_actor(sys, bus, log);
auto monitor_hdl = odc::make_system_monitor_actor(sys, orch, bus);
auto vpn_actor_hdl = odc::make_vpn_manager_actor(sys, vpn_mgr);
```

During shutdown, all actors are awaited before the system terminates.

## Source Files

- `src/actors/log_actor.hpp`
- `src/actors/system_monitor_actor.hpp`
- `src/actors/vpn_manager_actor.hpp`
- `src/core/type_id.hpp` (CAF type IDs and atoms)
