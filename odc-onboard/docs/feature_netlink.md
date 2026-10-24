# Feature: Netlink Manager

## Overview

The NetlinkManager provides low-level Linux netlink socket communication for configuring network interfaces. It is used to add IP addresses, configure routes, set MTU, and bring interfaces up/down without relying on external tools like `ip` or `ifconfig`.

## Implementation

Uses the `NETLINK_ROUTE` socket family to send and receive netlink messages directly. All operations construct raw netlink messages with `nlmsghdr` headers and parse responses for errors.

## API

### `add_address(iface, ip, prefix_len, ipv6)`

Adds an IP address to a network interface.

```cpp
netlink.add_address("wg-myvpn", "10.66.66.3", 32, false);
```

### `add_route(iface, dst, prefix_len, gw, ipv6)`

Adds a routing entry.

```cpp
netlink.add_route("wg-myvpn", "0.0.0.0", 0, "10.66.66.1", false);
```

### `delete_route(iface, dst, prefix_len, gw, ipv6)`

Removes a routing entry.

```cpp
netlink.delete_route("wg-myvpn", "0.0.0.0", 0, "10.66.66.1", false);
```

### `set_mtu(iface, mtu)`

Sets the Maximum Transmission Unit for an interface.

```cpp
netlink.set_mtu("wg-myvpn", 1420);
```

### `bring_interface_up(iface)`

Sets the `IFF_UP` flag on an interface.

```cpp
netlink.bring_interface_up("wg-myvpn");
```

### `bring_interface_down(iface)`

Clears the `IFF_UP` flag on an interface.

```cpp
netlink.bring_interface_down("wg-myvpn");
```

## Usage in VPN Connections

### WireGuard Start Sequence

```
add_address(iface, config.address, prefix_len, false)
bring_interface_up(iface)
add_route(iface, "0.0.0.0", 0, peer_endpoint_ip, false)
```

### OpenVPN TUN Builder Sequence

```
add_address(iface, pushed_address, pushed_prefix, ipv6)
add_route(iface, pushed_route, pushed_prefix, gateway, ipv6)
set_mtu(iface, pushed_mtu)
bring_interface_up(iface)
```

## Prerequisites

Requires `CAP_NET_ADMIN` capability.

## Error Handling

Netlink responses are parsed and errors are reported with descriptive messages. Failed operations return `false` and log the error via spdlog.

## Source Files

- `src/core/netlink_manager.hpp`
