# Feature: JSON Output Mode

## Overview

odc-onboard supports JSON output mode for machine-readable responses. This enables integration with scripts, monitoring systems, and automation tools.

## Enabling JSON Mode

### Command-line Flag

```bash
odc-onboard --check-privileges --json
```

### CLI Console Toggle

```
odc-onboard> --json
JSON output mode enabled
odc-onboard> --no-json
JSON output mode disabled
```

## JSON Output Examples

### Privilege Check

```json
{
  "is_root": false,
  "has_net_admin": true,
  "missing_capabilities": []
}
```

### VPN List

```json
[
  {
    "id": "my-wg",
    "type": "wireguard",
    "state": "Connected",
    "enabled": true,
    "name": "My WireGuard",
    "config_path": "/etc/odc-onboard/vpn/my-wg.conf",
    "stats": {
      "bytes_in": 1048576,
      "bytes_out": 524288,
      "packets_in": 1024,
      "packets_out": 512,
      "duration_sec": 3600,
      "reconnect_count": 0
    },
    "health": {
      "healthy": true,
      "latency_ms": 12,
      "consecutive_failures": 0
    }
  }
]
```

### VPN Status

```json
{
  "id": "my-wg",
  "type": "wireguard",
  "state": "Connected",
  "enabled": true,
  "name": "My WireGuard",
  "config_path": "/etc/odc-onboard/vpn/my-wg.conf",
  "stats": {
    "bytes_in": 1048576,
    "bytes_out": 524288,
    "packets_in": 1024,
    "packets_out": 512,
    "duration_sec": 3600,
    "reconnect_count": 0,
    "last_error": ""
  },
  "health": {
    "healthy": true,
    "latency_ms": 12,
    "consecutive_failures": 0
  }
}
```

### VPN Actions

```json
{
  "success": true,
  "message": "Starting connection: my-wg"
}
```

### Config Validation

```json
{
  "valid": true,
  "path": "/etc/odc-onboard/vpn/my-wg.conf",
  "errors": [],
  "warnings": []
}
```

### Log Level Change

```json
{
  "level": "debug"
}
```

### Metrics

```json
{
  "active_tasks": 2,
  "pending_tasks": 0,
  "completed_tasks": 15,
  "worker_count": 4
}
```

### Error Response

```json
{
  "error": "Connection not found: nonexistent"
}
```

## Source Files

- `src/cli/cli_app.hpp` (JSON output in all commands)
- `src/main.cpp` (`--json` flag handling)
