# Feature: WireGuard Connection

## Overview

odc-onboard supports WireGuard VPN connections using a minimal C++ WireGuard client implementation. It handles the full connection lifecycle: parsing config, creating the interface, configuring addresses/routes, and monitoring health.

## Configuration

WireGuard connections are defined in standard `.conf` files:

```ini
[Interface]
PrivateKey = <base64-encoded private key>
Address = 10.66.66.3/32
DNS = 10.66.66.1

[Peer]
PublicKey = <base64-encoded public key>
PresharedKey = <base64-encoded preshared key>
Endpoint = 51.178.82.148:54193
AllowedIPs = 10.66.66.0/24
```

### Application Config Entry

```json
{
  "vpn": {
    "wireguard": {
      "connections": [
        {
          "id": "my-wg",
          "enabled": true,
          "config_path": "/etc/odc-onboard/vpn/my-wg.conf",
          "metadata": {
            "name": "My WireGuard",
            "auto_start": true,
            "max_retries": 3,
            "health_check": {
              "enabled": true,
              "interval_sec": 30,
              "target_ip": "10.66.66.1"
            }
          }
        }
      ]
    }
  }
}
```

## Connection Lifecycle

### Start

1. Parse the `.conf` file to extract Interface and Peer sections
2. Extract the server endpoint (address + port)
3. Create a TUN interface via NetlinkManager
4. Add the IP address to the interface
5. Bring the interface up
6. Add a default route through the WireGuard peer
7. Initialize the WireGuard protocol client
8. Establish the connection to the endpoint

### Connected

- Data flows through the TUN interface
- Stats are collected every 5 seconds from `/sys/class/net/<iface>/statistics/`
- Health checks run at the configured interval (ICMP ping to `target_ip`)

### Health Check

- Sends ICMP echo requests to `target_ip`
- Tracks latency in milliseconds
- Counts consecutive failures
- If failures reach `max_retries`, the connection is automatically restarted

### Disconnect

1. Send a WireGuard "bye" message to the peer
2. Bring the interface down via NetlinkManager
3. Clean up the TUN interface

### Reconnect

On health check failure or unexpected disconnect:
1. Stop the current connection
2. Wait 2 seconds
3. Attempt to start again
4. Repeat up to `max_retries` times

## Interface Naming

The WireGuard interface is named `wg-<id>` where `<id>` is the connection ID from the config.

## Statistics

| Metric | Source |
|--------|--------|
| `bytes_in` | `/sys/class/net/wg-<id>/statistics/rx_bytes` |
| `bytes_out` | `/sys/class/net/wg-<id>/statistics/tx_bytes` |
| `packets_in` | `/sys/class/net/wg-<id>/statistics/rx_packets` |
| `packets_out` | `/sys/class/net/wg-<id>/statistics/tx_packets` |
| `duration_sec` | Accumulated time in Connected state |
| `reconnect_count` | Number of reconnection attempts |

## Connection Timeout

A configurable timeout (default 30 seconds) is applied during the connection phase. If the connection is not established within this time, the attempt is aborted and treated as a failure.

## Source Files

- `src/vpn/wireguard_connection.hpp`
- `src/vpn/vpn_connection.hpp` (base class)
