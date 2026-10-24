# Suite ODC-DEV-CEL — Cellular WAN (isolated)

**Feature under test:** `cellular.qmi` only  
**DUTs:** `odc-device-rpi-4b`, `odc-device-rpi-5`  
**Real device:** QMI modem (`/dev/cdc-wdm0` → `wwan0`)

![Cellular WAN test flow](../diagrams/rendered/suite-cellular-wan.svg)

> **Deep QMI suite:** for service-level operations, state-machine cases, modem discovery, and the compliance-gap register, see [`../qmi/`](../qmi/). This file remains the shallow board smoke suite.

## Goal

Prove the onboard graph can bring up the modem, attach to the mobile network, expose a usable WAN interface, and report `wan_ready` — **without** starting VPN, MAVLink, or RTSP.

## Fixture (minimal)

```yaml
version: 2
general:
  log_level: info
  socket: /run/odc-onboard/odc-onboard.sock

components:
  - id: cellular0
    type: cellular.qmi
    enabled: true
    config:
      device: /dev/cdc-wdm0
      iface: wwan0
      auto_connect: true
      # apn / pin / credentials: lab-specific

links: []
triggers: []
```

No `vpn.*`, `mavlink.*`, or `rtsp.*` entries.

## Prerequisites

| ID | Check | On fail |
|----|-------|---------|
| CEL-PRE-1 | `ATT-QMI` pass | **SKIP** entire suite |
| CEL-PRE-2 | SIM present / not PIN-locked (lab) | **SKIP** connect cases |
| CEL-PRE-3 | `odc-onboard-v2` active with fixture loaded | **FAIL** if service will not start |

## Cases

### CEL-01 — Modem visible to OS

| | |
|--|--|
| **Steps** | 1. Confirm `/dev/cdc-wdm0` exists. 2. Query manufacturer/model via `qmicli` or ModemManager. |
| **Expect** | Device responds; no I/O error. |
| **Evidence** | `qmicli` stdout |

### CEL-02 — Graph component present

| | |
|--|--|
| **Steps** | 1. Apply fixture; restart daemon. 2. `graph.status` / `graph components list`. |
| **Expect** | Component `cellular0` type `cellular.qmi`, `enabled: true`. |
| **Evidence** | `graph.status.json` |

### CEL-03 — Auto-connect / WAN up

| | |
|--|--|
| **Steps** | 1. Start `cellular0` if not already. 2. Wait up to N seconds for `wwan0` UP + IPv4 (or IPv6). 3. Read component state via graph API. |
| **Expect** | Interface carries address; graph reports WAN ready / connected-equivalent state; journal shows lifecycle success (no crash loop). |
| **Evidence** | `ip addr show wwan0`, journal snippet, graph state |
| **Skip** | No APN / no RF coverage in lab |

### CEL-04 — WAN readiness event semantics

| | |
|--|--|
| **Steps** | After CEL-03, confirm graph/component outputs indicate upstream available for VPN binding (`wan_ready` or documented status field). |
| **Expect** | Stable ready state for ≥30s (no flap). |
| **Evidence** | Timed status samples |

### CEL-05 — Disconnect / reconnect

| | |
|--|--|
| **Steps** | 1. Stop/disconnect cellular component via graph CLI. 2. Confirm `wwan0` loses session or is down. 3. Start again; wait for ready. |
| **Expect** | Clean teardown; reconnect succeeds without daemon restart. |
| **Evidence** | Before/after `ip` + journal |

### CEL-06 — Daemon restart durability (cellular-only)

| | |
|--|--|
| **Steps** | With modem connected, `systemctl restart odc-onboard-v2`; wait for fixture reload + re-connect. |
| **Expect** | Service active; `cellular0` returns to ready without manual YAML edit. |
| **Evidence** | `systemctl status`, graph status |

## Explicit non-goals (this suite)

- Establishing WireGuard/OpenVPN  
- MAVLink UART traffic  
- RTSP ingest/compress  

## Pass criteria (suite green)

- CEL-01, CEL-02 mandatory **PASS** when modem attached  
- CEL-03 **PASS** or documented **SKIP** (coverage/APN)  
- Zero **FAIL**; no daemon coredump  

## Matrix

Run identical case IDs on both Pis; record modem model per host (rpi-4b vs rpi-5 may differ USB controllers).
