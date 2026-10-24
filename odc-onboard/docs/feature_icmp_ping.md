# Feature: ICMP Ping Health Checks

## Overview

Both WireGuard and OpenVPN connections support periodic health checks using ICMP echo requests (ping). This detects connectivity issues and triggers automatic reconnection when the tunnel becomes unresponsive.

## Implementation

Uses raw ICMP sockets (`SOCK_RAW` with `IPPROTO_ICMP`) to send and receive echo requests/replies. No external `ping` binary is required.

### ICMP Packet Structure

- **Type**: `ICMP_ECHO` (8) for requests, `ICMP_ECHOREPLY` (0) for replies
- **Checksum**: Computed over the ICMP header and payload
- **Identifier**: Process-specific ID to match replies
- **Sequence**: Incrementing sequence number

### Checksum Calculation

Standard Internet checksum (one's complement of one's complement sum).

## Configuration

Health checks are configured per-connection in the application config:

```json
{
  "metadata": {
    "health_check": {
      "enabled": true,
      "interval_sec": 30,
      "target_ip": "10.66.66.1"
    },
    "max_retries": 3
  }
}
```

| Field | Description | Default |
|-------|-------------|---------|
| `enabled` | Enable periodic health checks | `false` |
| `interval_sec` | Seconds between checks | `30` |
| `target_ip` | IP address to ping | (required if enabled) |
| `max_retries` | Consecutive failures before restart | `3` (WG) / `5` (OVPN) |

## Health Check Flow

1. Background thread runs at `interval_sec` intervals
2. Sends ICMP echo request to `target_ip`
3. Waits for reply with a timeout
4. Records latency in milliseconds
5. On success: resets `consecutive_failures` to 0, sets `healthy = true`
6. On failure: increments `consecutive_failures`, sets `healthy = false`
7. If `consecutive_failures >= max_retries`: triggers automatic restart

## Health State

| Field | Description |
|-------|-------------|
| `healthy` | Current health status (boolean) |
| `latency_ms` | Last successful ping latency in milliseconds |
| `consecutive_failures` | Count of consecutive failed checks |
| `last_check` | Timestamp of the last health check |

## Events

Health check results are published to the EventBus as `VpnHealthCheckEvent`:

```json
{
  "connection_id": "my-wg",
  "type": "wireguard",
  "healthy": true,
  "latency_ms": 12,
  "consecutive_failures": 0,
  "timestamp": "2026-05-24T10:30:00Z"
}
```

## Prerequisites

Creating raw ICMP sockets requires `CAP_NET_RAW` capability. When running as root or with `CAP_NET_ADMIN`, this is typically included in the bounding set.

## Source Files

- `src/vpn/wireguard_connection.hpp` (ping implementation)
- `src/vpn/openvpn_connection.hpp` (ping implementation)
- `src/vpn/vpn_connection.hpp` (health check orchestration)
