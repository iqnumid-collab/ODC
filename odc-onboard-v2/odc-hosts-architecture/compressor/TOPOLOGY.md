# Compressor testing topologies

![Lab topology](diagrams/rendered/compressor-lab-topology.svg)

## T1 — Per-DUT loopback (primary local)

**Shape:** each Pi is a self-contained media plane. molt only SSHes in.

```
molt ──SSH──► rpi-4b [ video-source :8554 → compressor :8555 ← monitor ]
molt ──SSH──► rpi-5  [ video-source :8554 → compressor :8555 ← monitor ]
```

| Property | Value |
|----------|-------|
| Ingest URL | `rtsp://127.0.0.1:8554/stream` |
| Output URL | `rtsp://127.0.0.1:8555/stream` |
| Binding | Synthetic local (`lo` / `127.0.0.1`) |
| Proves | Encode/republish without LAN variables |

![DUT dataplane](diagrams/rendered/compressor-dut-dataplane.svg)

## T2 — Dual compressor on one DUT

Second instance on alternate ports (e.g. `:8556` / `:8557`) for VID-OPS-13. Still loopback.

## T3 — Optional USB camera (future)

Replace file source with V4L2 when available. **SKIP** today (`odc-video-source` is MP4-only).

---

## T4 — Cross-host bridge: remote ingest + remote consume (mandatory bridge)

**Shape:** source and/or monitor run on a **different** host than the compressor. Traffic uses lab ethernet `192.168.10.0/24`.

![Cross-host bridge](diagrams/rendered/compressor-cross-host-bridge.svg)

**Canonical layout (XBR-01 / XBR-02):**

```
molt  [odc-video-source :8554]
   │  rtsp://192.168.10.10:8554/stream
   ▼
DUT   [rtsp.compressor  listen 0.0.0.0:8555]
   │  rtsp://192.168.10.11|12:8555/stream
   ▼
molt  [odc-rtsp-monitor]
```

| Property | Value |
|----------|-------|
| Ingest | `rtsp://192.168.10.10:8554/stream` (`input_host` / ingest URI = molt) |
| Output | Bound on DUT eth0 (`0.0.0.0:8555` or DUT IP) |
| Consume | Monitor on molt (or hexa) pulls DUT LAN URL |
| Proves | Real RTSP bridging: remote pull → compress → remote clients get data |

**Fixture notes:** compressor must not rely on `127.0.0.1` for ingest; set `input_host` (or equivalent) to the **source host LAN IP**. Output must be reachable off-box (firewall / bind address).

## T5 — Dual-path (local + remote consumers)

Same DUT compressor as T1 or T4. Simultaneously:

- Local monitor on DUT → `127.0.0.1:8555`
- Remote monitor on molt → `DUT_IP:8555`

**PASS:** both report BW/frames > 0 (XBR-03). Proves multi-client LAN delivery.

## T6 — Cross-DUT peer matrix

![Cross-DUT matrix](diagrams/rendered/compressor-cross-dut-matrix.svg)

```
rpi-4b compressor :8555  ──LAN──►  monitor on rpi-5
rpi-5  compressor :8555  ──LAN──►  monitor on rpi-4b
molt optionally monitors both
```

**PASS:** each peer consume shows BW > 0 (XBR-04 / XBR-05). Proves independent compressors and correct host addressing.

## T7 — Optional hexa viewer

hexa-five runs only `odc-rtsp-monitor` against a DUT output URL. SKIP if tools not installed on hexa.

---

## Explicit non-topologies

| Topology | Status |
|----------|--------|
| RTSP over WireGuard / OpenVPN | Out of scope here (REAL_HARDWARE_TEST_SUITE) |
| Cellular WAN for RTSP | Out of scope |
| Device-to-device overlay via hub | Not required |

## Host responsibilities

![Host matrix](diagrams/rendered/compressor-host-matrix.svg)

| Host | Local loopback | Cross-host bridge |
|------|----------------|-------------------|
| molt | Driver | Source and/or monitor over LAN |
| each DUT | Source + compressor + monitor | Compressor; peer monitor for other DUT |
| hexa-five | — | Optional monitor only |
