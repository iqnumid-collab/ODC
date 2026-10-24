# VPN lab test suite (deep)

**Status:** design only (markdown + diagrams; no automation executed from this folder).  
**Scope:** feature-isolated VPN testing for `odc-onboard-v2` clients against the `hexa-five` hub — topologies, mechanism, architecture, steps, assertion logic, and case catalog.

![VPN lab topology](diagrams/rendered/vpn-lab-topology.svg)

## Purpose

Expand the shallow smoke suite [`../suites/02-vpn.md`](../suites/02-vpn.md) into a **lab-complete VPN contract**: star topology, WAN variants, dual-client concurrency, connect/reconnect data-plane checks, and isolation from MAV/RTSP.

| Layer | Document | Role |
|-------|----------|------|
| Shallow smoke | [`../suites/02-vpn.md`](../suites/02-vpn.md) | Board-level VPN-01… cases |
| **This folder** | Deep VPN suite | Topology, mechanism, architecture, steps, logic, full case IDs |

Run the shallow suite for quick tunnel health; use this folder when validating hub/client matrix and WAN variants.

## Host matrix

| Role | Host | IP | SSH | Responsibility |
|------|------|----|-----|----------------|
| Driver | `odc-host-molt` | `192.168.10.10` | local | Orchestrate, apply fixtures, archive evidence; optional `odc-hook` later |
| Hub | `odc-server-hexa-five` | `192.168.10.16` | `hexa-five` / `hexa` | `odc-tunnel-manager` (WG + OpenVPN); optional registry |
| DUT A | `odc-device-rpi-4b` | `192.168.10.11` | `odc-device-rpi-4b` | Independent VPN client peer |
| DUT B | `odc-device-rpi-5` | `192.168.10.12` | `odc-device-rpi-5` | Independent VPN client peer |

Star topology: **both Pis peer only to hexa-five**. Device-to-device overlay traffic is out of scope unless the hub explicitly routes it (not required for this suite).

## Document map

| File | Contents |
|------|----------|
| [TOPOLOGY.md](TOPOLOGY.md) | Lab topologies and WAN variants |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Client / hub / binding architecture |
| [MECHANISM.md](MECHANISM.md) | Handshake, binding events, data plane |
| [STEPS.md](STEPS.md) | Ordered bring-up and teardown steps |
| [TEST_LOGIC.md](TEST_LOGIC.md) | PASS / FAIL / SKIP decision rules |
| [SUITE_VPN.md](SUITE_VPN.md) | Full case catalog + evidence |
| [diagrams/](diagrams/) | Mermaid sources + rendered SVGs |

## Isolation policy

Inherited from [`../TEST_SUITE_OVERVIEW.md`](../TEST_SUITE_OVERVIEW.md):

- Fixture YAML contains **only** `vpn.wireguard` and/or `vpn.openvpn` (plus optional `cellular.qmi` as WAN for Variant A).
- Forbidden while running: MAV extender, RTSP compressor, cross-feature triggers.
- Missing hub / tunnel-manager → **SKIP**; wrong behavior with hub present → **FAIL**.
- Credentials and private keys stay in host configs / deploy packages — never paste production secrets into these docs.

## Recommended order

```
ATT preflight → hub alive → Variant B (eth WAN) WG → Variant B OVPN
  → dual-client concurrent → (optional) Variant A cellular WAN → teardown
```

Cellular Variant A requires prior `ODC-DEV-CEL` PASS (or equivalent WAN up). See [qmi/](../qmi/) for deep cellular.

## Diagrams

| Diagram | File |
|---------|------|
| Lab topology | [rendered/vpn-lab-topology.svg](diagrams/rendered/vpn-lab-topology.svg) |
| Architecture | [rendered/vpn-architecture.svg](diagrams/rendered/vpn-architecture.svg) |
| Mechanism sequence | [rendered/vpn-mechanism.svg](diagrams/rendered/vpn-mechanism.svg) |
| WAN variants | [rendered/vpn-wan-variants.svg](diagrams/rendered/vpn-wan-variants.svg) |
| Dual client | [rendered/vpn-dual-client.svg](diagrams/rendered/vpn-dual-client.svg) |
| Suite case map | [rendered/vpn-suite-case-map.svg](diagrams/rendered/vpn-suite-case-map.svg) |
| Test logic | [rendered/vpn-test-logic.svg](diagrams/rendered/vpn-test-logic.svg) |

## Related paths

| Area | Path |
|------|------|
| Shallow suite | [`../suites/02-vpn.md`](../suites/02-vpn.md) |
| Suite overview | [`../TEST_SUITE_OVERVIEW.md`](../TEST_SUITE_OVERVIEW.md) |
| Deploy VPN configs | [`../../../dist/aarch64/odc-onboard/config/vpn/`](../../../dist/aarch64/odc-onboard/config/vpn/) |
| Lab topology (wider) | [`../../../docs-topology/COMMUNICATION.md`](../../../docs-topology/COMMUNICATION.md) |
| Hub services | [`../../../docs-topology/SERVICES.md`](../../../docs-topology/SERVICES.md) |
