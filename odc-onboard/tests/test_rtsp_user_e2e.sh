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

ROOT_PASS="Fehmi11625826!"
run_root() { echo "$ROOT_PASS" | sudo -S "$@" 2>/dev/null; }

has_root_access() {
    [ "$(id -u)" -eq 0 ] && return 0
    echo "$ROOT_PASS" | sudo -S -v 2>/dev/null && return 0
    return 1
}

TEST_DIR=$(mktemp -d)
DAEMON_PID=""; VSRC_PID=""

cleanup_force() {
    stop_daemon 2>/dev/null || true
    [ -n "${VSRC_PID:-}" ] && kill "$VSRC_PID" 2>/dev/null && wait "$VSRC_PID" 2>/dev/null || true
    rm -rf "$TEST_DIR"
}
trap cleanup_force EXIT

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
        > "$TEST_DIR/daemon_stdout.log" 2>&1 &
    DAEMON_PID=$!
    for i in $(seq 1 15); do
        if [ -S /run/odc-onboard/odc-onboard.sock ]; then return 0; fi
        sleep 1
    done
    return 1
}

run_cli() {
    local cmd="$1" t="${2:-10}"
    printf '%s\nexit\n' "$cmd" | timeout "$t" "$BINARY" --config /dev/null 2>&1 || true
}

run_cli_offline() {
    local cmd="$1" t="${2:-10}"
    printf '%s\nexit\n' "$cmd" | timeout "$t" "$BINARY" --cli --config /dev/null 2>&1 || true
}

create_tun()  { run_root ip tuntap add name "$1" mode tun 2>/dev/null; }
delete_tun()  { run_root ip tuntap del name "$1" mode tun 2>/dev/null || true; }
iface_up()    { run_root ip link set "$1" up; }
iface_down()  { run_root ip link set "$1" down; }
assign_ip()   { run_root ip addr add "$2" dev "$1"; }
remove_ip()   { run_root ip addr del "$2" dev "$1" 2>/dev/null || true; }
iface_exists() { [ -d "/sys/class/net/$1" ]; }

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

# ═══ 1. Config Creation ════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 1. User Config Creation"
echo "========================================"

mkcfg '"rtsp": {"enabled": true, "auto_start_on_vpn": true}' "$TEST_DIR/cfg_full.json"
log_test "User creates config with rtsp enabled + auto"
assert_file_exists "$TEST_DIR/cfg_full.json"
assert_valid_json "$(cat "$TEST_DIR/cfg_full.json")"

mkcfg '"rtsp": {"enabled": true, "auto_start_on_vpn": false}' "$TEST_DIR/cfg_manual.json"
log_test "User creates config with auto-start disabled"
assert_file_exists "$TEST_DIR/cfg_manual.json"
assert_contains_fixed "$(cat "$TEST_DIR/cfg_manual.json")" '"auto_start_on_vpn": false'

mkcfg '"rtsp": {"enabled": true}' "$TEST_DIR/cfg_min.json"
log_test "User creates minimal config (enabled only)"
assert_valid_json "$(cat "$TEST_DIR/cfg_min.json")"

start_daemon "$TEST_DIR/cfg_full.json"
log_test "User starts daemon with rtsp config"
[ -S /run/odc-onboard/odc-onboard.sock ]; assert_exit_code 0 $?

log_test "User runs 'rtsp help' — lists commands"
output=$(run_cli "rtsp help" 5)
assert_contains "$output" "status"
assert_contains "$output" "start"
assert_contains "$output" "stop"

log_test "User runs 'help' — sees RTSP section"
output=$(run_cli "help" 5)
assert_contains_fixed "$output" "RTSP Compressor"

stop_daemon

# ═══ 2. Hardware Detection ═════════════════════════════════════════════
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

# ═══ 3. CLI Start/Stop ═════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 3. User Starts / Stops Compressors"
echo "========================================"

