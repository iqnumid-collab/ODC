# odc-onboard-v2 Test Suite Reference

Catalog of every automated test workflow under [`tests/`](../tests/), including what each suite exercises, how it works, and the latest known results.

## How to run

| Command | Scope |
|---------|--------|
| `sudo tests/run_all.sh` | Full master runner: build, install, all suites, cleanup |
| `sudo REAL_HARDWARE=1 tests/run_all.sh` | Above + real hardware graph E2E (lab VPN / FC / RTSP) |
| `sudo tests/test_graph_<name>.sh` | Run a single graph suite against `build/odc-onboard-v2` |
| `sudo tests/test_graph_real_hardware_e2e.sh` | Real hardware only (see [REAL_HARDWARE_TEST_SUITE.md](REAL_HARDWARE_TEST_SUITE.md)) |

Shared helpers live in [`tests/helpers/test_helpers.sh`](../tests/helpers/test_helpers.sh) (IPC JSON-RPC, graph CLI wrappers, wait helpers, PASS/FAIL/SKIP accounting).

## Result legend

| Outcome | Meaning |
|---------|---------|
| **PASS** | Assertion succeeded |
| **FAIL** | Assertion failed; suite exits non-zero if any FAIL |
| **SKIP** | Prerequisite missing (no root, no GStreamer, no FC, VPN unreachable, etc.) |

