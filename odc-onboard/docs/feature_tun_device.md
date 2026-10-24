# Feature: TUN Device Management

## Overview

odc-onboard creates and manages TUN/TAP network devices for VPN connections using the Linux `/dev/net/tun` character device and ioctl system calls.

## Implementation

The `TunDevice` class provides RAII-style management of TUN/TAP devices:

1. Opens `/dev/net/tun`
2. Configures the device via `TUNSETIFF` ioctl
3. Returns a file descriptor for use by VPN clients
4. Automatically closes the fd on destruction

## Device Creation

```cpp
TunDevice tun("wg-myvpn", TunDevice::Mode::Tun);
int fd = tun.fd();
```

### Flags

| Flag | Description |
|------|-------------|
| `IFF_TUN` | Layer 3 IP tunnel (used by WireGuard and OpenVPN) |
| `IFF_TAP` | Layer 2 Ethernet tunnel |
| `IFF_NO_PI` | No packet information header (raw packets only) |

## Interface Naming Convention

| Connection Type | Interface Name |
|-----------------|----------------|
| WireGuard | `wg-<id>` |
| OpenVPN | `ovpn-<id>` |

## Device Lifecycle

1. **Creation**: `TunDevice` constructor opens `/dev/net/tun` and calls `ioctl(TUNSETIFF)`
2. **Usage**: The fd is passed to the VPN client (WgacClient or OpenVPN3)
3. **Cleanup**: Destructor closes the fd, the kernel removes the interface

## Prerequisites

The `/dev/net/tun` device must exist on the system. If missing, create it:

```bash
sudo mkdir -p /dev/net
sudo mknod /dev/net/tun c 10 200
sudo chmod 600 /dev/net/tun
```

The `grant-requirements` CLI command handles this automatically.

## Permissions

Creating TUN devices requires `CAP_NET_ADMIN`. The service file grants this via:

```ini
Capabilities=CAP_NET_ADMIN+ep
```

## Source Files

- `src/core/tun_device.hpp`
