# RTSP Compressor — User Endpoint Test Workflows

---

## 1. User Journey: Config → Daemon → CLI

The full path a user takes — editing `config.json`, starting the daemon, and running CLI commands to manage compressors.

![User Config Flow](diagrams/rendered/rtsp-user-config-flow.svg)

**Flow:**
1. User creates `config.json` with `"rtsp": {"enabled": true}`
2. Starts daemon → socket appears at `/run/odc-onboard/odc-onboard.sock`
3. CLI auto-detects the socket and connects (no `--cli` flag needed)
4. User interacts via three commands:
   - `rtsp status` — lists all compressors
   - `rtsp start <id> <iface> <ip>` — spawns a compressor thread with its own `GMainLoop` and `RtspBridge`
   - `rtsp stop <id>` — quits the GLib loop, joins the thread, stops the bridge

---

## 2. End-to-End Video Pipeline Test

Full integration: real MP4 video → compressor transcode → bandwidth verification.

![End-to-End Video Pipeline](diagrams/rendered/rtsp-user-e2e-video.svg)

**Flow:**
1. `odc-video-source` serves `mercedes.mp4` on `rtsp://127.0.0.1:8554/stream` (loop)
2. User starts daemon, then runs `rtsp start e2e-test lo 127.0.0.1`
3. Compressor builds a `BridgeConfig`: H.265 800kbps 720p30
4. `RtspBridge` ingests from video source, re-encodes, serves on `rtsp://0.0.0.0:8554/stream`
5. `odc-rtsp-monitor` connects, measures bandwidth over 5s
6. Test asserts `Avg Bandwidth > 0` kbps — real video data was transcoded

**GStreamer pipeline inside compressor:**
```
rtspsrc (ingest from port 8554) → decodebin → x265enc (800kbps, 720p30) → rtph265pay → GstRTSPServer (port 8554)
```

---

## 3. Auto-Spawn Config Test

Two config modes tested side-by-side — `auto_start_on_vpn: true` vs `false`.

![Auto-Spawn Config](diagrams/rendered/rtsp-user-auto-spawn.svg)

**Left path (auto=true):**
- Daemon starts, TUN + IP assigned → netlink event fires
- `VpnInterfaceEvent{AddressAdded}` → `RtspCompressorManager` auto-spawns
- `rtsp status` shows compressor running
- Remove IP → teardown

**Right path (auto=false):**
- Same TUN + IP → no auto-spawn
- `rtsp status` shows empty
- Manual `rtsp start` still works
- Manual `rtsp stop` still works

---

## 4. CLI Command Paths

All paths from user input to visible output — covering standalone CLI, daemon-connected CLI, config-driven auto-spawn, and the backend layer.

![CLI Command Paths](diagrams/rendered/rtsp-user-cli-paths.svg)

**Three entry points:**
- **Standalone CLI** (`--cli --config /dev/null`) → `CliLocalBackend` — no daemon needed
- **Daemon-connected CLI** (auto-detect socket) → `CliRemoteBackend` → IPC `MethodRegistry`
- **Config** (`config.json`) → `RtspCompressorManager` auto-spawn via EventBus

**Five output types the user sees:**
- `Started compressor: mycam` (success)
- `connection: mycam  running: yes` (status with instances)
- `No compressor instances active.` (status empty)
- `RTSP compressor not configured` (error — no rtsp key in config)
- Help text listing all 3 subcommands

---

## 5. Test Suite Overview

All 10 sections and 57 tests executed sequentially.

![Suite Overview](diagrams/rendered/rtsp-user-suite-overview.svg)

| # | Section | Tests | Root? |
|---|---------|-------|-------|
| 1 | Config Creation | 6 | No |
| 2 | Hardware Detection | 4 | No |
| 3 | CLI Start/Stop | 8 | Yes |
| 4 | Auto-Spawn Config | 6 | Yes |
| 5 | End-to-End Video | 10 | No |
| 6 | RTSP Disabled | 4 | No |
| 7 | Multiple Compressors | 4 | Yes |
| 8 | Thread Safety + Shutdown | 5 | Yes |
| 9 | Build Verification | 5 | No |
| 10 | CLI Help Consistency | 5 | No |
| **Total** | | **57** | |

---

## Summary

| Diagram | Type | What it shows |
|---------|------|---------------|
| Config → CLI Flow | Flowchart | User edits config, starts daemon, runs CLI, compressor thread lifecycle |
| E2E Video Pipeline | Sequence | video-source → compressor transcode → monitor bandwidth verification |
| Auto-Spawn Config | Flowchart | `auto_start_on_vpn: true` vs `false` — two parallel test paths |
| CLI Command Paths | Flowchart | 3 entry points (standalone/daemon/config) → 5 output message types |
| Suite Overview | Flowchart | All 10 test sections with substeps, totaling 57 tests |
