# RTSP Compressor Test Suite — Workflow Diagrams

---

## 1. Test Suite Overview

All 15 test sections executed sequentially — from source verification through build checks to thread safety.

![Test Suite Overview](diagrams/rendered/rtsp-test-suite-overview.svg)

### Section breakdown

| # | Section | Privileged |
|---|---------|-----------|
| 1 | Source Code Verification | No |
| 2 | CLI Help & Command Parsing | No |
| 3 | Hardware Detection (detectHardware) | No |
| 4 | Config Generation & Validation | No |
| 5 | Daemon Startup — Compressor Disabled | No |
| 6 | Daemon Startup — Compressor Enabled | Yes |
| 7 | Manual CLI Operations | Yes |
| 8 | IPC Method Registration | No |
| 9 | Auto-Spawn Full Lifecycle | Yes |
| 10 | Auto-Spawn Disabled Mode | Yes |
| 11 | Multiple Concurrent Compressors | Yes |
| 12 | Graceful Shutdown | Yes |
| 13 | End-to-End Video Pipeline | No |
| 14 | Build Verification | No |
| 15 | Thread Safety & Concurrency | Yes |

---

## 2. Auto-Spawn Lifecycle Test

Sequence of the core test: TUN interface creation triggers `VpnInterfaceEvent`, the compressor manager spawns a thread, and teardown occurs on IP removal.

![Auto-Spawn Lifecycle](diagrams/rendered/rtsp-test-auto-spawn.svg)

**Flow:**
1. Test creates TUN interface, assigns IP → netlink event fires
2. `EventBus` delivers `VpnInterfaceEvent(AddressAdded)` to `RtspCompressorManager`
3. Manager spawns `std::thread` with its own `GMainLoop`, creates `RtspBridge`
4. Bridge starts GStreamer transcode pipeline, publishes `RtspCompressorStatusEvent(running=true)`
5. CLI `rtsp status` confirms instance running
6. IP removal triggers `AddressRemoved` → `g_main_loop_quit` → thread joins → status `running=false`

### Test matrix (Section 9)

| Event | Expected |
|-------|----------|
| `AddressAdded` (IPv4, valid IP) | Compressor spawned |
| `AddressAdded` (same conn_id, already running) | No duplicate |
| `AddressRemoved` | Compressor torn down |
| `AddressAdded` (IPv6) | No spawn (filtered) |
| `AddressAdded` (empty IP) | No spawn (skipped) |
| `InterfaceUp` only | No spawn |
| `InterfaceCreated` only | No spawn |
| `VpnState::Disconnected` | Teardown |
| `VpnState::Error` | Teardown |

---

## 3. End-to-End Video Pipeline Test

Full integration test using real video — `odc-video-source` serves MP4, compressor transcodes, `odc-rtsp-monitor` verifies.

![End-to-End Video Pipeline](diagrams/rendered/rtsp-test-e2e-video.svg)

**Flow:**
1. `odc-video-source` serves `mercedes.mp4` on `rtsp://127.0.0.1:19554/source`
2. Daemon starts with RTSP config, auto-spawns compressor on IP trigger
3. Compressor's `RtspBridge` pulls from the video source, transcodes to H.265 720p@800kbps
4. Output served on `rtsp://127.0.0.1:8554/stream`
5. `odc-rtsp-monitor` connects via TCP, measures bandwidth over 5 seconds
6. Test asserts `Avg Bandwidth > 0` (real video data was received)
7. Cleanup: stop compressor, kill video source, kill daemon

### GStreamer pipeline inside the compressor

```
rtspsrc (ingest from video source port 19554)
  → decodebin (depay + decode H.264)
  → x264enc (encode to H.265, 800 kbps, 1280x720, 30 fps)
  → rtph265pay (RTP payload)
  → GstRTSPServer (serve on port 8554, auth optional)
```

### GStreamer pipeline inside the monitor

```
rtspsrc (connect to compressor output port 8554)
  → identity (pad probe for bandwidth measurement)
  → decodebin (depay + decode)
  → fakesink (discard, no rendering)
```

---

## 4. Daemon Lifecycle in Tests

State machine for how each test section manages the odc-onboard daemon.

![Daemon Lifecycle](diagrams/rendered/rtsp-test-daemon-lifecycle.svg)

**Phases:**
1. **ConfigGen** — generate JSON config with rtsp section variations
2. **DaemonStart** — fork `odc-onboard --daemon --socket`, poll for socket, timeout after 10 retries
3. **RunTests** — privilege-gate (skip CAP_NET_ADMIN sections if no root), pipe CLI commands, verify output
4. **DaemonStop** — `kill DAEMON_PID`, `wait`, remove socket file
5. **Score** — tally PASS/FAIL/SKIP, print summary, exit with appropriate code

### Privilege gating pattern

```
has_net_admin?
  ├── NO  → for i in $(seq tests); skip "Requires CAP_NET_ADMIN"; done
  └── YES → create_tun_device → set_interface_up → assign_ip → test → cleanup TUN
```

Root operations use `echo "Fehmi11625826!" | sudo -S <cmd>` via the `run_privileged()` helper.

---

## 5. CLI Command Test Paths

All paths from user input to test assertions — covering CLI piping, IPC methods, EventBus events, and the compressor manager.

![CLI Command Paths](diagrams/rendered/rtsp-test-cli-paths.svg)

**Three entry points into the compressor manager:**

| Entry | Mechanism | Tests in Section |
|-------|-----------|-----------------|
| **CLI Piping** | `printf 'cmd\nexit' \| BINARY --cli` | 2, 5, 6, 7, 11, 13 |
| **IPC Methods** | `rtsp.status`, `rtsp.start`, `rtsp.stop` JSON-RPC | 8 |
| **EventBus** | `VpnInterfaceEvent`, `VpnStateChangeEvent` | 9, 10 |

**Four types of assertions:**
- `assert_contains "running: yes"` — compressor spawned successfully
- `assert_contains "No compressor"` — all instances torn down
- `assert_exit_code 0` — process didn't crash
- `assert_contains "3 compressors"` — multiple concurrent instances

---

## Summary

| Diagram | Type | Nodes |
|---------|------|-------|
| Suite Overview | Flowchart | 15 sections, 65+ nodes |
| Auto-Spawn Lifecycle | Sequence | 7 participants, 20+ messages |
| End-to-End Video | Sequence | 5 participants, 18+ messages |
| Daemon Lifecycle | State | 5 phases, 20+ states |
| CLI Command Paths | Flowchart | 3 entry points, 4 assertion types |
