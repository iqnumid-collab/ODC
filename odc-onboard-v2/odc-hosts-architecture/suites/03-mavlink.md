# Suite ODC-DEV-MAV — MAVLink full behaviour

**Feature under test:** `mavlink.router` + `mavlink.extender` (phased)  
**Orchestrator:** `molt` (`192.168.10.10`) — SSH driver + GCS verifier  
**DUTs:** `odc-device-rpi-4b` (`.11`), `odc-device-rpi-5` (`.12`)  
**Real device:** Flight controller on `/dev/ttyACM0` (USB ACM) per DUT  
**VPN peer (Phases C–D):** `hexa-five` (`.16`) tunnel-manager

![MAVLink full suite flow](../diagrams/rendered/suite-mavlink-full.svg)

Isolated router-only flow (Phases A–B): ![suite-mavlink.svg](../diagrams/rendered/suite-mavlink.svg)

## Goal

Prove full MAVLink behaviour on real hardware:

1. **Local router** opens FC UART and serves GCS over TCP/UDP.
2. **Host sharing** — both DUT FCs are visible from **molt** as GCS over lab LAN.
3. **Extender operations** — VPN `TransportBinding` arms `mavlink.extender`; counters and lifecycle work.
4. **Cross-host verify** — molt correlates LAN GCS traffic with extender data-plane while tunnel is up.

Cross-Pi FC mesh (Pi A seeing Pi B’s vehicle) is **out of scope**.

## Sharing model

| Plane | Path | Who verifies |
|-------|------|--------------|
| LAN GCS | FC → `mavlink.router` → TCP `DUT:5760` / UDP `DUT:14550` | molt (`pymavlink` / mavproxy) |
| VPN extender | FC → router → extender UDP client → `target_ip:udp_port` over WG/OVPN to hexa-five | molt via `graph.status` counters + optional tunnel UDP probe |

## Fixtures

| Phase | File | Components |
|-------|------|------------|
| A–B | [`../fixtures/mav-router-only.yaml`](../fixtures/mav-router-only.yaml) | `mavlink.router` only |
| C–D | [`../fixtures/mav-router-vpn-extender.yaml`](../fixtures/mav-router-vpn-extender.yaml) | `vpn.wireguard` + `mavlink.router` + `mavlink.extender` + links/triggers |

Install path: `/etc/odc-onboard/system.yaml` (backup first: `/etc/odc-onboard/system.yaml.bak.mav`).  
Per-DUT WG peer path defaults to `$ODC_WG_CONFIG` (see env knobs).

### Implementation note (extender data plane)

Extender opens a UDP **client** toward `target_ip:udp_port` (local tunnel IPv4 from VPN binding). Primary router keeps LAN TCP `5760` / UDP `14550`. Assert extender health via `graph.status` fields `active`, `target_ip`, `messages_in`, `messages_out` — not by stealing primary ports.

## Environment knobs

| Variable | Default | Meaning |
|----------|---------|---------|
| `ODC_MAVLINK_DEVICE` | `/dev/ttyACM0` | FC UART on DUT |
| `ODC_MAVLINK_BAUD` | `1500000` | Primary baud (fallback `115200` in fixture) |
| `ODC_MAVLINK_TCP_PORT` | `5760` | Primary TCP GCS |
| `ODC_MAVLINK_UDP_PORT` | `14550` | Primary UDP GCS / extender peer port |
| `ODC_WG_CONFIG` | `/etc/odc-onboard/vpn/lab-wg.conf` | Per-DUT WireGuard peer vs hexa-five |
| `ODC_DUT_IPS` | `192.168.10.11,192.168.10.12` | Matrix targets from molt |

See also [`HOST_ATTACHMENTS.md`](../HOST_ATTACHMENTS.md).

## Prerequisites

| ID | Check | On fail |
|----|-------|---------|
| MAV-PRE-1 | `test -c $ODC_MAVLINK_DEVICE` on DUT | **SKIP** that DUT |
| MAV-PRE-2 | `systemctl is-active odc-onboard-v2`; socket exists | **FAIL** |
| MAV-PRE-3 | Ports 5760 / 14550 free on DUT | **FAIL** if occupied by stale process |
| MAV-PRE-4 | hexa-five reachable + `$ODC_WG_CONFIG` present | **SKIP** Phases C–D only |

## Phase A — Local router (real FC)

