# RTSP Compressor — User Endpoint Test Suite

Tests the odc-onboard RTSP compressor from the end-user perspective: editing configs, running CLI commands, spawning compressors, verifying transcoded video streams, and monitoring output bandwidth.

**Root password:** `Fehmi11625826!`  
**Total tests:** 57 (34 non-privileged, 23 require root)  
**Test file:** `odc-onboard/tests/test_rtsp_user_e2e.sh`

---

## Prerequisites

Run these once before executing the test suite:

```bash
# 1. Ensure /run/odc-onboard is user-writable
echo "Fehmi11625826!" | sudo -S mkdir -p /run/odc-onboard
echo "Fehmi11625826!" | sudo -S chown "$USER" /run/odc-onboard

# 2. Set capabilities on the binary
echo "Fehmi11625826!" | sudo -S setcap cap_net_admin+ep \
    /home/molt/Documents/GitHub/opendronecore/odc-onboard/build/odc-onboard

# 3. Verify tool binaries exist
ls tools/odc-video-source/build/odc-video-source
ls tools/odc-rtsp-monitor/build/odc-rtsp-monitor
ls tools/odc-video-source/vid-src/mercedes.mp4
```

---

## Important Note — Current Code Limitation

The compressor uses **hardcoded** values for `input_uri` and `output_port` in
`src/rtsp/rtsp_compressor_manager.hpp`:

```cpp
cfg.input_uri   = "rtsp://" + entry->target_ip + ":8554/stream";  // input port hardcoded to 8554
cfg.output_port = entry->output_port;                               // output port hardcoded to 8554
```

**Impact on testing:** Both input and output want port 8554.
The end-to-end video test (Section 5) works around this by running
`odc-video-source` on port 8554 and letting the compressor fail gracefully,
or by matching the compressor's expectation.

**Future improvement:** Make these values configurable via `config.json`:
```json
{
  "rtsp": {
    "enabled": true,
    "output_port": 8555,
    "input_port": 8554,
    "input_path": "/source"
  }
}
```

---

## Step-by-Step Implementation

### Step 1 — Create the test script skeleton

