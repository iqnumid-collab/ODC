# Suite ODC-DEV-VID — Video / RTSP compressor (isolated)

**Feature under test:** `rtsp.compressor` data plane  
**DUTs:** `odc-device-rpi-4b`, `odc-device-rpi-5`  
**Lab tools:** `odc-video-source`, `odc-rtsp-monitor`  
**Runner:** `scripts/lab-run-video-suite.sh` → `logs/video-suite/<ts>/` (+ [compressor/LAST_REPORT.md](../compressor/LAST_REPORT.md))  
**Optional real device:** USB camera (otherwise file-backed MP4 source)

![Video RTSP isolated test flow](../diagrams/rendered/suite-video-rtsp.svg)

## Goal

Prove the RTSP compressor ingests a stream, transcodes per config (codec/bitrate/resolution), and serves an output that the monitor can measure — **without** live VPN binding, cellular, or MAVLink.

Transport is **localhost / DUT loopback** (or explicit LAN IP of the DUT), not a tunnel interface.

## Fixture (minimal)

```yaml
version: 2
general:
  log_level: info
  socket: /run/odc-onboard/odc-onboard.sock

components:
  - id: rtsp_lab
    type: rtsp.compressor
    enabled: true
    config:
      output_codec: h265
      output_bitrate_kbps: 800
      output_width: 1280
      output_height: 720
      output_framerate: 30
      input_port: 8554
      input_path: /stream
      output_port: 8555
      output_path: /stream
      input_use_tcp: true

links: []
triggers: []
```

**Binding strategy for isolation:** start compressor with an explicit local binding / manual start so it does **not** wait on `vpn.connected`. If the implementation requires a `TransportBinding`, inject a **synthetic local binding** (`interface_name=lo`, `target_ip=127.0.0.1`, `ip_ready=true`) via test helper or graph API — never via a live VPN component in this suite.

## Lab process layout

| Process | Role | URL / port |
|---------|------|------------|
| `odc-video-source` | Publish source RTSP | `rtsp://127.0.0.1:8554/stream` (or DUT IP) |
| `odc-onboard-v2` → `rtsp_lab` | Compress / republish | listen `:8555/stream` |
| `odc-rtsp-monitor` | Consume output; report bandwidth | connect to `:8555` |

## Prerequisites

| ID | Check | On fail |
|----|-------|---------|
| VID-PRE-1 | GStreamer + rtspserver available | **SKIP** |
| VID-PRE-2 | `/opt/odc/bin/odc-video-source` and `odc-rtsp-monitor` | **SKIP** |
| VID-PRE-3 | Test media file present **or** V4L2 camera | **SKIP** if neither |
| VID-PRE-4 | Ports 8554/8555 free | **FAIL** if stale listeners |

## Cases

### VID-01 — Component registration

| | |
|--|--|
| **Steps** | Load fixture; verify `rtsp_lab` in `graph.status`. |
| **Expect** | Type `rtsp.compressor`; config ports match fixture. |

### VID-02 — Source publishes

| | |
|--|--|
| **Steps** | Start `odc-video-source` on input port/path. |
| **Expect** | Source listening; optional `gst-discoverer` / brief client connect succeeds. |

### VID-03 — Compressor start (local binding)

| | |
|--|--|
| **Steps** | Start `rtsp_lab` with local binding; confirm output port listening. |
| **Expect** | Compressor running; journal shows pipeline start. |

### VID-04 — Monitor bandwidth > 0

| | |
|--|--|
| **Steps** | Run `odc-rtsp-monitor` against output URI for T seconds. |
| **Expect** | Reported bitrate/frame count **> 0**. |
| **Evidence** | Monitor stdout / JSON metrics |

### VID-05 — Codec / resolution honor config

| | |
|--|--|
| **Steps** | Inspect output caps (monitor or `gst-discoverer-1.0` on output URI). |
| **Expect** | Codec/resolution consistent with fixture (allow documented encoder tolerances). |

### VID-06 — Stop / restart compressor

| | |
|--|--|
| **Steps** | Stop component; monitor fails or bandwidth→0; start again; VID-04. |
| **Expect** | Clean stop; restart without daemon restart. |

### VID-07 — Port conflict negative

| | |
|--|--|
| **Steps** | Occupy 8555 with a dummy listener; start compressor. |
| **Expect** | Error state; daemon remains active. Free port and recover. |

### VID-08 — Daemon restart with tools still up

| | |
|--|--|
| **Steps** | Restart `odc-onboard-v2` only; re-start compressor; monitor again. |
| **Expect** | Pipeline recovers; bandwidth > 0. |

### VID-09 — Optional USB camera source

| | |
|--|--|
| **Steps** | Replace file source with V4L2 device in video-source args. |
| **Expect** | Same as VID-04. |
| **Skip** | No camera |

## Explicit non-goals

- VPN-triggered compressor arming  
- Routing RTSP over `wg*` / `tun*`  
- MAVLink or cellular  

## Pass criteria

- VID-01–VID-04 **PASS** with file source  
- Isolation: no VPN/MAV/cellular components required or running  
- Zero orphan GStreamer pipelines after teardown  

## Teardown checklist

1. Stop monitor and video-source  
2. Stop `rtsp_lab`  
3. Confirm nothing listens on 8554/8555  
4. Restore previous `system.yaml` if backed up  
