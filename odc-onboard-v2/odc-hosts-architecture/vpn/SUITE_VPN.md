# Suite `ODC-DEV-VPN` — deep case catalog

**Suite ID:** `ODC-DEV-VPN`  
**Shallow twin:** [`../suites/02-vpn.md`](../suites/02-vpn.md)  
**Diagrams:** [case map](diagrams/rendered/vpn-suite-case-map.svg) · [test logic](diagrams/rendered/vpn-test-logic.svg)

This catalog is the authoritative deep list. Shallow suite IDs in `02-vpn.md` should remain compatible aliases where they overlap.

## Preconditions

| ID | Check | SKIP if |
|----|-------|---------|
| VPN-P01 | Hub `192.168.10.16` SSH + VPN listener up | Hub not deployed |
| VPN-P02 | DUT has onboard + WG/OVPN client tooling + profiles | Package incomplete |
| VPN-P03 | VPN-only fixture applied; graph loaded | Cannot replace config |

## WireGuard cases (Variant B)

| ID | Title | Steps (summary) | PASS when |
|----|-------|-----------------|-----------|
| VPN-WG-01 | Connect | Start `vpn.wireguard`; wait binding | connected + iface + overlay IP |
| VPN-WG-02 | Overlay ping | Ping hub overlay from DUT (and reverse if allowed) | ≥3 replies |
| VPN-WG-03 | Reconnect | Stop → down → start → up | second connect within timeout |
| VPN-WG-04 | Hub peer | Query tunnel-manager / `wg show` | peer present, handshake fresh |

## OpenVPN cases (Variant B)

| ID | Title | Steps (summary) | PASS when |
|----|-------|-----------------|-----------|
| VPN-OV-01 | Connect | Start `vpn.openvpn`; wait binding | connected + tun + overlay IP |
| VPN-OV-02 | Overlay ping | Ping hub overlay | ≥3 replies |
| VPN-OV-03 | Reconnect | Disconnect / restart component | second connect within timeout |
| VPN-OV-04 | Hub peer | Server status / client list | session listed |

## Matrix cases

| ID | Title | Topology | PASS when |
|----|-------|----------|-----------|
| VPN-MX-01 | Dual client | T4 both Pis | two peers; kill A keeps B |
| VPN-MX-02 | Isolation | any | no MAV extender / RTSP while VPN fixture active |
| VPN-MX-03 | Cellular WAN | T3 | one protocol connect+ping via wwan path |

## Host matrix execution

| Host | Mandatory | Optional |
|------|-----------|----------|
| rpi-4b | P*, WG-*, OV-* (if OVPN profile present), MX-02 | MX-03 |
| rpi-5 | same | MX-03 |
| both | MX-01 | — |
| hexa-five | peer queries for *-04 / MX-01 | — |
| molt | driver only | future odc-hook peer |

## Evidence checklist (per case)

- Timestamp + host + case ID + result  
- Fixture YAML hash or path  
- Component status JSON  
- `ip addr` / `wg show` or OVPN status snippet  
- Ping transcript  
- Hub peer dump  
- Journal excerpt around connect/reconnect  

Archive under `logs/<date>/<host>/ODC-DEV-VPN/<case-id>/` on molt.

## Suggested report block

```
host=odc-device-rpi-5 suite=ODC-DEV-VPN variant=B
  VPN-P01 PASS
  VPN-P02 PASS
  VPN-P03 PASS
  VPN-WG-01 PASS
  VPN-WG-02 PASS
  VPN-WG-03 PASS
  VPN-WG-04 PASS
  VPN-OV-01 PASS
  VPN-OV-02 PASS
  VPN-OV-03 PASS
  VPN-OV-04 PASS
  VPN-MX-02 PASS
  VPN-MX-03 SKIP reason=no_sim
host=matrix
  VPN-MX-01 PASS
summary: 12 PASS, 0 FAIL, 1 SKIP
```

## Relationship to shallow suite

| Shallow (`02-vpn.md`) | Deep (this file) |
|-----------------------|------------------|
| VPN connect smoke | VPN-WG-01 / VPN-OV-01 |
| Peer / status | VPN-*-04 |
| Reconnect | VPN-*-03 |
| (often omitted) | VPN-MX-01 dual, VPN-MX-03 cellular |

Prefer deep IDs in new runners; keep shallow doc as the quick checklist.
