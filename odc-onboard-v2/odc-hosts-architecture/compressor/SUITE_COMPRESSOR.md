# Suite `ODC-DEV-VID` — deep compressor case catalog

**Suite ID:** `ODC-DEV-VID`  
**Shallow twin:** [`../suites/04-video-rtsp.md`](../suites/04-video-rtsp.md)  
**Diagrams:** [case map](diagrams/rendered/compressor-suite-case-map.svg) · [bridge](diagrams/rendered/compressor-cross-host-bridge.svg) · [matrix](diagrams/rendered/compressor-cross-dut-matrix.svg) · [bridge workflow](diagrams/rendered/compressor-bridge-workflow.svg)

Primary local source: **real RTSP** from `odc-video-source`.  
Primary bridge: **LAN** between molt ↔ DUT and DUT ↔ DUT (no VPN).

![Suite case map](diagrams/rendered/compressor-suite-case-map.svg)

## Preconditions

| ID | Check | On fail |
|----|-------|---------|
| VID-PRE-1 | GStreamer + rtspserver on DUT | SKIP |
| VID-PRE-2 | DUT has `odc-video-source` + `odc-rtsp-monitor` | SKIP |
| VID-PRE-3 | Test media present where source runs | SKIP |
| VID-PRE-4 | Ports 8554/8555 free on hosts that listen | FAIL if stale |
| VID-PRE-5 | Lab L3: molt ↔ DUT eth0 ping; DUT ↔ DUT ping | SKIP bridge set |
| VID-PRE-6 | Bridge source/monitor host has tools (molt and/or hexa) | SKIP XBR that need them |

## Operations cases (local T1)

| ID | Title | PASS when |
|----|-------|-----------|
| VID-OPS-01 | Component registration | `rtsp_lab` in `graph.status`; type/ports match |
| VID-OPS-02 | Source publishes | `:8554` listening; brief connect OK |
| VID-OPS-03 | Compressor start | Running; `:8555` listen; Started event |
| VID-OPS-04 | Monitor throughput | Bitrate/frames > 0 |
| VID-OPS-05 | Codec H.265 | Caps consistent with fixture |
| VID-OPS-06 | Codec H.264 | Caps consistent with H.264 fixture |
| VID-OPS-07 | Passthrough copy | Output codec matches source path |
| VID-OPS-08 | Bitrate band | Monitor BW within tolerance of config |
| VID-OPS-09 | Stop | Output dead; Stopped; daemon alive |
| VID-OPS-10 | Restart | Second start restores OPS-04 |
| VID-OPS-11 | Port conflict | Error state; recover after free |
| VID-OPS-12 | Daemon restart | Re-arm compressor; BW > 0 |
| VID-OPS-13 | Dual instance | Two compressors both BW > 0 |

## Event cases (local T1)

| ID | Title | PASS when |
|----|-------|-----------|
| VID-EVT-01 | ClientConnected | Event after monitor connects |
| VID-EVT-02 | ClientDisconnected | Event after monitor exits |
| VID-EVT-03 | SourceLost | Event after killing video-source |
| VID-EVT-04 | SourceRecovered | Event after source restart; BW > 0 |
| VID-EVT-05 | AbrStepDown | Event + bitrate drop under throttle |
| VID-EVT-06 | AbrStepUp | Event after conditions stabilize |
| VID-EVT-07 | AuthChanged | Event on auth configure; access rules hold |
| VID-EVT-08 | Status metrics | bitrate>0, fps>0, clients≥1 in status |

If IPC does not export RTSP events yet → **SKIP** EVT-* with `events_not_exported`.

## Cross-host bridge cases (T4–T6)

![Cross-host bridge](diagrams/rendered/compressor-cross-host-bridge.svg)

| ID | Title | Topology | PASS when |
|----|-------|----------|-----------|
| VID-XBR-01 | Remote ingest | Source on molt → compressor on DUT | DUT journal/pipeline running; no ingest error; local or remote monitor eventually OK |
| VID-XBR-02 | Remote consume | Monitor on molt → `rtsp://DUT_IP:8555/stream` | BW/frames > 0 on **molt** transcript |
| VID-XBR-03 | Dual-path consumers | Local DUT monitor + molt monitor | **Both** BW > 0 in same window |
| VID-XBR-04 | Cross-DUT consume | Monitor on peer Pi pulls this DUT | Peer monitor BW > 0 |
| VID-XBR-05 | Bidirectional matrix | Each DUT compresses; peer monitors both ways | Both directions BW > 0 |
| VID-XBR-06 | LAN codec/bitrate E2E | Remote monitor caps vs fixture | Codec/resolution/(bitrate band) match across host |
| VID-XBR-07 | Remote client events | Subscribe on DUT; monitor from molt | ClientConnected / ClientDisconnected for remote client |
| VID-XBR-08 | Bridge continuity | Kill/restart **remote** source | SourceLost → SourceRecovered; remote BW restores |

![Cross-DUT matrix](diagrams/rendered/compressor-cross-dut-matrix.svg)

**Mandatory bridge set (suite green):** XBR-01, XBR-02, XBR-03 on each DUT; XBR-04 once per direction (or XBR-05).  
**Extended:** XBR-06..08 (FAIL if prereqs OK but wrong).

## Optional

| ID | Title | SKIP when |
|----|-------|-----------|
| VID-OPT-01 | USB camera source | No camera / no V4L2 path |
| VID-OPT-02 | Hexa viewer | No monitor tools on hexa-five |

## Host matrix execution

| Host | Local mandatory | Bridge mandatory | Extended / optional |
|------|-----------------|------------------|---------------------|
| rpi-4b | PRE + OPS-01..05 + OPS-09..10 + EVT-01..04 + EVT-08 | XBR-01..03 as compressor; XBR-04 as peer monitor | OPS-06..08,11..13; EVT-05..07; XBR-06..08 |
| rpi-5 | same | same | same |
| molt | driver | source for XBR-01; monitor for XBR-02/03/06 | — |
| hexa-five | — | — | OPT-02 |

## Suggested report block

```
host=odc-device-rpi-4b suite=ODC-DEV-VID
  VID-OPS-* PASS
  VID-EVT-* PASS
  VID-XBR-01 PASS
  VID-XBR-02 PASS
  VID-XBR-03 PASS
  VID-XBR-04 PASS peer=rpi-5
host=bridge
  VID-XBR-05 PASS bidirectional
summary: N PASS, 0 FAIL, M SKIP
```

## Relationship to shallow suite

| Shallow (`04-video-rtsp.md`) | Deep (this file) |
|-----------------------------|------------------|
| VID-01…09 | VID-OPS-* / VID-OPT-01 |
| (omitted) | VID-EVT-*, VID-XBR-* |
