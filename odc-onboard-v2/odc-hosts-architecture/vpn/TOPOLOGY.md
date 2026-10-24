# VPN testing topologies

![Lab topology](diagrams/rendered/vpn-lab-topology.svg)

## T1 — Star hub (primary)

**Shape:** `hexa-five` is the only VPN server. Each DUT is an independent client peer.

```
rpi-4b ──WG/OVPN──► hexa-five ◄──WG/OVPN── rpi-5
                         ▲
                         │ SSH / evidence
                       molt
```

| Property | Value |
|----------|-------|
| Overlay model | Point-to-hub (star) |
| Client count | 1 or 2 concurrent |
| Control plane | molt → SSH/IPC to DUTs + hub |
| Data plane under test | DUT overlay IP ↔ hub overlay IP |

**Why this topology:** matches production “device dials into ground station / tunnel manager” and allows dual-client without coupling Pis to each other.

## T2 — WAN Variant B (ethernet first)

DUT uses **lab eth0** (`192.168.10.0/24`) as upstream. Outer packets to hub `192.168.10.16` travel on the LAN.

Use for: all mandatory WG/OVPN cases. Lowest flake rate; no SIM dependency.

## T3 — WAN Variant A (cellular)

DUT uses **wwan0** as default route (or policy route) so handshake packets leave via cellular while lab SSH may still use eth for orchestration.

Use for: optional `VPN-MX-03` after cellular suite PASS. SKIP if no SIM / no `wan_ready`.

![WAN variants](diagrams/rendered/vpn-wan-variants.svg)

## T4 — Dual-client concurrent

Both DUTs connect at once with distinct peer identities (keys / CN / AllowedIPs).

![Dual client](diagrams/rendered/vpn-dual-client.svg)

Assertions:

1. Hub lists **two** live peers.
2. Each DUT gets its own overlay IP / iface.
3. Stopping peer A does **not** drop peer B.

## T5 — Protocol split (same topology, different transport)

| Protocol | Typical outer | Client config on DUT | Hub |
|----------|---------------|----------------------|-----|
| WireGuard | UDP `51820` | `vpn.wireguard` / `*.conf` | tunnel-manager WG |
| OpenVPN | UDP/TCP per server profile | `vpn.openvpn` / `*.ovpn` | tunnel-manager OVPN |

Run WG and OVPN as **separate case groups** (do not require both tunnels up on the same DUT at once unless a future fixture explicitly tests that).

## Out of scope topologies

| Topology | Why deferred |
|----------|--------------|
| Mesh Pi↔Pi VPN | Not product path for this suite |
| molt as VPN peer (odc-hook) | Optional later; hub remains source of truth |
| Full registry + hook E2E | Documented elsewhere; not required for tunnel PASS |

## Topology selection per case

| Case family | Topology |
|-------------|----------|
| VPN-P* preflight | T1 + hub reachability |
| VPN-WG-* / VPN-OV-* | T1 + T2 |
| VPN-MX-01 dual | T1 + T4 + T2 |
| VPN-MX-03 cellular WAN | T1 + T3 |
| VPN-MX-02 isolation | Any active VPN fixture; assert no MAV/RTSP |
