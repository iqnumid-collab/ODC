# VPN mechanism under test

![Mechanism](diagrams/rendered/vpn-mechanism.svg)

## Happy-path sequence

1. **Fixture replace** — install VPN-only YAML on DUT; restart or reload graph as the lab runner defines.
2. **WAN ready** — Variant B: ping/route to `192.168.10.16`. Variant A: cellular `wan_ready` / default via `wwan0`.
3. **Start component** — `graph.components.start` (or equivalent) for the VPN node id (e.g. `vpn_lab`).
4. **Handshake** — WireGuard key exchange or OpenVPN TLS + channel setup toward hub.
5. **Binding** — component publishes `TransportBinding` / `ip_ready` with overlay address.
6. **Hub observe** — tunnel-manager peer list shows DUT peer with recent handshake.
7. **Data plane** — ICMP (or small TCP probe) DUT overlay IP ↔ hub overlay IP.
8. **Reconnect** — stop/disconnect → assert `ip_lost` / disconnected → start again → `ip_ready` within timeout.
9. **Teardown** — stop component, remove TUN/WG iface, restore previous YAML.

## WireGuard specifics

| Check | Mechanism |
|-------|-----------|
| Connect | Interface up, peer handshake `latest handshake` recent |
| Overlay | Assigned address in AllowedIPs / Address field |
| Reconnect | `wg-quick down/up` or component restart clears and re-handshakes |
| Dual client | Distinct public keys; hub AllowedIPs do not collide |

## OpenVPN specifics

| Check | Mechanism |
|-------|-----------|
| Connect | TUN up, client log shows `Initialization Sequence Completed` (or component status connected) |
| Overlay | `ifconfig` / status IP from server pool |
| Reconnect | SIGUSR1 or component restart |
| Dual client | Distinct certs/CN; server `duplicate-cn` policy must allow two devices if using shared cert (prefer unique certs) |

## Events the suite cares about

| Event / signal | Meaning |
|----------------|---------|
| `ip_ready` | Overlay usable — gate for ping cases |
| `ip_lost` / disconnect | Expected after stop; must appear before reconnect PASS |
| Hub peer present | Server-side confirmation (not only client optimism) |
| No MAV/RTSP | Isolation — mechanism of suite hygiene, not VPN protocol |

## Timeouts (design defaults)

| Gate | Suggested timeout |
|------|-------------------|
| Outer reach hub | 5 s |
| Connect → `ip_ready` | 30–60 s |
| Overlay ping | 10 s, ≥3 replies |
| Reconnect cycle | 60–90 s |
| Dual peer both up | 90 s |

Adjust per lab; record actual values in evidence JSON/log header.

## Negative mechanisms (expected FAIL or SKIP)

| Condition | Expected |
|-----------|----------|
| Hub listener closed while fixture claims connect | FAIL connect |
| Wrong server endpoint in client profile | FAIL connect |
| Cellular WAN without SIM | SKIP Variant A |
| Both protocols forced up with conflicting default routes | Avoid; run sequentially |
