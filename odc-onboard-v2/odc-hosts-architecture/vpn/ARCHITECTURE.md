# VPN architecture (lab test view)

![Architecture](diagrams/rendered/vpn-architecture.svg)

## Layers

| Layer | On DUT (`odc-onboard-v2`) | On hub (`hexa-five`) |
|-------|---------------------------|----------------------|
| Config | Minimal fixture YAML: `vpn.wireguard` and/or `vpn.openvpn` | Tunnel server profiles / peer registry |
| Runtime | `GraphRuntime` → VPN graph component | `odc-tunnel-manager` |
| Binding | `TransportBinding` (`ip_ready` / lost) | Peer list / handshake age |
| Kernel | `wg*` or TUN iface + routes | Matching server iface |
| Orchestration | molt applies fixture, starts components, collects IPC | molt queries peer list / ports |

## Client-side building blocks

1. **Fixture YAML** — only VPN components (+ optional cellular for Variant A).
2. **Graph component** — starts/stops the tunnel from config paths under `/etc/odc/.../vpn/` (or deploy package equivalent).
3. **TransportBinding** — signals when the overlay address is usable; suite treats `ip_ready` as connect success gate.
4. **IPC / CLI** — `vpn.status` (or graph component status) for connected flag, iface name, overlay IP.
5. **Isolation** — no `mavlink.*` / `rtsp.*` components in the fixture; suite asserts they are not running.

## Hub-side building blocks

1. **odc-tunnel-manager** — owns WireGuard and/or OpenVPN server listeners.
2. **Peer identity** — distinct keys / certs / AllowedIPs per DUT (`peer-4b`, `peer-5`).
3. **Optional registry** — WebSocket registry may track devices; **VPN suite PASS must not require registry** if tunnel + peer list already prove connectivity.
4. **Ports** — WG typically UDP `51820`; OpenVPN per deployed profile (document actual port in evidence).

## Control vs data plane

| Plane | Path | Used for |
|-------|------|----------|
| Control | molt SSH → DUT graph IPC; molt SSH → hub CLI/status | start/stop, status, peer list |
| Outer data | DUT WAN (eth or wwan) → hub LAN/VPN endpoint | encrypted encapsulating packets |
| Overlay data | DUT tunnel IP ↔ hub tunnel IP | ping / small probe proving tunnel usable |

## Config artifacts (lab)

Typical package layout (paths may vary by install):

| Artifact | Role |
|----------|------|
| `config/vpn/dev.conf` / `remote.conf` | WireGuard client profiles |
| `config/vpn/dev.ovpn` / `remote.ovpn` | OpenVPN client profiles |
| Hub server configs | Managed on hexa-five with tunnel-manager |

Tests reference **profile names** and **suite IDs**, not pasted private keys.

## Failure domains

| Domain | Typical symptom | Suite reaction |
|--------|-----------------|----------------|
| Hub down / no listener | Connect timeout | SKIP if hub never started; FAIL if hub claimed up |
| Wrong peer key / AllowedIPs | Handshake never completes | FAIL |
| WAN down | No outer reachability | FAIL Variant B; SKIP Variant A if cellular missing |
| Binding never fires | Interface up but no `ip_ready` | FAIL |
| Cross-feature leak | MAV/RTSP running | FAIL isolation |
