# Feature: Interface Statistics

## Overview

odc-onboard collects real-time traffic statistics for VPN connections by reading from the Linux sysfs interface statistics files.

## Implementation

Statistics are read from `/sys/class/net/<iface>/statistics/` by a background thread every 5 seconds.

### Files Read

| File | Metric |
|------|--------|
| `rx_bytes` | Total bytes received |
| `tx_bytes` | Total bytes sent |
| `rx_packets` | Total packets received |
| `tx_packets` | Total packets sent |

### Function Signature

```cpp
InterfaceStats read_interface_stats(const std::string& iface);
```

Returns a struct with `rx_bytes`, `tx_bytes`, `rx_packets`, `tx_packets`. Returns default/zero values if the interface does not exist or files cannot be read.

## Statistics Tracked

| Metric | Description |
|--------|-------------|
| `bytes_in` | Cumulative received bytes |
| `bytes_out` | Cumulative sent bytes |
| `packets_in` | Cumulative received packets |
| `packets_out` | Cumulative sent packets |
| `duration_sec` | Total time spent in Connected state |
| `reconnect_count` | Number of reconnection attempts |
| `last_error` | Last error message (if any) |

## Events

Statistics updates are published to the EventBus as `VpnStatsEvent`:

```json
{
  "connection_id": "my-wg",
  "type": "wireguard",
  "bytes_in": 1048576,
  "bytes_out": 524288,
  "packets_in": 1024,
  "packets_out": 512,
  "duration_sec": 3600,
  "reconnect_count": 0,
  "timestamp": "2026-05-24T10:30:00Z"
}
```

## CLI Access

```
odc-onboard> vpn status my-wg

Connection: my-wg
  ...
  Statistics:
    Bytes in:      1048576
    Bytes out:     524288
    Packets in:    1024
    Packets out:   512
    Duration:      3600s
    Reconnects:    0
```

## Source Files

- `src/vpn/vpn_connection.hpp` (`read_interface_stats` function)