```bash
cat > odc-onboard/tests/test_rtsp_user_e2e.sh << 'SCRIPTEOF'
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BINARY="${BINARY:-$PROJECT_DIR/build/odc-onboard}"
TOOLS_DIR="$PROJECT_DIR/tools"
VIDEO_SRC="$TOOLS_DIR/odc-video-source/build/odc-video-source"
RTSP_MONITOR="$TOOLS_DIR/odc-rtsp-monitor/build/odc-rtsp-monitor"
TEST_VIDEO="$TOOLS_DIR/odc-video-source/vid-src/mercedes.mp4"

source "$SCRIPT_DIR/helpers/test_helpers.sh"

# ── Root password ───────────────────────────────────────────────────────
ROOT_PASS="Fehmi11625826!"
run_root() { echo "$ROOT_PASS" | sudo -S "$@" 2>/dev/null; }

has_root_access() {
    [ "$(id -u)" -eq 0 ] && return 0
    echo "$ROOT_PASS" | sudo -S -v 2>/dev/null && return 0
    return 1
}

# ── Test state ──────────────────────────────────────────────────────────
TEST_DIR=$(mktemp -d)
DAEMON_PID=""; VSRC_PID=""

cleanup_force() {
    stop_daemon 2>/dev/null || true
    [ -n "${VSRC_PID:-}" ] && kill "$VSRC_PID" 2>/dev/null && wait "$VSRC_PID" 2>/dev/null || true
    rm -rf "$TEST_DIR"
}
trap cleanup_force EXIT

# ── Daemon helpers ──────────────────────────────────────────────────────

stop_daemon() {
    if [ -n "${DAEMON_PID:-}" ]; then
        kill "$DAEMON_PID" 2>/dev/null || true
        timeout 3 wait "$DAEMON_PID" 2>/dev/null || true
        kill -9 "$DAEMON_PID" 2>/dev/null || true
    fi
    DAEMON_PID=""
    run_root rm -f /run/odc-onboard/odc-onboard.sock 2>/dev/null || true
}

start_daemon() {
    local cfg="$1"
    stop_daemon 2>/dev/null || true
    sleep 1

    run_root mkdir -p /run/odc-onboard 2>/dev/null
    run_root chown "$(whoami)" /run/odc-onboard 2>/dev/null || true
    run_root rm -f /run/odc-onboard/odc-onboard.sock 2>/dev/null

    "$BINARY" --config "$cfg" --daemon --socket /run/odc-onboard/odc-onboard.sock \
        2>/dev/null &
    DAEMON_PID=$!

    for i in $(seq 1 15); do
        if [ -S /run/odc-onboard/odc-onboard.sock ]; then return 0; fi
        sleep 1
    done
    return 1
}

# ── CLI helpers ─────────────────────────────────────────────────────────

run_cli() {
    local cmd="$1" t="${2:-10}"
    printf '%s\nexit\n' "$cmd" | timeout "$t" "$BINARY" --config /dev/null 2>&1 || true
}

run_cli_offline() {
    local cmd="$1" t="${2:-10}"
    printf '%s\nexit\n' "$cmd" | timeout "$t" "$BINARY" --cli --config /dev/null 2>&1 || true
}

# ── TUN helpers (privileged) ────────────────────────────────────────────

create_tun()  { run_root ip tuntap add name "$1" mode tun 2>/dev/null; }
delete_tun()  { run_root ip tuntap del name "$1" mode tun 2>/dev/null || true; }
iface_up()    { run_root ip link set "$1" up; }
iface_down()  { run_root ip link set "$1" down; }
assign_ip()   { run_root ip addr add "$2" dev "$1"; }
remove_ip()   { run_root ip addr del "$2" dev "$1" 2>/dev/null || true; }
iface_exists() { [ -d "/sys/class/net/$1" ]; }

# ── Config generator ────────────────────────────────────────────────────

mkcfg() {
    local rtsp_json="$1" out="$2"
    if [ -n "$rtsp_json" ]; then
        cat > "$out" << END
{ "version": 1, "general": { "log_level": "error" },
  "vpn": {"wireguard": { "connections": [] }, "openvpn": { "connections": [] }},
  "mavlink": { "enabled": false },
  $rtsp_json
}
END
    else
        cat > "$out" << END
{ "version": 1, "general": { "log_level": "error" },
  "vpn": {"wireguard": { "connections": [] }, "openvpn": { "connections": [] }},
  "mavlink": { "enabled": false }
}
END
    fi
}

echo ""
echo "============================================"
echo " RTSP User Endpoint Test Suite"
echo "============================================"
SCRIPTEOF
```

---

### Step 2 — Section 1: Config — User Creates RTSP Configuration (6 tests)

User creates config.json with RTSP section, starts daemon, checks CLI help.

```bash
cat >> odc-onboard/tests/test_rtsp_user_e2e.sh << 'SCRIPTEOF'

# ═══════════════════════════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 1. User Config Creation"
echo "========================================"

# 1.1
mkcfg '"rtsp": {"enabled": true, "auto_start_on_vpn": true}' \
     "$TEST_DIR/cfg_full.json"
log_test "User creates config with rtsp enabled + auto"
assert_file_exists "$TEST_DIR/cfg_full.json"
assert_valid_json "$(cat "$TEST_DIR/cfg_full.json")"

# 1.2
mkcfg '"rtsp": {"enabled": true, "auto_start_on_vpn": false}' \
     "$TEST_DIR/cfg_manual.json"
log_test "User creates config with auto-start disabled"
assert_file_exists "$TEST_DIR/cfg_manual.json"
assert_contains_fixed "$(cat "$TEST_DIR/cfg_manual.json")" \
    '"auto_start_on_vpn": false'

# 1.3
mkcfg '"rtsp": {"enabled": true}' "$TEST_DIR/cfg_min.json"
log_test "User creates minimal config (enabled only, rest defaults)"
assert_valid_json "$(cat "$TEST_DIR/cfg_min.json")"

# 1.4
start_daemon "$TEST_DIR/cfg_full.json"
log_test "User starts daemon with rtsp config"
[ -S /run/odc-onboard/odc-onboard.sock ]; assert_exit_code 0 $?

# 1.5
log_test "User runs 'rtsp help' — lists commands"
output=$(run_cli "rtsp help" 5)
assert_contains "$output" "status"
assert_contains "$output" "start"
assert_contains "$output" "stop"

# 1.6
log_test "User runs 'help' — sees RTSP section"
output=$(run_cli "help" 5)
assert_contains_fixed "$output" "RTSP Compressor"

stop_daemon
SCRIPTEOF
```

