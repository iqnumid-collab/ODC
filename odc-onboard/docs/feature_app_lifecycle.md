# Feature: App Lifecycle

## Overview

The `AppLifecycle` class orchestrates the startup and shutdown sequence of odc-onboard, ensuring all components are initialized and torn down in the correct order.

## Startup Sequence

1. **Thread Orchestrator** -- Created with initial/min/max thread configuration
2. **Event Bus** -- Bound to the orchestrator for async event delivery
3. **Log Control** -- spdlog-basedized, initialized from config
4. **VPN Manager** -- Created, loads connections from JSON config
5. **CAF Actor System** -- Created for actor-based components
6. **App Context** -- Populated with all component pointers
7. **Config Watcher** -- Started for hot-reload monitoring
8. **Actors Spawned** -- Log Actor, System Monitor Actor, VPN Manager Actor

```cpp
AppLifecycle app;
app.set_drain_timeout(10);
app.startup(config_path);
```

## Shutdown Sequence

1. **Stop all VPN connections** -- `VpnManager::stop_all()`
2. **Drain timeout** -- Wait for pending tasks to complete
3. **Publish ShutdownEvent** -- Notify all subscribers
4. **Stop config watcher** -- Stop inotify thread
5. **Await CAF actors** -- Wait for actors to finish
6. **Shutdown orchestrator** -- Stop worker threads
7. **Shutdown logging** -- Flush and close sinks

```cpp
app.shutdown();
```

## Configuration

The drain timeout is configurable:

```bash
odc-onboard --drain-timeout 30
```

## Component Access

After startup, components are accessible via getters:

```cpp
auto& sys = app.actor_system();
auto& bus = app.event_bus();
auto& log = app.log_control();
auto& orch = app.orchestrator();
auto& vpn_mgr = app.vpn_manager();
```

## Source Files

- `src/core/app_lifecycle.hpp`
