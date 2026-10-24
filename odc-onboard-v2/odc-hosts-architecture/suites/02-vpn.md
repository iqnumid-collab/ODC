# Suite ODC-DEV-VPN — VPN tunnels (isolated)

**Feature under test:** `vpn.wireguard` and `vpn.openvpn`  
**DUTs:** `odc-device-rpi-4b`, `odc-device-rpi-5`  
**Peers:** lab tunnel endpoints on `hexa-five`  
**WAN:** prefer `cellular.qmi` when proving “VPN over cellular”; allow Ethernet-only WAN for bring-up if documented  
**Deep suite:** topologies, mechanism, architecture, steps, logic, and full case IDs → [`../vpn/`](../vpn/)

![VPN isolated test flow](../diagrams/rendered/suite-vpn.svg)

## Goal

Prove each VPN component can connect, obtain tunnel IP, expose `TransportBinding` fields (`interface_name`, `target_ip`, `ip_ready`), and disconnect cleanly — **without** MAVLink extenders or RTSP compressors.

## Fixture variants

### Variant A — VPN over cellular (preferred)

```yaml
version: 2
components:
  - id: cellular0
    type: cellular.qmi
    enabled: true
    config:
      device: /dev/cdc-wdm0
      iface: wwan0
      auto_connect: true
  - id: vpn_lab_wg
    type: vpn.wireguard
    enabled: true
    config:
      config_path: /etc/odc-onboard/vpn/lab-wg.conf
      wan: cellular0
links: []
triggers: []
```

### Variant B — WireGuard only (eth WAN / no modem)

Same as A without `cellular0` and without `wan:` (or `wan` omitted). Use only when cellular is unavailable; mark report `wan=ethernet`.

### Variant C — OpenVPN isolated

Replace `vpn.wireguard` with `vpn.openvpn` + `config_path` / `creds_path` for lab `.ovpn`.

**Never** include `mavlink.extender` or `rtsp.compressor` in this suite’s fixtures.

## Prerequisites

| ID | Check | On fail |
|----|-------|---------|
| VPN-PRE-1 | Peer config files present on DUT | **SKIP** connect cases |
| VPN-PRE-2 | `hexa-five` tunnel service reachable (or lab WG endpoint) | **SKIP** connect cases |
| VPN-PRE-3 | Variant A: CEL suite preflight / modem ready | **SKIP** Variant A; run B if allowed |
| VPN-PRE-4 | Distinct peer credentials for rpi-4b vs rpi-5 | **FAIL** if shared keys cause clash |

## Cases

### VPN-01 — Component registration

| | |
|--|--|
| **Steps** | Load fixture; `graph.status` shows `vpn_lab_wg` (and `cellular0` if Variant A). |
| **Expect** | Types correct; enabled; validate passes. |

### VPN-02 — WireGuard connect

| | |
|--|--|
| **Steps** | Start VPN component; poll until connected / `ip_ready`. |
| **Expect** | `wg*` (or configured) iface UP; tunnel IPv4 present; binding fields populated in graph status. |
| **Evidence** | `ip -br addr`, `wg show` (if WG), graph binding JSON |
| **Skip** | Server unreachable |

### VPN-03 — Peer visible on server (lab)

| | |
|--|--|
| **Steps** | From `molt`/`hexa-five`, list tunnel peers after DUT connect. |
| **Expect** | This DUT’s peer appears (hostname/IP/key match inventory). |
| **Evidence** | Server CLI / peer list snapshot |

### VPN-04 — Disconnect / reconnect

| | |
|--|--|
| **Steps** | Stop VPN via graph; iface gone or down; start again. |
| **Expect** | Clean teardown; second connect succeeds; no duplicate ifaces. |

### VPN-05 — OpenVPN connect (isolated)

| | |
|--|--|
| **Steps** | Same as VPN-02 with Variant C fixture. |
| **Expect** | `tun*`/`tap*` up; binding `ip_ready`; no MAV/RTSP processes started. |

### VPN-06 — Multi-VPN independence (optional matrix)

| | |
|--|--|
| **Steps** | Two VPN components (e.g. WG + OVPN or two WG peers) **without** service links. |
| **Expect** | Both show independent bindings; no cross-talk of interface names. |
| **Note** | Still no MAV/RTSP — isolation preserved |

### VPN-07 — Restart durability

| | |
|--|--|
| **Steps** | Connected → restart `odc-onboard-v2` → reconnect from persisted YAML. |
| **Expect** | Tunnel recovers; peer reappears on server. |

## Explicit non-goals

- Activating MAVLink extenders on `vpn.connected`  
- Starting RTSP compressors / measuring video bitrate  
- Bonded multi-path routing  

## Pass criteria

- VPN-01 always **PASS** when suite runs  
- At least one of VPN-02 or VPN-05 **PASS** (or both SKIP with infra reason)  
- Zero unexpected **FAIL**; no leftover orphan TUN after teardown  

## Isolation assertion (mandatory)

After connect, process list / graph status must show **no** running `mavlink.extender` or `rtsp.compressor` components.