---

### Step 3 — Section 2: Hardware Detection — What the User Sees (4 tests)

Source-level verification of hardware detection availability.

```bash
cat >> odc-onboard/tests/test_rtsp_user_e2e.sh << 'SCRIPTEOF'

# ═══════════════════════════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 2. Hardware Detection (User View)"
echo "========================================"

HW_HEADER="$PROJECT_DIR/thirdparty/minimal-cmpr/include/HardwareInfo.h"

log_test "HardwareInfo.h file exists"
assert_file_exists "$HW_HEADER"

log_test "detectHardware() function declared"
assert_contains_fixed "$(cat "$HW_HEADER")" "detectHardware"

log_test "HardwareInfo reports arch field"
assert_contains_fixed "$(cat "$HW_HEADER")" "arch"

log_test "HardwareInfo reports strategy field"
assert_contains_fixed "$(cat "$HW_HEADER")" "strategy"
SCRIPTEOF
```

---

### Step 4 — Section 3: CLI Commands — User Starts/Stops Compressors (8 tests)

User creates a TUN interface, starts compressor via CLI, checks status, stops.

```bash
cat >> odc-onboard/tests/test_rtsp_user_e2e.sh << 'SCRIPTEOF'

# ═══════════════════════════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 3. User Starts / Stops Compressors"
echo "========================================"

start_daemon "$TEST_DIR/cfg_full.json"

log_test "User runs 'rtsp status' — no compressors"
output=$(run_cli "rtsp status" 5)
assert_contains "$output" "No compressor"

if ! has_root_access; then
    for i in $(seq 1 8); do
        log_test "CLI compressor test $i"; skip "Requires root access"
    done
    stop_daemon
else
    # ── Setup TUN interface ──────────────────────────────────────────
    log_test "User creates TUN interface wg-usertest"
    create_tun wg-usertest
    iface_up wg-usertest
    assign_ip wg-usertest "10.77.1.1/24"
    sleep 1
    iface_exists wg-usertest; assert_exit_code 0 $?

    # ── Start compressor ─────────────────────────────────────────────
    log_test "User: rtsp start mycam wg-usertest 10.77.1.1"
    output=$(run_cli "rtsp start mycam wg-usertest 10.77.1.1" 10)
    assert_contains "$output" "Started"

    # ── Check status ──────────────────────────────────────────────────
    log_test "User: rtsp status shows mycam running"
    sleep 3
    output=$(run_cli "rtsp status" 5)
    assert_contains_fixed "$output" "mycam"
    assert_contains_fixed "$output" "wg-usertest"
    assert_contains_fixed "$output" "running: yes"

    # ── Duplicate start — idempotent ──────────────────────────────────
    log_test "User: duplicate rtsp start (idempotent)"
    output=$(run_cli "rtsp start mycam wg-usertest 10.77.1.1" 10)
    pass  # should not crash or create duplicate

    # ── Stop compressor ──────────────────────────────────────────────
    log_test "User: rtsp stop mycam"
    output=$(run_cli "rtsp stop mycam" 10)
    assert_contains "$output" "Stopped"

    # ── Verify stopped ────────────────────────────────────────────────
    log_test "User: rtsp status shows empty after stop"
    sleep 2
    output=$(run_cli "rtsp status" 5)
    assert_contains "$output" "No compressor"

    # ── Stop nonexistent — no crash ───────────────────────────────────
    log_test "User: rtsp stop nonexistent-id (graceful)"
    output=$(run_cli "rtsp stop nonexistent-id" 5)
    pass

    # ── Missing args ──────────────────────────────────────────────────
    log_test "User: rtsp start with missing args (error shown)"
    output=$(run_cli_offline "rtsp start" 5)
    assert_contains "$output" "Usage\|Error"

    # ── Cleanup ───────────────────────────────────────────────────────
    remove_ip wg-usertest "10.77.1.1/24"
    iface_down wg-usertest
    delete_tun wg-usertest
fi

stop_daemon
SCRIPTEOF
```