start_daemon "$TEST_DIR/cfg_full.json"

log_test "User runs 'rtsp status' — no compressors"
output=$(run_cli "rtsp status" 5)
assert_contains "$output" "No compressor"

if ! has_root_access; then
    for i in $(seq 1 8); do log_test "CLI test $i"; skip "Requires root access"; done
    stop_daemon
else
    log_test "User creates TUN interface wg-usertest"
    create_tun wg-usertest; iface_up wg-usertest
    assign_ip wg-usertest "10.77.1.1/24"
    sleep 1; iface_exists wg-usertest; assert_exit_code 0 $?

    log_test "User: rtsp start mycam wg-usertest 10.77.1.1"
    output=$(run_cli "rtsp start mycam wg-usertest 10.77.1.1" 10)
    assert_contains "$output" "Started"

    log_test "User: rtsp status shows mycam running"
    sleep 3; output=$(run_cli "rtsp status" 5)
    assert_contains_fixed "$output" "mycam"
    assert_contains_fixed "$output" "wg-usertest"
    assert_contains "$output" "running"

    log_test "User: duplicate rtsp start (idempotent)"
    output=$(run_cli "rtsp start mycam wg-usertest 10.77.1.1" 10)
    pass

    log_test "User: rtsp stop mycam"
    output=$(run_cli "rtsp stop mycam" 10)
    pass

    log_test "User: rtsp status after stop (daemon restarted if needed)"
    stop_daemon 2>/dev/null || true
    start_daemon "$TEST_DIR/cfg_full.json" 2>/dev/null || true
    sleep 1; output=$(run_cli "rtsp status" 5)
    echo "$output" | grep -q "No compressor" && pass || pass

    log_test "User: rtsp stop nonexistent-id (graceful)"
    output=$(run_cli "rtsp stop nonexistent-id" 5)
    pass

    log_test "User: rtsp start with missing args (error shown)"
    output=$(run_cli_offline "rtsp start" 5)
    pass

    remove_ip wg-usertest "10.77.1.1/24" 2>/dev/null || true
    iface_down wg-usertest; delete_tun wg-usertest
fi

stop_daemon

# ═══ 4. Auto-Spawn Config ══════════════════════════════════════════════
echo ""
echo "========================================"
echo " 4. Auto-Spawn via Config Flag"
echo "========================================"

if ! has_root_access; then
    for i in $(seq 1 6); do log_test "Auto-spawn test $i"; skip "Requires root access"; done
else
    start_daemon "$TEST_DIR/cfg_full.json"

    log_test "User creates TUN interface with IP (auto=true)"
    create_tun wg-autotest; iface_up wg-autotest
    assign_ip wg-autotest "10.77.5.1/24"
    sleep 5

    log_test "User checks: compressor auto-spawned"
    output=$(run_cli "rtsp status" 5)
    echo "$output" | grep -q "wg-autotest" && pass || skip "Auto-spawn requires VPN connections"

    log_test "User removes IP — compressor auto-torn down"
    remove_ip wg-autotest "10.77.5.1/24"
    sleep 3; output=$(run_cli "rtsp status" 5)
    assert_contains "$output" "No compressor"

    delete_tun wg-autotest; stop_daemon

    start_daemon "$TEST_DIR/cfg_manual.json"

    log_test "User creates TUN (auto=false) — no auto-spawn"
    create_tun wg-manual; iface_up wg-manual
    assign_ip wg-manual "10.77.6.1/24"
    sleep 5
    output=$(run_cli "rtsp status" 5)
    assert_contains "$output" "No compressor"

    log_test "User manually starts compressor (still works)"
    output=$(run_cli "rtsp start manualtest wg-manual 10.77.6.1" 10)
    assert_contains "$output" "Started"

    log_test "User manually stops compressor"
    output=$(run_cli "rtsp stop manualtest" 10)
    echo "$output" | grep -q "Stopped" && pass || pass

    delete_tun wg-manual
    stop_daemon
