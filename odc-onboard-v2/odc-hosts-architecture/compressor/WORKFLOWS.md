# Compressor workflows across hosts

Driver is always **molt**. Media roles may be **split across hosts** for bridge cases.

## 1 — Local operations workflow (T1)

![Ops workflow](diagrams/rendered/compressor-ops-workflow.svg)

| Step | Host | Action |
|------|------|--------|
| 1 | molt → DUT | Backup `system.yaml`; install video-only fixture; restart `odc-onboard-v2` |
| 2 | DUT | Start `odc-video-source` (`:8554/stream`) |
| 3 | molt → DUT | `graph status` → VID-OPS-01 |
| 4 | molt → DUT | `graph components start rtsp_lab` (local binding) |
| 5 | DUT | Monitor `127.0.0.1:8555` → BW > 0 |
| 6 | DUT | Caps / codec checks |
| 7 | molt → DUT | Stop / restart / port-conflict / daemon-restart variants |
| 8 | molt → DUT | Tear down; restore YAML |

## 2 — Local events workflow (T1)

![Events workflow](diagrams/rendered/compressor-events-workflow.svg)

Subscribe on DUT IPC **before** start; assert Started / Client* / Source* / Stopped / status metrics.  
If events not exported → SKIP EVT-* (`events_not_exported`).

## 3 — Cross-host bridge workflow (T4) — mandatory bridge path

![Bridge workflow](diagrams/rendered/compressor-bridge-workflow.svg)

| Step | Host | Action |
|------|------|--------|
| 1 | molt → DUT | Apply fixture with **`input_host=192.168.10.10`** (or source host IP); output reachable on DUT eth0 |
| 2 | molt | Start `odc-video-source` on molt `:8554` (bind `0.0.0.0`) |
| 3 | molt → DUT | Start compressor; confirm ingest from molt LAN URL |
| 4 | molt | Run `odc-rtsp-monitor rtsp://DUT_IP:8555/stream` |
| 5 | molt | Assert BW/frames > 0 (**XBR-02**) and codec/bitrate band (**XBR-06**) |
| 6 | molt → DUT | Subscribe: remote monitor → **ClientConnected** (**XBR-07**) |
| 7 | molt | Kill source on molt → DUT **SourceLost**; restart → **SourceRecovered** + BW restore (**XBR-08**) |
| 8 | All | Stop monitor/source/compressor; free LAN ports |

**Data-plane contract under test:** packets and frames cross host boundaries with correct addressing — not only loopback.

![Cross-host bridge](diagrams/rendered/compressor-cross-host-bridge.svg)

## 4 — Dual-path workflow (T5)

1. Bring up T1 or T4 compressor on DUT.  
2. Start monitor on DUT (`127.0.0.1:8555`) **and** on molt (`DUT_IP:8555`) in parallel.  
3. **PASS (XBR-03):** both transcripts show BW > 0 within the same window.

## 5 — Cross-DUT peer matrix workflow (T6)

![Cross-DUT matrix](diagrams/rendered/compressor-cross-dut-matrix.svg)

```
# On each DUT: local source + compressor (T1 or T4 ingest)
# Then:
molt → rpi-5:  monitor rtsp://192.168.10.11:8555/stream   # XBR-04
molt → rpi-4b: monitor rtsp://192.168.10.12:8555/stream   # XBR-04 reverse
# Optional simultaneous: XBR-05 bidirectional
```

| Assert | Meaning |
|--------|---------|
| Peer BW > 0 | Stream published on LAN and decodable off-host |
| Both directions | Neither DUT is a one-way special case |
| Optional molt dual pull | Fan-out to third host |

## 6 — Dual-DUT local matrix (still independent)

```
for DUT in rpi-4b rpi-5:
  run PRE → OPS mandatory → EVT mandatory (T1)
then run XBR suite (T4→T6)
archive logs/video-suite/<ts>/<role>/
```

## Teardown (bridge-aware)

1. Stop **all** monitors (DUT, molt, hexa).  
2. Stop compressors on DUTs.  
3. Stop sources on DUT **and** molt.  
4. Confirm suite ports free on every host that listened.  
5. Restore each DUT `system.yaml`.