Apply `mav-router-only.yaml`. No VPN / RTSP / cellular.

### MAV-A01 — UART open / baud fallback

| | |
|--|--|
| **Steps** | Start `mav_primary`; watch journal for UART open. |
| **Expect** | Router running; baud `1500000` or fallback `115200` succeeds. |
| **Evidence** | journal, `graph.status` for `mav_primary` |

### MAV-A02 — Heartbeats via TCP from molt

| | |
|--|--|
| **Steps** | On molt: `scripts/lab-mav-heartbeat-probe.py tcp DUT_IP 5760`; wait for HEARTBEAT. |
| **Expect** | ≥1 heartbeat within timeout; record `system_id`. |
| **Evidence** | molt probe log |
| **Note** | Graph mode: use `--json` / `graph status` for `endpoint_count` / `connected_count`. Legacy `mavlink endpoints` CLI may be empty. If TCP listens but no HEARTBEAT, check FC baud / dialect (fixture tries `115200`, `1500000`, `57600`). |

### MAV-A03 — Heartbeats via UDP from molt

| | |
|--|--|
| **Steps** | On molt: UDP to `DUT_IP:14550` (server mode). |
| **Expect** | Frames received. |

### MAV-A04 — Endpoint counters

| | |
|--|--|
| **Steps** | `mavlink endpoints` (or graph IPC) before/after A02. |
| **Expect** | `messages_in` / `messages_out` increase on UART and/or GCS endpoints. |

### MAV-A05 — Routing stats

| | |
|--|--|
| **Steps** | `mavlink stats`. |
| **Expect** | `total_routed` (or equivalent) > 0. |

### MAV-A06 — Stop / start without unplug

| | |
|--|--|
| **Steps** | `graph components stop mav_primary`; start again; re-check A02. |
| **Expect** | Recover without unplugging FC. |

### MAV-A07 — Daemon restart

| | |
|--|--|
| **Steps** | `systemctl restart odc-onboard-v2` (prefer kill if stop hangs); wait; A02. |
| **Expect** | UART re-acquired; traffic resumes. |

### MAV-A08 — Wrong device path (negative)

| | |
|--|--|
| **Steps** | Point config at `/dev/ttyACM9`; start component. |
| **Expect** | Error state; daemon stays up. Restore good path after. |

## Phase B — Host sharing (molt GCS to both Pis)

Still fixture A. Run matrix in parallel when both FCs attached.

### MAV-B01 — Parallel LAN GCS

| | |
|--|--|
| **Steps** | molt opens TCP to `.11:5760` and `.12:5760` concurrently. |
| **Expect** | Both receive heartbeats; `system_id` values are distinct when FCs differ. |

### MAV-B02 — Sustained stream

| | |
|--|--|
| **Steps** | Hold both sessions ~30s; poll counters. |
| **Expect** | No router crash; counters monotonic. |

### MAV-B03 — Isolation

| | |
|--|--|
| **Steps** | `graph.status` lists only `mav_primary`. |
| **Expect** | No `vpn.*` / `rtsp.*` / `cellular.*` running under fixture A. |

## Phase C — Extender operations (VPN path)

Requires MAV-PRE-4. Apply `mav-router-vpn-extender.yaml`. Prefer Ethernet WAN for VPN bring-up unless cellular already up from suite `01`.

### MAV-C01 — VPN connected + ip_ready

| | |
|--|--|
| **Steps** | Start `vpn_lab`; poll until connected / `ip_ready`. |
| **Expect** | Tunnel iface UP; binding has `target_ip`. |
| **Skip** | Server unreachable |

### MAV-C02 — Extender arms on trigger

| | |
|--|--|
| **Steps** | Confirm `vpn.connected` activates `lab_mavlink` link. |
| **Expect** | Extender `active=true`; `target_ip` matches tunnel IPv4; `udp_port=14550`. |

### MAV-C03 — Extender counters while FC flows

| | |
|--|--|
| **Steps** | Keep UART heartbeats; sample extender `messages_in`/`messages_out` over 10–15s. |
| **Expect** | Counters increase; primary LAN GCS still works. |

### MAV-C04 — VPN disconnect deactivates extender

| | |
|--|--|
| **Steps** | Stop/disconnect VPN; wait for `vpn.disconnected` trigger. |
| **Expect** | Extender inactive / stopped; link deactivated. |

### MAV-C05 — VPN reconnect re-arms