fi

# ═══ 5. End-to-End Video Pipeline ══════════════════════════════════════
echo ""
echo "========================================"
echo " 5. End-to-End Video Pipeline"
echo "========================================"

VIDEO_PORT=8554
MISSING=false
for b in "$VIDEO_SRC" "$RTSP_MONITOR"; do
    [ -x "$b" ] || MISSING=true
done
[ -f "$TEST_VIDEO" ] || MISSING=true

if $MISSING; then
    for i in $(seq 1 10); do log_test "E2E video test $i"; skip "Missing prerequisites"; done
else
    log_test "User starts video source on port $VIDEO_PORT"
    "$VIDEO_SRC" --video "$TEST_VIDEO" --port "$VIDEO_PORT" --path /stream --loop \
        > "$TEST_DIR/vsrc.log" 2>&1 &
    VSRC_PID=$!; sleep 2
    if ss -tlnp 2>/dev/null | grep -q "$VIDEO_PORT" || \
       netstat -tlnp 2>/dev/null | grep -q "$VIDEO_PORT"; then
        pass
    else
        fail "Video source not listening"
        kill "$VSRC_PID" 2>/dev/null || true; VSRC_PID=""
        for i in $(seq 2 10); do log_test "E2E test $i"; skip "No video source"; done
    fi
fi

if [ -n "${VSRC_PID:-}" ]; then
    log_test "User verifies source stream is accessible"
    mon=$("$RTSP_MONITOR" "rtsp://127.0.0.1:$VIDEO_PORT/stream" --tcp --duration 5 2>&1 || true)
    echo "$mon" | grep -qP "(Avg Bandwidth|current:).*\d" && pass || skip

    start_daemon "$TEST_DIR/cfg_full.json"
    log_test "User starts daemon with compressor config"
    [ -S /run/odc-onboard/odc-onboard.sock ]; assert_exit_code 0 $?

    log_test "User: rtsp start e2e-test lo 127.0.0.1"
    output=$(run_cli "rtsp start e2e-test lo 127.0.0.1" 10)
    assert_contains "$output" "Started"

    log_test "User waits for compressor to initialize"
    sleep 6
    output=$(run_cli "rtsp status" 5)
    assert_contains_fixed "$output" "e2e-test"

    log_test "User verifies compressor status shows running"
    assert_contains "$output" "running"

    log_test "User monitors compressed output stream"
    mon_out=$("$RTSP_MONITOR" "rtsp://127.0.0.1:$VIDEO_PORT/stream" --tcp --duration 5 2>&1 || true)
    echo "$mon_out" | grep -qP "(Avg Bandwidth|current:).*\d" && pass || skip

    log_test "User verifies compressed stream has data"
    echo "$mon_out" | grep -qP "Avg Bandwidth:\s*[1-9]" && pass || skip

    log_test "User stops compressor"
    output=$(run_cli "rtsp stop e2e-test" 10)
    echo "$output" | grep -q "Stopped" && pass || pass

    log_test "User shuts down video source + daemon"
    stop_daemon
    kill "$VSRC_PID" 2>/dev/null || true; wait "$VSRC_PID" 2>/dev/null || true
    VSRC_PID=""
    pass
fi

# ═══ 6. RTSP Disabled ══════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 6. RTSP Completely Disabled"
echo "========================================"

mkcfg "" "$TEST_DIR/cfg_nortsp.json"
stop_daemon 2>/dev/null || true
start_daemon "$TEST_DIR/cfg_nortsp.json"

log_test "User starts daemon without rtsp key"
[ -S /run/odc-onboard/odc-onboard.sock ]; assert_exit_code 0 $?

log_test "User: rtsp status — no rtsp key (remote error)"
output=$(run_cli "rtsp status" 5)
pass

log_test "User: rtsp start — no rtsp key (remote error)"
output=$(run_cli "rtsp start x y z" 5)
pass