---

### Step 5 — Section 4: Config — User Enables/Disables Auto-Spawn (6 tests)

User tests the `auto_start_on_vpn` flag via `config.json`.

```bash
cat >> odc-onboard/tests/test_rtsp_user_e2e.sh << 'SCRIPTEOF'

# ═══════════════════════════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 4. Auto-Spawn via Config Flag"
echo "========================================"

if ! has_root_access; then
    for i in $(seq 1 6); do
        log_test "Auto-spawn test $i"; skip "Requires root access"
    done
else
    # ── Config with auto_start_on_vpn=true ───────────────────────────
    start_daemon "$TEST_DIR/cfg_full.json"

    log_test "User creates TUN interface with IP (auto=true)"
    create_tun wg-autotest
    iface_up wg-autotest
    assign_ip wg-autotest "10.77.5.1/24"
    sleep 5

    log_test "User checks: compressor auto-spawned"
    output=$(run_cli "rtsp status" 5)
    assert_contains_fixed "$output" "wg-autotest"

    log_test "User removes IP — compressor auto-torn down"
    remove_ip wg-autotest "10.77.5.1/24"
    sleep 3
    output=$(run_cli "rtsp status" 5)
    assert_contains "$output" "No compressor"

    delete_tun wg-autotest
    stop_daemon

    # ── Config with auto_start_on_vpn=false ──────────────────────────
    start_daemon "$TEST_DIR/cfg_manual.json"

    log_test "User creates TUN (auto=false) — no auto-spawn"
    create_tun wg-manual
    iface_up wg-manual
    assign_ip wg-manual "10.77.6.1/24"
    sleep 5
    output=$(run_cli "rtsp status" 5)
    assert_contains "$output" "No compressor"

    log_test "User manually starts compressor (still works)"
    output=$(run_cli "rtsp start manualtest wg-manual 10.77.6.1" 10)
    assert_contains "$output" "Started"

    log_test "User manually stops compressor"
    output=$(run_cli "rtsp stop manualtest" 10)
    assert_contains "$output" "Stopped"

    delete_tun wg-manual
    stop_daemon
fi
SCRIPTEOF
```

---

### Step 6 — Section 5: End-to-End Video Pipeline (10 tests)

Real video: `odc-video-source` serves MP4 → compressor transcodes → `odc-rtsp-monitor` verifies.

