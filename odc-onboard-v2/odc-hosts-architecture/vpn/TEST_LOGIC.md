# VPN test logic

![Test logic](diagrams/rendered/vpn-test-logic.svg)

## Result model

| Result | Meaning |
|--------|---------|
| **PASS** | All mandatory assertions for the case succeeded within timeout |
| **FAIL** | Prerequisites present but behavior incorrect |
| **SKIP** | Declared dependency missing (hub, protocol, SIM/WAN Variant A) |

Same aggregate format as [`../TEST_SUITE_OVERVIEW.md`](../TEST_SUITE_OVERVIEW.md).

## Decision gates (every connect case)

Evaluate in order; first matching terminal result wins.

1. **Hub available?**  
   - No listener / SSH hub fail after lab claimed hub up for suite → if hub was never provisioned: **SKIP**; if hub was started then died: **FAIL**.
2. **Fixture isolation OK?**  
   - MAV/RTSP present in active graph → **FAIL** (`VPN-MX-02`).
3. **WAN for this variant OK?**  
   - Variant B: cannot reach hub LAN IP → **FAIL**.  
   - Variant A: no cellular WAN → **SKIP**.
4. **Start succeeded?**  
   - No connected / no `ip_ready` before timeout → **FAIL**.
5. **Data plane OK?**  
   - Overlay ping fails while status says connected → **FAIL**.
6. **Hub peer OK?**  
   - Client connected but hub has no peer / stale handshake → **FAIL**.
7. **Reconnect OK?** (reconnect cases only)  
   - Second `ip_ready` missing → **FAIL**.
8. Else → **PASS**.

## Assertion catalog

| ID | Assertion | Evidence |
|----|-----------|----------|
| A-REACH | Outer reach hub endpoint | `ping` / `nc -uz` |
| A-START | Component start returns OK | IPC/CLI |
| A-BIND | `ip_ready` or status connected | Event log / status JSON |
| A-IFACE | Tunnel iface exists with IPv4/IPv6 | `ip addr` |
| A-PING | Overlay ping ≥ N replies | ping output |
| A-PEER | Hub lists peer / handshake fresh | hub `wg show` / OVPN status |
| A-DOWN | After stop: disconnected / iface down | status + `ip link` |
| A-REUP | After restart: A-BIND + A-PING again | same as connect |
| A-ISO | No MAV/RTSP live | process/port/graph list |
| A-DUAL | Two peers concurrent; independence | hub peer list + per-DUT status |

## Flake control

| Risk | Mitigation |
|------|------------|
| Stale iface from prior run | Teardown before start; FAIL if leftover blocks bind |
| Clock / handshake “recent” ambiguity | Require handshake age &lt; 180 s after connect |
| Ping blocked by policy | Prefer hub↔client; if ICMP filtered, use documented TCP probe on allowed port |
| Dual race | Start A, wait A-PEER, start B, wait both |

## Mapping assertions → cases

| Case | Required assertions |
|------|---------------------|
| VPN-P01 | A-REACH (hub) |
| VPN-P02 | tools/binary present |
| VPN-P03 | fixture applied + A-ISO |
| VPN-WG-01 / VPN-OV-01 | A-START, A-BIND, A-IFACE |
| VPN-WG-02 / VPN-OV-02 | A-PING |
| VPN-WG-03 / VPN-OV-03 | A-DOWN, A-REUP |
| VPN-WG-04 / VPN-OV-04 | A-PEER |
| VPN-MX-01 | A-DUAL |
| VPN-MX-02 | A-ISO |
| VPN-MX-03 | Variant A WAN + connect subset |