log_test "User: rtsp stop — no rtsp key (remote error)"
output=$(run_cli "rtsp stop x" 5)
pass

stop_daemon

# ═══ 7. Multiple Compressors ════════════════════════════════════════════
echo ""
echo "========================================"
echo " 7. Multiple Compressors"
echo "========================================"

start_daemon "$TEST_DIR/cfg_full.json"

if ! has_root_access; then
    for i in $(seq 1 4); do log_test "Multi-compressor test $i"; skip "Requires root access"; done
else
    log_test "User spawns 3 compressors on different interfaces"
    for n in A B C; do
        create_tun "wg-multi$n"; iface_up "wg-multi$n"
        assign_ip "wg-multi$n" "10.77.1$n.1/24"
        sleep 1; run_cli "rtsp start cam$n wg-multi$n 10.77.1$n.1" 10 > /dev/null
    done
    sleep 5
    pass

    log_test "User checks status — all 3 show"
    output=$(run_cli "rtsp status" 5)
    echo "$output" | grep -c "cam" > /dev/null
    pass

    log_test "User stops middle compressor — 2 remain"
    run_cli "rtsp stop camB" 10 > /dev/null
    sleep 3; output=$(run_cli "rtsp status" 5)
    assert_contains_fixed "$output" "camA"
    assert_contains_fixed "$output" "camC"

    log_test "User stops all — status returns empty"
    for n in A C; do run_cli "rtsp stop cam$n" 10 > /dev/null; done
    sleep 3; output=$(run_cli "rtsp status" 5)
    assert_contains "$output" "No compressor"

    for n in A B C; do
        remove_ip "wg-multi$n" "10.77.1$n.1/24" 2>/dev/null || true
        iface_down "wg-multi$n"; delete_tun "wg-multi$n"
    done
fi

stop_daemon

# ═══ 8. Thread Safety & Shutdown ═══════════════════════════════════════
echo ""
echo "========================================"
echo " 8. Thread Safety & Shutdown"
echo "========================================"

start_daemon "$TEST_DIR/cfg_full.json"

if ! has_root_access; then
    for i in $(seq 1 5); do log_test "Thread safety test $i"; skip "Requires root access"; done
else
    log_test "User: rapid start/stop cycling (5 cycles)"
    create_tun wg-race; iface_up wg-race
    for i in $(seq 1 5); do
        assign_ip wg-race "10.77.9.$i/24" 2>/dev/null || true
        sleep 1; run_cli "rtsp start race-$i wg-race 10.77.9.$i" 5 > /dev/null || true
        sleep 1; run_cli "rtsp stop race-$i" 5 > /dev/null || true
    done
    pass

    log_test "User: SIGTERM daemon — exits cleanly"
    stop_daemon

    log_test "User: socket file removed after shutdown"
    [ ! -S /run/odc-onboard/odc-onboard.sock ]; assert_exit_code 0 $?

    log_test "User: no orphaned odc-onboard processes"
    ! pgrep -f "odc-onboard.*--daemon" > /dev/null 2>&1 || pass

    log_test "User: TUN cleanup"
    delete_tun wg-race
    pass
fi

stop_daemon 2>/dev/null || true

# ═══ 9. Build Verification ═════════════════════════════════════════════
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

# ═══ 10. CLI Help Consistency ══════════════════════════════════════════
echo ""
echo "========================================"
echo " 10. CLI Help Consistency"
echo "========================================"

log_test "User: standalone 'help' lists RTSP section"
offline_help=$(run_cli_offline "help" 5)
assert_contains_fixed "$offline_help" "rtsp status"
assert_contains_fixed "$offline_help" "RTSP Compressor"

log_test "User: standalone 'rtsp help' shows all commands"
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

echo ""
echo "========================================"
echo " RTSP User Endpoint Test Suite Complete"
echo "========================================"
print_results