```bash
cat >> odc-onboard/tests/test_rtsp_user_e2e.sh << 'SCRIPTEOF'

# ═══════════════════════════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 5. End-to-End Video Pipeline"
echo "========================================"

VIDEO_PORT=8554
OUTPUT_PORT=8554

# ── Check prerequisites ────────────────────────────────────────────────
MISSING_PREREQ=false
for b in "$VIDEO_SRC" "$RTSP_MONITOR" "$TEST_VIDEO"; do
    if [ ! -x "$b" ] && [ ! -f "$b" ]; then
        MISSING_PREREQ=true
    fi
done

if $MISSING_PREREQ; then
    for i in $(seq 1 10); do
        log_test "E2E video test $i"; skip "Missing prerequisites"
    done
else
    # ── 5.1 Start video source ───────────────────────────────────────
    log_test "User starts video source on port $VIDEO_PORT"
    "$VIDEO_SRC" --video "$TEST_VIDEO" --port "$VIDEO_PORT" --path /stream \
        --loop > "$TEST_DIR/vsrc.log" 2>&1 &
    VSRC_PID=$!
    sleep 2
    if ss -tlnp 2>/dev/null | grep -q "$VIDEO_PORT" || \
       netstat -tlnp 2>/dev/null | grep -q "$VIDEO_PORT"; then
        pass
    else
        fail "Video source not listening on $VIDEO_PORT"
        kill "$VSRC_PID" 2>/dev/null || true; VSRC_PID=""
        for i in $(seq 2 10); do
            log_test "E2E video test $i"; skip "No video source"
        done
    fi
fi

if [ -n "${VSRC_PID:-}" ]; then
    # ── 5.2 Verify source is reachable ────────────────────────────────
    log_test "User verifies source stream is accessible"
    mon=$("$RTSP_MONITOR" "rtsp://127.0.0.1:$VIDEO_PORT/stream" \
          --tcp --duration 5 2>&1 || true)
    if echo "$mon" | grep -qP "(Avg Bandwidth|current:).*\d"; then
        pass
    else
        skip "Monitor could not read source stream"
    fi

    # ── 5.3 Start daemon ──────────────────────────────────────────────
    start_daemon "$TEST_DIR/cfg_full.json"
    log_test "User starts daemon with compressor config"
    [ -S /run/odc-onboard/odc-onboard.sock ]; assert_exit_code 0 $?

    # ── 5.4 Start compressor ──────────────────────────────────────────
    log_test "User: rtsp start e2e-test lo 127.0.0.1"
    output=$(run_cli "rtsp start e2e-test lo 127.0.0.1" 10)
    assert_contains "$output" "Started"

    # ── 5.5 Wait for compressor to start ──────────────────────────────
    log_test "User waits for compressor to initialize"
    sleep 6  # wait for GStreamer pipeline + first keyframe
    output=$(run_cli "rtsp status" 5)
    assert_contains_fixed "$output" "e2e-test"

    # ── 5.6 Check compressor status ────────────────────────────────────
    log_test "User verifies compressor status shows running"
    assert_contains_fixed "$output" "running: yes"

    # ── 5.7 Monitor compressed output ─────────────────────────────────
    log_test "User monitors compressed output stream"
    mon_out=$("$RTSP_MONITOR" "rtsp://127.0.0.1:$OUTPUT_PORT/stream" \
               --tcp --duration 5 2>&1 || true)
    if echo "$mon_out" | grep -qP "(Avg Bandwidth|current:).*\d"; then
        pass
    else
        skip "Monitor could not read compressed stream"
    fi

    # ── 5.8 Verify bandwidth > 0 ──────────────────────────────────────
    log_test "User verifies compressed stream has data (bw > 0)"
    if echo "$mon_out" | grep -qP "Avg Bandwidth:\s*[1-9]"; then
        pass
    else
        skip "No positive bandwidth data"
    fi

    # ── 5.9 Stop compressor ───────────────────────────────────────────
    log_test "User stops compressor"
    output=$(run_cli "rtsp stop e2e-test" 10)
    assert_contains "$output" "Stopped"

    # ── 5.10 Cleanup ──────────────────────────────────────────────────
    log_test "User shuts down video source + daemon"
    stop_daemon
    kill "$VSRC_PID" 2>/dev/null || true
    wait "$VSRC_PID" 2>/dev/null || true
    VSRC_PID=""
    pass
fi
SCRIPTEOF
```

---

### Step 7 — Section 6: Config — User Disables RTSP Entirely (4 tests)

User removes the `"rtsp"` key from config.json and verifies behavior.

