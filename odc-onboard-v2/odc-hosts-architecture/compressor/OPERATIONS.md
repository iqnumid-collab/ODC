# Compressor operations catalog

![Lifecycle](diagrams/rendered/compressor-lifecycle.svg)

## Lifecycle states (under test)

| State | Meaning | Typical stimulus |
|-------|---------|------------------|
| Idle | Daemon up; compressor stopped; no source required | Fixture loaded |
| SourceUp | `odc-video-source` publishing (local or remote host) | Start source on role host |
| Starting | Component start requested | `graph components start` |
| Running | Pipeline up; output listening on DUT | Started + `:8555` listen |
| Reconnecting | Ingest lost; bridge recovering | Kill **local or remote** source |
| AbrActive | Bitrate adapting | Throttle / loss |
| Error | Start failed (e.g. port busy) | Occupy output port |
| Stopped | Clean stop | `graph components stop` |

## Operation groups — local (T1)

### A — Registration and source

| Op | CLI / process | Expect |
|----|---------------|--------|
| Register | `graph status` | `rtsp.compressor` present; ports match fixture |
| Source publish | `odc-video-source --port 8554 …` on DUT | `:8554` listen |

### B — Compress / republish

| Op | CLI / process | Expect |
|----|---------------|--------|
| Start | `graph components start rtsp_lab` | Running + Started |
| Throughput | `odc-rtsp-monitor rtsp://127.0.0.1:8555/stream` | BW > 0 |
| Codec / bitrate | Fixture variants | Caps honor config |

### C — Resilience / clients / scale

Stop, restart, port conflict, daemon restart, source loss/recover, client join/leave, ABR, auth, dual instance — as in [SUITE_COMPRESSOR.md](SUITE_COMPRESSOR.md) OPS/EVT tables.

![DUT dataplane](diagrams/rendered/compressor-dut-dataplane.svg)

---

## Operation groups — cross-host bridge (T4–T6)

![Cross-host bridge](diagrams/rendered/compressor-cross-host-bridge.svg)

### F — Remote ingest

| Op | Where | Expect |
|----|-------|--------|
| Publish source on molt | `odc-video-source` bind `0.0.0.0:8554` | Reachable as `rtsp://192.168.10.10:8554/stream` |
| Point compressor ingest | DUT fixture `input_host=192.168.10.10` | Pipeline starts without ingest error (XBR-01) |

### G — Remote consume

| Op | Where | Expect |
|----|-------|--------|
| Monitor from molt | `odc-rtsp-monitor rtsp://DUT_IP:8555/stream` | BW > 0 on **molt** (XBR-02) |
| Dual-path | DUT local monitor + molt monitor | Both BW > 0 (XBR-03) |
| Peer DUT monitor | Other Pi pulls this DUT | BW > 0 (XBR-04) |
| Bidirectional | Both Pis compress; each peers | Both directions OK (XBR-05) |

![Cross-DUT matrix](diagrams/rendered/compressor-cross-dut-matrix.svg)

### H — Bridge data integrity

| Op | Assert |
|----|--------|
| Codec/resolution over LAN | Remote caps match fixture (XBR-06) |
| Bitrate band over LAN | Remote BW within tolerance |
| Remote client events | ClientConnected/Disconnected when molt monitor joins/leaves (XBR-07) |
| Continuity | Kill remote source → SourceLost; restore → SourceRecovered + remote BW (XBR-08) |

### I — Addressing rules (must hold)

| Rule | Detail |
|------|--------|
| Loopback ingest | Only for T1; never for XBR-01 |
| LAN ingest | Source host IP in fixture / URI |
| LAN publish | Compressor and source listen on `0.0.0.0` (or eth0) for bridge |
| Consumer URI | Always `rtsp://<compressor-host-LAN-IP>:8555/stream` off-box |

## Who runs what

| Operation class | Typical hosts |
|-----------------|---------------|
| Local OPS/EVT | DUT only (+ molt SSH) |
| XBR ingest | Source = molt; compressor = DUT |
| XBR consume | Monitor = molt and/or peer DUT and/or hexa |
| Control / evidence | molt always |