| | |
|--|--|
| **Steps** | Reconnect VPN without daemon restart. |
| **Expect** | Extender re-arms (`active=true`) with new or same `target_ip`. |

### MAV-C06 — Manual extender stop/start

| | |
|--|--|
| **Steps** | While VPN up: `graph components stop mav_ext_lab`; start again. |
| **Expect** | Stop clears activity; start + binding restores counters. |

### MAV-C07 — No bind without ip_ready (negative)

| | |
|--|--|
| **Steps** | Configure extender `auto: false` or withhold binding; observe status. |
| **Expect** | Extender does **not** claim `active` with a tunnel IP until `ip_ready`. **FAIL** if it binds early. |

### MAV-C08 — Dual-DUT independent extenders

| | |
|--|--|
| **Steps** | Run C01–C03 on both Pis in parallel. |
| **Expect** | Distinct tunnel IPs; both extenders active independently. |

## Phase D — Cross-host verification from molt

### MAV-D01 — LAN GCS while extender active

| | |
|--|--|
| **Steps** | With extender active, molt TCP to `DUT:5760`. |
| **Expect** | Heartbeats; primary still owns TCP 5760 (extender must not steal it). |

### MAV-D02 — MAVLink over VPN data path

| | |
|--|--|
| **Steps** | From molt, UDP toward DUT tunnel IP `:14550` (or hexa-side documented peer route). |
| **Expect** | Heartbeats with same `system_id` as Phase A when reachable. |
| **Skip** | No route from molt into tunnel subnet |

### MAV-D03 — Correlate LAN and extender counters

| | |
|--|--|
| **Steps** | During D01 window, snapshot primary endpoint counters and extender counters. |
| **Expect** | Both increase in the same interval. |

### MAV-D04 — Clean teardown

| | |
|--|--|
| **Steps** | Stop extenders → stop VPN → stop router (or restore bak YAML + restart). |
| **Expect** | Clean journal; no hung stop (lab note: `systemctl kill -s SIGKILL` if stop-sigterm times out). |

## Explicit non-goals

- Cross-Pi vehicle mesh  
- Cellular-only WAN required for MAV (Ethernet VPN OK)  
- RTSP / video  
- Full production credential sets  

## Pass criteria

| Phases | When FC attached | When VPN preflight OK |
|--------|------------------|------------------------|
| A–B | **PASS** required on each DUT with FC | — |
| C–D | — | **PASS** required; else **SKIP** with reason |

**FAIL** examples: daemon crash; extender `active` without `ip_ready`; counters stuck with FC present; TCP 5760 dead while service claims running.

## Dual-board matrix

Each Pi must have its **own** FC. Sharing one USB FC across both hosts is not supported for parallel runs.

## Evidence to archive on molt

Under `logs/<date>/<host>/ODC-DEV-MAV/`:

| Artifact | Source |
|----------|--------|
| `system.yaml.fixture` | Applied fixture |
| `graph.status.json` | Start / mid / end |
| `mavlink-endpoints.txt` / `mavlink-stats.txt` | CLI |
| `pymavlink-lan.log` | Phase A/B/D LAN probes |
| `journal-odc-onboard-v2.txt` | journalctl |
| `tunnel-ip.txt` | Phase C binding / `ip -br addr` |

## Runner

Implemented on molt: [`../../../scripts/lab-run-mav-suite.sh`](../../../scripts/lab-run-mav-suite.sh)

```bash
./scripts/lab-run-mav-suite.sh              # Phases A–B both DUTs
./scripts/lab-run-mav-suite.sh --with-vpn   # + Phases C–D (hexa-five + WG peer)
ODC_DUT_FILTER=192.168.10.11 ./scripts/lab-run-mav-suite.sh
```

Heartbeat probe (no pymavlink): [`../../../scripts/lab-mav-heartbeat-probe.py`](../../../scripts/lab-mav-heartbeat-probe.py).  
Logs default to `logs/mav-suite/<timestamp>/`.

## Implementation refs

- [`../../src/graph/components/mav_router_component.cpp`](../../src/graph/components/mav_router_component.cpp)  
- [`../../src/graph/components/mav_extender_component.cpp`](../../src/graph/components/mav_extender_component.cpp)  
- CLI: `graph status`, `mavlink endpoints`, `mavlink stats`  
- Related: suite [`02-vpn.md`](02-vpn.md) for tunnel peers  