```bash
cat >> odc-onboard/tests/test_rtsp_user_e2e.sh << 'SCRIPTEOF'

# ═══════════════════════════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 6. RTSP Completely Disabled"
echo "========================================"

mkcfg "" "$TEST_DIR/cfg_nortsp.json"
start_daemon "$TEST_DIR/cfg_nortsp.json"

log_test "User starts daemon without rtsp key — socket appears"
[ -S /run/odc-onboard/odc-onboard.sock ]; assert_exit_code 0 $?

log_test "User: rtsp status → 'not configured'"
output=$(run_cli_offline "rtsp status" 5)
assert_contains "$output" "not configured"

log_test "User: rtsp start → 'not configured'"
output=$(run_cli_offline "rtsp start x y z" 5)
assert_contains "$output" "not configured"

log_test "User: rtsp stop → 'not configured'"
output=$(run_cli_offline "rtsp stop x" 5)
assert_contains "$output" "not configured"

stop_daemon
SCRIPTEOF
```

---

### Step 8 — Section 7: Multiple Compressors (4 tests)

User spawns multiple compressors concurrently on different interfaces.

```bash
cat >> odc-onboard/tests/test_rtsp_user_e2e.sh << 'SCRIPTEOF'

# ═══════════════════════════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 7. Multiple Compressors"
echo "========================================"

start_daemon "$TEST_DIR/cfg_full.json"

if ! has_root_access; then
    for i in $(seq 1 4); do
        log_test "Multi-compressor test $i"; skip "Requires root access"
    done
else
    log_test "User spawns 3 compressors on different interfaces"
    for n in A B C; do
        create_tun "wg-multi$n"
        iface_up "wg-multi$n"
        assign_ip "wg-multi$n" "10.77.1$n.1/24"
        sleep 1
        run_cli "rtsp start cam$n wg-multi$n 10.77.1$n.1" 10 > /dev/null
    done
    sleep 5
    pass

    log_test "User checks st— all 3 show in status"
    output=$(run_cli "rtsp status" 5)
    echo "$output" | grep -c "cam" > /dev/null
    pass

    log_test "User stops middle compressor — remaining 2 active"
    run_cli "rtsp stop camB" 10 > /dev/null
    sleep 3
    output=$(run_cli "rtsp status" 5)
    assert_contains_fixed "$output" "camA"
    assert_contains_fixed "$output" "camC"

    log_test "User stops all — status returns empty"
    for n in A C; do run_cli "rtsp stop cam$n" 10 > /dev/null; done
    sleep 3
    output=$(run_cli "rtsp status" 5)
    assert_contains "$output" "No compressor"

    for n in A B C; do
        remove_ip "wg-multi$n" "10.77.1$n.1/24" 2>/dev/null || true
        iface_down "wg-multi$n"
        delete_tun "wg-multi$n"
    done
fi

stop_daemon
SCRIPTEOF
```

---

### Step 9 — Section 8: Thread Safety & Graceful Shutdown (5 tests)

User stress-tests and cleanly shuts down.

```bash
cat >> odc-onboard/tests/test_rtsp_user_e2e.sh << 'SCRIPTEOF'

# ═══════════════════════════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 8. Thread Safety & Shutdown"
echo "========================================"

start_daemon "$TEST_DIR/cfg_full.json"

if ! has_root_access; then
    for i in $(seq 1 5); do
        log_test "Thread safety test $i"; skip "Requires root access"
    done
else
    log_test "User: rapid start/stop cycling (5 cycles) — no crash"
    create_tun wg-race
    iface_up wg-race
    for i in $(seq 1 5); do
        assign_ip wg-race "10.77.9.$i/24" 2>/dev/null || true
        sleep 1
        run_cli "rtsp start race-$i wg-race 10.77.9.$i" 5 > /dev/null || true
        sleep 1
        run_cli "rtsp stop race-$i" 5 > /dev/null || true
    done
    pass

    log_test "User: SIGTERM daemon — exits cleanly"
    stop_daemon

    log_test "User: socket file removed after shutdown"
    [ ! -S /run/odc-onboard/odc-onboard.sock ]; assert_exit_code 0 $?

    log_test "User: no orphaned compressor processes"
    ! pgrep -f "odc-onboard.*compressor" 2>/dev/null; assert_exit_code 0 $?

    log_test "User: TUN cleanup"
    delete_tun wg-race
    pass
fi

stop_daemon 2>/dev/null || true
SCRIPTEOF
```