**Latest real-hardware run** (after binary fixes, lab VPN reachable): **56 PASS, 0 FAIL, 6 SKIP** — see [Known results](#known-results).

---

## Master runner

**Script:** [`tests/run_all.sh`](../tests/run_all.sh)

| Step | Goal | Logic |
|------|------|--------|
| 1 Build | Produce `build/odc-onboard-v2` | CMake configure (reuse cache), `make -j` |
| 2 Install | System integration layout | Install to `/usr/bin/odc-onboard-v2`, deploy configs to `/etc/odc-onboard/`, install systemd unit |
| 3 Run suites | Aggregate PASS/FAIL/SKIP | Calls each suite script; parses `PASS:` / `FAIL:` / `SKIP:` lines |
| 4 Cleanup (trap) | Leave host clean | Stop service, remove TUN devices, kill orphaned processes |

**Suite order (installed binary):**

1. Graph Schema → Graph VPN Binding → Graph MAVLink Extender → Graph RTSP E2E  
2. Graph Real Hardware E2E *(only if `REAL_HARDWARE=1`)*  
3. Service Installation → Service Mode → CLI Mode → MAVLink Hardware → VPN Integration → VPN Monitor + Extender → RTSP Compressor → RTSP User E2E  

**Then repeats graph suites** against the build-tree binary (same scripts, `BINARY=build/odc-onboard-v2`).

---

## Graph v2 suites (primary)

These target the **graph runtime** (`version: 2` YAML, `graph.*` IPC/CLI). Each suite uses a temp dir, private Unix socket, and a short-lived daemon unless noted.

### 1. Graph Schema (Phase 0)

**Script:** [`tests/test_graph_schema.sh`](../tests/test_graph_schema.sh)

| | |
|--|--|
| **Goal** | Validate graph config format, rejection of bad YAML, and baseline IPC (`graph.status`, `graph.validate`) |
| **Prerequisites** | Built binary; `CAP_NET_ADMIN` or root for live daemon tests |

| Section | Workflow / logic | Expected result |
|---------|------------------|-----------------|
| 1 Example config | Checks `config/system.example.yaml` has `version: 2`, `components`, `links`, `triggers` | PASS |
| 2 Invalid YAML | Malformed YAML → daemon refuses start; duplicate component ids → `graph.validate` returns `valid=false` | PASS / SKIP without net admin |
| 3 graph.status + validate | Start daemon with example config; IPC `graph.status` (version 2, ≥5 components); `graph.validate` valid | PASS |

---

### 2. Graph VPN Binding (Phase 1/2)

**Script:** [`tests/test_graph_vpn_binding.sh`](../tests/test_graph_vpn_binding.sh)

| | |
|--|--|
| **Goal** | CRUD for components, links, triggers via IPC; YAML persistence; optional real WireGuard connect |
| **Prerequisites** | Root / `CAP_NET_ADMIN`; `tests/vpn-configs/wg-client.conf` |

| Section | Workflow / logic | Expected result |
|---------|------------------|-----------------|
| Daemon smoke | Minimal graph YAML; daemon + socket up | PASS |
| graph.components | `add` → visible in status + YAML; `remove` → gone from status + YAML | PASS |
| graph.links | `add` / `remove` link; status + YAML round-trip | PASS |
| graph.triggers | `set` trigger; status + YAML persist | PASS |
| Optional VPN | `graph.components.start vpn_test`; poll status for connected state | PASS if server reachable; else SKIP |

---

### 3. Graph MAVLink Extender (Phase 3)

**Script:** [`tests/test_graph_mavlink_extender.sh`](../tests/test_graph_mavlink_extender.sh)

| | |
|--|--|
| **Goal** | MAVLink router + extender graph topology, IPC control, simulated VPN binding via TUN |
| **Prerequisites** | Root / `CAP_NET_ADMIN` |

| Section | Workflow / logic | Expected result |
|---------|------------------|-----------------|
| 1 Static config | YAML contains `mav_primary`, `mav_ext_cloud`, tunnel→transport link, `vpn.connected` trigger | PASS |
| 2 Daemon + status | Start daemon; `graph.status` lists extender + link; `graph.validate` valid | PASS |
| 3 IPC control | `graph.components.list`; stop/start extender via IPC | PASS |
| 4 TUN simulation | Create `test-graph-ext0`, assign IP; wait; extender still in status after interface churn | PASS |
| 5 Link mutation | `graph.links.remove` / `add` round-trip; YAML persisted | PASS |

---

### 4. Graph RTSP E2E (Phase 4)

**Script:** [`tests/test_graph_rtsp_e2e.sh`](../tests/test_graph_rtsp_e2e.sh)

| | |
|--|--|
| **Goal** | RTSP compressor component via graph IPC; loopback pipeline with video source + monitor |
| **Prerequisites** | GStreamer-linked binary; built `odc-video-source`, `odc-rtsp-monitor`, test video |

| Section | Workflow / logic | Expected result |
|---------|------------------|-----------------|
| 1 Config + GStreamer | Graph YAML with `rtsp.compressor`; `ldd` shows GStreamer | PASS or SKIP (no GStreamer) |
| 2 Daemon + IPC | Daemon up; `graph.status` lists `rtsp_cloud`; validate valid | PASS |
| 3 Start/stop | `graph.components.start/stop rtsp_cloud` on loopback | PASS |
| 4 E2E pipeline | `odc-video-source` on :8554 → start compressor → `odc-rtsp-monitor` on :8555 bandwidth > 0 | PASS or SKIP |
| 5 CRUD | Add/remove second compressor via IPC | PASS |

---

### 5. Graph Real Hardware E2E

**Script:** [`tests/test_graph_real_hardware_e2e.sh`](../tests/test_graph_real_hardware_e2e.sh)  
**Detail:** [REAL_HARDWARE_TEST_SUITE.md](REAL_HARDWARE_TEST_SUITE.md)

| | |
|--|--|
| **Goal** | End-to-end field/lab integration: persisted YAML, CLI mutations, multi-VPN, real MAVLink + RTSP, restart durability |
| **Prerequisites** | Root; GStreamer; video tools; VPN configs at `/etc/odc-onboard/vpn/` (hexa-five lab); optional `/dev/ttyACM0` |

| Section | Workflow / logic | Latest result |
|---------|------------------|---------------|
| **0 Preflight** | Binary, deploy VPN configs (lab endpoints `192.168.10.201` / `.202`), seed YAML, GStreamer, video tools, privileges | PASS |
| **1 Start services** | `odc-video-source` on input ports; daemon from persisted `system.yaml` | PASS |
| **2 Baseline** | `graph.status` JSON, version 2, `graph.validate`, CLI vs IPC parity | PASS |
| **3 CLI mutations** | Add `mav_primary`, 2× WG + 2× OVPN clients, extenders, compressors, links, triggers; YAML asserts | PASS |
| **4 Data plane** | `graph components start mav_primary` + first WG VPN; wait `connected` + `ip_ready`; links active; extender/compressor armed/running | PASS when VPN up; SKIP if lab unreachable |
| **5 MAVLink** | FC device present; `mav_primary` in status; TCP :5760 listen; extender message counters (10s poll) | 5760 **PASS** after router fix; extender traffic **SKIP** without FC traffic |
| **6 RTSP E2E** | Wait compressor `running` + output port; `odc-rtsp-monitor` bandwidth on :8555 | **PASS** when VPN + pipeline up; **SKIP** if compressor not started |
| **7 Teardown** | Stop compressor/VPN via CLI; topology still in persisted YAML | PASS |
| **8 Restart** | Stop daemon (SIGTERM → SIGKILL timeout); restart same config; component ids match; `graph.validate` | **PASS** after JSON config persistence fix |
| **9 Cleanup** | Config file retained; no orphaned video-source processes | PASS / SKIP |

**Trigger model under test:**

```
vpn.connected  → activate links, arm extender/compressor
vpn.ip_ready   → propagate binding, start data plane
vpn.disconnected → deactivate links, stop targets
```

---

## Legacy integration suites

These predate or complement the graph model. They assume the **installed** binary at `/usr/bin/odc-onboard-v2` (or v1 path in older scripts) and often use **systemd** + legacy JSON config or CLI verbs (`vpn`, `mavlink`, `rtsp`).

### 6. Service Installation

**Script:** [`tests/test_service_install.sh`](../tests/test_service_install.sh)

| | |
|--|--|
| **Goal** | Verify package layout: binary, systemd unit, config dirs, permissions |
| **Logic** | Checks `/usr/bin/odc-onboard-v2`, ELF type, unit file, `systemctl` enable/start, directory ownership |
| **Result** | PASS when run after `run_all.sh` install step |

---

### 7. Service Mode Lifecycle

**Script:** [`tests/test_service_mode.sh`](../tests/test_service_mode.sh)

| | |
|--|--|
| **Goal** | Daemon lifecycle via systemd: start, IPC socket, signals, graceful shutdown |
| **Logic** | `systemctl start odc-onboard`; assert active process, `/run/odc-onboard/odc-onboard.sock`, journal events, stop/restart |
| **Result** | PASS with working service unit |

---

### 8. CLI Mode Interaction

**Script:** [`tests/test_cli_mode.sh`](../tests/test_cli_mode.sh)

| | |
|--|--|
| **Goal** | Remote CLI (IPC attach) and local standalone CLI |
| **Logic** | Attach to daemon → `help`, `vpn list`, `mavlink status`, `graph status`; local mode without daemon for offline commands |
| **Result** | PASS; exercises v1-style commands where still exposed |

---

### 9. MAVLink Hardware

**Script:** [`tests/test_mavlink_hardware.sh`](../tests/test_mavlink_hardware.sh)

| | |
|--|--|
| **Goal** | Real flight-controller UART + MAVLink router on installed service |
| **Prerequisites** | `/dev/ttyACM0` (or configured device); service running |

| Section | Goal |
|---------|------|
| Device existence | FC serial device present |
| UART endpoint | Endpoint created in router |
| Baudrate detection | Autobaud across configured rates |
| Message reception | MAVLink frames from FC |
| TCP server | Port 5760 listening |
| Deduplication | Dedup config honored |
| Logging | MAVLink log start/stop |
| Statistics | Endpoint counters in status |
| Events | Router status events on bus |

**Result:** PASS for software paths; hardware sections SKIP without FC.

---

### 10. VPN Integration

**Script:** [`tests/test_vpn_integration.sh`](../tests/test_vpn_integration.sh)

| | |
|--|--|
| **Goal** | Legacy VPN manager: WireGuard + OpenVPN connect/stop/restart via CLI |
| **Logic** | Two halves (WG, OVPN): validate config → create → start → status → stop → restart |
| **Result** | PASS when lab VPN reachable; SKIP/FAIL on network issues |

---

### 11. VPN Monitor + MAVLink Extender

**Script:** [`tests/test_vpn_monitor_extender.sh`](../tests/test_vpn_monitor_extender.sh)

| | |
|--|--|
| **Goal** | Netlink VPN monitor, extender auto-provisioning, binding propagation (legacy app lifecycle) |
| **Logic** | Source checks (`vpn_monitor.hpp`, netlink groups); daemon with JSON config; simulated TUN; extender CRUD; `tcp_server_port=0` on extenders |
| **Result** | Large static + integration suite; PASS in CI when privileged |

---

### 12. RTSP Compressor Integration

**Script:** [`tests/test_rtsp_compressor.sh`](../tests/test_rtsp_compressor.sh)

| | |
|--|--|
| **Goal** | RTSP subsystem wiring: sources, CMake, CLI `rtsp` commands, IPC methods, hardware detection |
| **Logic** | Mostly **static verification** (files, symbols, help text) + offline CLI parsing; not a full video pipeline |
| **Result** | PASS (no live GStreamer required for most checks) |

---

### 13. RTSP User Endpoint Tests

**Script:** [`tests/test_rtsp_user_e2e.sh`](../tests/test_rtsp_user_e2e.sh)

| | |
|--|--|
| **Goal** | User-facing RTSP workflows on legacy JSON config (`rtsp.enabled`, `auto_start_on_vpn`) |
| **Logic** | Create user configs → daemon → `rtsp help/status/start/stop` → video source + monitor bandwidth → VPN auto-start behavior |
| **Prerequisites** | GStreamer binary; built video tools |
| **Result** | PASS when GStreamer + tools available |

---

## Auxiliary runners

| Script | Purpose |
|--------|---------|
| [`tests/run_privileged_tests.sh`](../tests/run_privileged_tests.sh) | Subset of suites needing root |
| [`tests/test_suite.sh`](../tests/test_suite.sh) | Older aggregated runner |

---

## Known results

Summary from recent runs on hexa-five lab hardware (Docker chroot, `REAL_HARDWARE=1`):

| Suite | PASS | FAIL | SKIP | Notes |
|-------|------|------|------|-------|
| **Graph Real Hardware E2E** | 56 | 0 | 6 | All failures fixed in binary; skips = optional hardware / VPN |
| Graph Schema | All | 0 | 0–5 | SKIP only without net admin |
| Graph VPN Binding | All | 0 | 0–2 | VPN connect may SKIP |
| Graph MAVLink Extender | All | 0 | 0 | |
| Graph RTSP E2E | All | 0 | 0–4 | SKIP without GStreamer/tools |

### Binary fixes tied to prior FAILs

| Symptom | Root cause | Fix (component) |
|---------|------------|-----------------|
| TCP 5760 not listening | `MavRouter` got component id as `extender_id` → `tcp_port=0` | `mav_router_component.cpp`: pass `""` for primary router |
| Restart `Failed to load graph config` | Flow-style YAML broke reload (`device: /dev/ttyACM0`) | `graph_schema.cpp`: persist config as JSON string; block YAML; `.bak` fallback |
| RTSP compressor never listening | Wrong GLib context; double `start_bridge`; start required VPN binding for localhost ingest | `rtsp_compressor_component.cpp`, `RtspBridge.cpp` |
| Links inactive after VPN | Trigger replay / `enabled=false` on targets | `trigger_evaluator.cpp`, `graph_runtime.cpp`, `link_engine.cpp` |

---

## File map

```
tests/
├── run_all.sh                      # Master runner
├── helpers/
│   ├── test_helpers.sh             # Shared IPC, waits, assertions
│   └── real_hardware.env.example   # Lab VPN / port overrides
├── test_graph_schema.sh            # Phase 0
├── test_graph_vpn_binding.sh       # Phase 1/2
├── test_graph_mavlink_extender.sh  # Phase 3
├── test_graph_rtsp_e2e.sh          # Phase 4
├── test_graph_real_hardware_e2e.sh # Lab / field E2E
├── test_service_install.sh
├── test_service_mode.sh
├── test_cli_mode.sh
├── test_mavlink_hardware.sh
├── test_vpn_integration.sh
├── test_vpn_monitor_extender.sh
├── test_rtsp_compressor.sh
└── test_rtsp_user_e2e.sh
```

## Related documentation

- [REAL_HARDWARE_TEST_SUITE.md](REAL_HARDWARE_TEST_SUITE.md) — lab setup, env vars, CLI cookbook
- [ARCHITECTURE.md](ARCHITECTURE.md) — graph runtime and triggers
- [YAML_SCHEMA.md](YAML_SCHEMA.md) — config schema
- [docs-architecture/RTSP_USER_E2E_TEST_SUITE.md](../../docs-architecture/RTSP_USER_E2E_TEST_SUITE.md) — RTSP bandwidth criteria
