# Feature: Event Bus

## Overview

The EventBus is a publish/subscribe event system that decouples components in odc-onboard. It uses the thread orchestrator for async handler delivery and supports both sync and async publishing.

## Event Types

| Event | Published By | Consumed By |
|-------|-------------|-------------|
| `LogEvent` | Any component | Log Actor |
| `SetLogLevelEvent` | CLI command | Log Actor |
| `MetricsSnapshotEvent` | System Monitor Actor | Subscribers |
| `SystemStatusEvent` | Any component | Subscribers |
| `ShutdownEvent` | Main thread | All actors |
| `VpnStateChangeEvent` | VPN connections | Subscribers |
| `VpnStatsEvent` | VPN connections | Subscribers |
| `VpnHealthCheckEvent` | VPN connections | Subscribers |

## API

### Subscribe

```cpp
auto sub_id = bus.subscribe<VpnStateChangeEvent>([](const VpnStateChangeEvent& e) {
    std::cout << e.connection_id << ": " << e.old_state << " -> " << e.new_state << "\n";
});
```

### Unsubscribe

```cpp
bus.unsubscribe(sub_id);
```

### Publish (Async)

```cpp
bus.publish(VpnStateChangeEvent{
    .connection_id = "my-wg",
    .type = VpnType::WireGuard,
    .old_state = VpnState::Idle,
    .new_state = VpnState::Connecting
});
```

Handlers are submitted to the thread orchestrator and executed asynchronously.

### Publish (Sync)

```cpp
bus.publish_sync(VpnStateChangeEvent{...});
```

Blocks until all handlers have been executed.

## Thread Safety

Uses `std::shared_mutex` for concurrent reads (publish) and exclusive writes (subscribe/unsubscribe). Handler snapshots are taken before delivery to avoid holding the lock during handler execution.

## Source Files

- `src/core/event_bus.hpp`
- `src/core/event.hpp`