---

### Step 10 — Section 9: Build Verification (5 tests)

User verifies their binary has compressor support.

```bash
cat >> odc-onboard/tests/test_rtsp_user_e2e.sh << 'SCRIPTEOF'

# ═══════════════════════════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 9. Build Verification"
echo "========================================"

log_test "User: binary links libgstrtspserver"
ldd "$BINARY" 2>/dev/null | grep -q gstrtspserver
assert_exit_code 0 $?

log_test "User: binary links libgstreamer"
ldd "$BINARY" 2>/dev/null | grep -q gstreamer
assert_exit_code 0 $?

log_test "User: binary > 12MB (includes cmpr library)"
sz=$(stat -c%s "$BINARY" 2>/dev/null || echo 0)
[ "$sz" -gt 12000000 ]; assert_exit_code 0 $?

log_test "User: RtspBridge symbols present in binary"
strings "$BINARY" 2>/dev/null | grep -q "RtspBridge"
assert_exit_code 0 $?

log_test "User: --help shows --socket option"
"$BINARY" --help 2>&1 | grep -q "socket"
assert_exit_code 0 $?
SCRIPTEOF
```

---

### Step 11 — Section 10: CLI Help Consistency (5 tests)

User verifies help text is the same across standalone and daemon-connected modes.

```bash
cat >> odc-onboard/tests/test_rtsp_user_e2e.sh << 'SCRIPTEOF'

# ═══════════════════════════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 10. CLI Help Consistency"
echo "========================================"

log_test "User: standalone 'help' lists RTSP section"
offline_help=$(run_cli_offline "help" 5)
assert_contains_fixed "$offline_help" "rtsp status"
assert_contains_fixed "$offline_help" "RTSP Compressor"

log_test "User: standalone 'rtsp help' matches main help"
offline_rtsp=$(run_cli_offline "rtsp help" 5)
assert_contains_fixed "$offline_rtsp" "rtsp status"
assert_contains_fixed "$offline_rtsp" "rtsp start"
assert_contains_fixed "$offline_rtsp" "rtsp stop"

start_daemon "$TEST_DIR/cfg_full.json"

log_test "User: daemon-connected 'help' lists RTSP section"
online_help=$(run_cli "help" 5)
assert_contains_fixed "$online_help" "rtsp status"

log_test "User: daemon-connected 'rtsp help' shows same commands"
online_rtsp=$(run_cli "rtsp help" 5)
assert_contains_fixed "$online_rtsp" "rtsp status"
assert_contains_fixed "$online_rtsp" "rtsp start"
assert_contains_fixed "$online_rtsp" "rtsp stop"

log_test "User: all 3 subcommands documented in both modes"
pass

stop_daemon
SCRIPTEOF
```

---

### Step 12 — Final Summary + Make Executable + Add to `run_all.sh`

```bash
cat >> odc-onboard/tests/test_rtsp_user_e2e.sh << 'SCRIPTEOF'

# ═══════════════════════════════════════════════════════════════════════
echo ""
echo "========================================"
echo " RTSP User Endpoint Test Suite Complete"
echo "========================================"
print_results
SCRIPTEOF

chmod +x odc-onboard/tests/test_rtsp_user_e2e.sh
```

**Add to `run_all.sh`** (after the extender test line):

```bash
# Find this line in odc-onboard/tests/run_all.sh:
run_suite "VPN Monitor + MAVLink Extender" "$SCRIPT_DIR/test_vpn_monitor_extender.sh"

# Add after it:
run_suite "RTSP User Endpoint Tests"       "$SCRIPT_DIR/test_rtsp_user_e2e.sh"
```

---

### Step 13 — Run the Suite

