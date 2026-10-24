# Feature: Config Hot-Reload

## Overview

odc-onboard watches its configuration file for changes using the Linux `inotify` API. When the file is modified, the new configuration is loaded and applied without restarting the service.

## Implementation

The `ConfigWatcher` class runs a dedicated background thread that:

1. Creates an `inotify` file descriptor
2. Adds a watch on the config file path
3. Monitors for filesystem events
4. Triggers a callback when changes are detected

### Monitored Events

| Event | Description |
|-------|-------------|
| `IN_MODIFY` | File content was modified |
| `IN_CLOSE_WRITE` | File was opened for writing and then closed |
| `IN_MOVE` | File was moved into or out of the watched location |
| `IN_DELETE` | File was deleted |

## Version Check

To prevent unnecessary reloads, the config watcher compares the config version before and after a change. The reload is only triggered if the version number has incremented:

```json
{
  "version": 2
}
```

Increment `version` after making changes to signal a reload.

## Usage

1. Edit the config file:
   ```bash
   sudo nano /etc/odc-onboard/config.json
   ```

2. Increment the version number and save.

3. The watcher detects the change and reloads the configuration automatically.

## Integration

The `ConfigWatcher` is started by `AppLifecycle` during startup:

```cpp
config_watcher_.start(config_path, [this]() {
    reload_config();
});
```

On shutdown, the watcher thread is stopped gracefully.

## Source Files

- `src/core/config_watcher.hpp`
- `src/core/app_lifecycle.hpp` (integration)
