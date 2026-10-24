# Feature: OpenVPN Connection

## Overview

odc-onboard supports OpenVPN connections using an embedded OpenVPN3 client. It implements the full TUN builder interface to configure network interfaces after the TLS handshake completes.

## Configuration

OpenVPN connections use standard `.ovpn` files with inline certificates:

```
client
proto udp
remote 51.178.82.148 1194
dev tun
cipher AES-128-GCM
auth SHA256
tls-cipher TLS-ECDHE-ECDSA-WITH-AES-128-GCM-SHA256
tls-version-min 1.2

<ca>
-----BEGIN CERTIFICATE-----
...
-----END CERTIFICATE-----
</ca>

<cert>
-----BEGIN CERTIFICATE-----
...
-----END CERTIFICATE-----
</cert>

<key>
-----BEGIN PRIVATE KEY-----
...
-----END PRIVATE KEY-----
</key>

<tls-crypt>
-----BEGIN OpenVPN Static key V1-----
...
-----END OpenVPN Static key V1-----
</tls-crypt>
```

### Application Config Entry

```json
{
  "vpn": {
    "openvpn": {
      "connections": [
        {
          "id": "my-ovpn",
          "enabled": true,
          "config_path": "/etc/odc-onboard/vpn/my-ovpn.ovpn",
          "credentials_path": "/etc/odc-onboard/vpn/my-ovpn.creds.enc",
          "metadata": {
            "name": "My OpenVPN",
            "auto_start": true,
            "max_retries": 5,
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

1. Create a TUN device named `ovpn-<id>` via `/dev/net/tun`
2. Load the `.ovpn` configuration
3. Load credentials (encrypted or plaintext)
4. Initialize the OpenVPN3 client
5. Begin the TLS handshake

### TUN Builder

When the server pushes network configuration, the TUN builder callbacks are invoked:

| Callback | Purpose |
|----------|---------|
| `tun_builder_set_layer` | Set OSI layer (typically 3) |
| `tun_builder_add_address` | Add IPv4/IPv6 address with prefix length |
| `tun_builder_add_route` | Add routing entries |
| `tun_builder_exclude_route` | Add excluded routes |
| `tun_builder_reroute_gw` | Configure default gateway rerouting |
| `tun_builder_set_remote_address` | Record the server address |
| `tun_builder_set_mtu` | Set interface MTU |
| `tun_builder_set_session_name` | Name the VPN session |
| `tun_builder_add_proxy_bypass` | Configure proxy bypass hosts |
| `tun_builder_teardown` | Clean up on disconnect |

After configuration is received, NetlinkManager applies:
- IP addresses to the TUN interface
- Routing entries
- MTU settings
- Default gateway (if reroute is enabled)
- Interface UP state

### Connected

- Data flows through the TUN interface
- Stats collected every 5 seconds
- Health checks run at configured interval

### Events

| Event | Action |
|-------|--------|
| `CONNECTED` | Mark state as Connected, start health checks |
| `DISCONNECTED` | Mark state as Disconnected, trigger reconnect if retries remain |
| `RECONNECTING` | Mark state as Reconnecting |
| Fatal error | Mark state as Error, log the error message |

### Disconnect

1. Signal the OpenVPN3 client to disconnect
2. Bring the TUN interface down
3. Clean up the TUN device

## Interface Naming

The OpenVPN interface is named `ovpn-<id>` where `<id>` is the connection ID.

## Credential Handling

OpenVPN credentials (username/password) are loaded from:
1. Encrypted file (`.creds.enc`) with key file (`.creds.enc.key`)
2. Plaintext file (fallback if no key file exists)

Format:
```
username
password
```

## Health Check

Same ICMP ping mechanism as WireGuard:
- Pings `target_ip` at `interval_sec` intervals
- Tracks latency and consecutive failures
- Auto-restarts on `max_retries` failures

## Connection Timeout

Default 30-second timeout during the connection phase.

## Source Files

- `src/vpn/openvpn_connection.hpp`
- `src/vpn/vpn_connection.hpp` (base class)
- `src/core/tun_device.hpp`
- `thirdparty/minimal-openvpn3/` (embedded OpenVPN3 client)