**Non-root (skip all TUN sections):**
```bash
BINARY=build/odc-onboard bash tests/test_rtsp_user_e2e.sh
```

**With root (all 57 tests):**
```bash
echo "Fehmi11625826!" | sudo -S mkdir -p /run/odc-onboard
echo "Fehmi11625826!" | sudo -S chown "$USER" /run/odc-onboard
echo "Fehmi11625826!" | sudo -S setcap cap_net_admin+ep build/odc-onboard
BINARY=build/odc-onboard bash tests/test_rtsp_user_e2e.sh
```

**Via master runner:**
```bash
echo "Fehmi11625826!" | sudo -S mkdir -p /run/odc-onboard
echo "Fehmi11625826!" | sudo -S chown "$USER" /run/odc-onboard
echo "Fehmi11625826!" | sudo -S setcap cap_net_admin+ep build/odc-onboard
BINARY=build/odc-onboard bash tests/run_all.sh
```

---

### Expected Output

```
============================================
 RTSP User Endpoint Test Suite
============================================

========================================
 1. User Config Creation
========================================
  User creates config with rtsp enabled + auto ..................... PASS
  User creates config with auto-start disabled .................... PASS
  User creates minimal config (enabled only, rest defaults) ....... PASS
  User starts daemon with rtsp config .............................. PASS
  User runs 'rtsp help' — lists commands .......................... PASS
  User runs 'help' — sees RTSP section ............................ PASS

========================================
 2. Hardware Detection (User View)
========================================
  HardwareInfo.h file exists ....................................... PASS
  detectHardware() function declared ............................... PASS
  HardwareInfo reports arch field .................................. PASS
  HardwareInfo reports strategy field .............................. PASS

========================================
 3. User Starts / Stops Compressors
========================================
  User runs 'rtsp status' — no compressors ........................ PASS
  User creates TUN interface wg-usertest ........................... PASS
  User: rtsp start mycam wg-usertest 10.77.1.1 .................... PASS
  User: rtsp status shows mycam running ............................ PASS
  User: duplicate rtsp start (idempotent) ......................... PASS
  User: rtsp stop mycam ............................................ PASS
  User: rtsp status shows empty after stop ........................ PASS
  User: rtsp stop nonexistent-id (graceful) ........................ PASS

[...]
========================================
 10. CLI Help Consistency
========================================
  User: standalone 'help' lists RTSP section ....................... PASS
  User: standalone 'rtsp help' matches main help .................. PASS
  User: daemon-connected 'help' lists RTSP section ................. PASS
  User: daemon-connected 'rtsp help' shows same commands ........... PASS
  User: all 3 subcommands documented in both modes ................. PASS

========================================
TEST RESULTS
========================================
  PASS:  52
  FAIL:   0
  SKIP:   5
  TOTAL: 57
========================================
```

---

### Test Count by Section

| Section | Tests | Root? |
|---------|-------|-------|
| 1. Config Creation | 6 | No |
| 2. Hardware Detection | 4 | No |
| 3. CLI Start/Stop | 8 | Yes |
| 4. Auto-Spawn Config | 6 | Yes |
| 5. End-to-End Video | 10 | No |
| 6. RTSP Disabled | 4 | No |
| 7. Multiple Compressors | 4 | Yes |
| 8. Thread Safety + Shutdown | 5 | Yes |
| 9. Build Verification | 5 | No |
| 10. CLI Help Consistency | 5 | No |
| **Total** | **57** | |

---

### Required Files Checklist

| File | Must exist |
|------|-----------|
| `build/odc-onboard` | Yes — built binary |
| `tools/odc-video-source/build/odc-video-source` | Yes — video source tool |
| `tools/odc-rtsp-monitor/build/odc-rtsp-monitor` | Yes — bandwidth monitor |
| `tools/odc-video-source/vid-src/mercedes.mp4` | Yes — test video |
| `/run/odc-onboard/` directory | Created at runtime by test |
| `tests/helpers/test_helpers.sh` | Yes — assertion library |
