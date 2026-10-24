# RTSP compressor lab suite (deep)

**Status:** runner live — `scripts/lab-run-video-suite.sh` (reports under `logs/video-suite/<ts>/`).  
**Latest:** [LAST_REPORT.md](LAST_REPORT.md) · [SKIP_FAIL_TRACKER.md](SKIP_FAIL_TRACKER.md)  
**Scope:** feature-isolated `rtsp.compressor` testing on `odc-onboard-v2` DUTs with **real RTSP** ingest and monitor validation — including **LAN cross-host bridges** that prove streams and data planes work between hosts.

![Lab topology](diagrams/rendered/compressor-lab-topology.svg)

## Purpose

Expand the shallow suite [`../suites/04-video-rtsp.md`](../suites/04-video-rtsp.md) into a **host-complete compressor contract**:

1. Per-DUT loopback ops + events (isolates encode/republish).  
2. **Cross-host bridging** (source / compressor / monitor on different lab hosts over `192.168.10.0/24`).  
3. Dual-DUT peer consume matrix.

Without VPN, MAVLink, or cellular tunnels — bridging uses **lab ethernet only**.

| Layer | Document | Role |
|-------|----------|------|
| Shallow smoke | [`../suites/04-video-rtsp.md`](../suites/04-video-rtsp.md) | VID-01…09 board cases |
| **This folder** | Deep compressor suite | Topology, methodology, ops/events/bridge workflows, case map |

## Host matrix

| Role | Host | IP | Responsibility |
|------|------|----|----------------|
| Driver / bridge peer | `odc-host-molt` | `192.168.10.10` | Orchestrate; optional LAN source + remote monitor; archive evidence |
| DUT A | `odc-device-rpi-4b` | `192.168.10.11` | Compressor under test; local and/or remote ingest |
| DUT B | `odc-device-rpi-5` | `192.168.10.12` | Compressor under test; peer consume of DUT A (and reverse) |
| Optional viewer | `odc-server-hexa-five` | `192.168.10.16` | Optional remote `odc-rtsp-monitor` only (no tunnel-manager role) |

![Host matrix](diagrams/rendered/compressor-host-matrix.svg)

## Document map

| File | Contents |
|------|----------|
| [TOPOLOGY.md](TOPOLOGY.md) | Loopback + **cross-host bridge** shapes |
| [METHODOLOGY.md](METHODOLOGY.md) | PASS / FAIL / SKIP + bridge evidence |
| [WORKFLOWS.md](WORKFLOWS.md) | Local ops/events + **bridge workflows** |
| [OPERATIONS.md](OPERATIONS.md) | Lifecycle + local/bridge operation catalog |
| [SUITE_COMPRESSOR.md](SUITE_COMPRESSOR.md) | Case IDs: OPS / EVT / **XBR** / OPT |
| [diagrams/](diagrams/) | Mermaid sources + rendered SVGs |

## Isolation policy

Inherited from [`../TEST_SUITE_OVERVIEW.md`](../TEST_SUITE_OVERVIEW.md):

- Fixture YAML contains **only** `rtsp.compressor` (no VPN / MAV / cellular components).
- **Loopback cases:** synthetic local binding (`lo` / `127.0.0.1`).
- **Bridge cases:** ingest/consume via **lab eth0** IPs; still no WG/OVPN overlays.
- Missing GStreamer / tools / media / peer reachability → **SKIP**; wrong behavior with prereqs → **FAIL**.

## Recommended order

```
ATT video tools
  → VID-PRE (each DUT)
  → VID-OPS + VID-EVT mandatory (loopback, each DUT)
  → VID-XBR mandatory bridges (molt ↔ DUT, then DUT ↔ DUT)
  → optional codec/ABR/auth/dual/hexa viewer
  → teardown ports + restore system.yaml
```

## Diagrams

| Diagram | File |
|---------|------|
| Lab topology | [rendered/compressor-lab-topology.svg](diagrams/rendered/compressor-lab-topology.svg) |
| Host matrix | [rendered/compressor-host-matrix.svg](diagrams/rendered/compressor-host-matrix.svg) |
| DUT dataplane | [rendered/compressor-dut-dataplane.svg](diagrams/rendered/compressor-dut-dataplane.svg) |
| Cross-host bridge | [rendered/compressor-cross-host-bridge.svg](diagrams/rendered/compressor-cross-host-bridge.svg) |
| Cross-DUT matrix | [rendered/compressor-cross-dut-matrix.svg](diagrams/rendered/compressor-cross-dut-matrix.svg) |
| Bridge workflow | [rendered/compressor-bridge-workflow.svg](diagrams/rendered/compressor-bridge-workflow.svg) |
| Ops workflow | [rendered/compressor-ops-workflow.svg](diagrams/rendered/compressor-ops-workflow.svg) |
| Events workflow | [rendered/compressor-events-workflow.svg](diagrams/rendered/compressor-events-workflow.svg) |
| Suite case map | [rendered/compressor-suite-case-map.svg](diagrams/rendered/compressor-suite-case-map.svg) |
| Test methodology | [rendered/compressor-test-methodology.svg](diagrams/rendered/compressor-test-methodology.svg) |
| Lifecycle | [rendered/compressor-lifecycle.svg](diagrams/rendered/compressor-lifecycle.svg) |

## Related paths

| Area | Path |
|------|------|
| Shallow suite | [`../suites/04-video-rtsp.md`](../suites/04-video-rtsp.md) |
| Suite overview | [`../TEST_SUITE_OVERVIEW.md`](../TEST_SUITE_OVERVIEW.md) |
| Attachments | [`../HOST_ATTACHMENTS.md`](../HOST_ATTACHMENTS.md) |
| Tools | [`../../tools/odc-video-source/`](../../tools/odc-video-source/), [`../../tools/odc-rtsp-monitor/`](../../tools/odc-rtsp-monitor/) |
| Events | [`../../src/rtsp/rtsp_events.hpp`](../../src/rtsp/rtsp_events.hpp) |
