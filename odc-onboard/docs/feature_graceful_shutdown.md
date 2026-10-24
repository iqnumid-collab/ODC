# Feature: Graceful Shutdown

## Overview

odc-onboard performs a coordinated shutdown sequence when receiving `SIGTERM` or `SIGINT`, ensuring VPN connections are cleanly terminated and resources are released.

## Signal Handling

| Signal | Behavior |
|--------|----------|
| `SIGTERM` | Traces graceful shutdown |
| `SIGINT` | Traces graceful shutdown |

Signals are caught via `std::signal()` and set an atomic `g_running` flag to `false`.

## Shutdown Sequence

1. **Stop all VPN connections** -- `VpnManager::stop_all()` iterates all connections and calls `stop()` on each
2. **Drain timeout** -- Waits for the configured drain period (default 10s) for pending tasks to complete
3. **Publish shutdown event** -- `EventBus` publishes `ShutdownEvent` to notify all subscribers
4. **Stop config watcher** -- Stops the inotify background thread
5. **Await CAF actors** -- Waits for all actors to finish processing
6. **Shutdown thread orchestrator** -- Stops worker threads, drains and discards pending tasks
7. **Shutdown logging** -- Flushes and closes log sinks

## Drain Timeout

Configurable via `--drain-timeout <seconds>`:

```bash
odc-onboard --drain-timeout 30
```

| Value | Behavior |
|-------|----------|
| `0` | No drain wait, immediate shutdown |
| `10` (default) | Wait 10 seconds for pending tasks |
| `30` | Wait 30 seconds for pending tasks |

## systemd Integration

The service file configures a watchdog timeout:

```ini
WatchdogSec=30
```

If the service does not respond to systemd's watchdog ping within 30 seconds, systemd will restart it.

## Source Files

- `src/core/app_lifecycle.hpp`
- `src/main.cpp` (signal handler)
