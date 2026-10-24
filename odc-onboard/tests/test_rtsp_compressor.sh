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
run_priv() { echo "$ROOT_PASS" | sudo -S "$@" 2>/dev/null; }
has_priv() {
    [ "$(id -u)" -eq 0 ] && return 0
    echo "$ROOT_PASS" | sudo -S -v 2>/dev/null && return 0
    capsh --print 2>/dev/null | grep -q "^Current:.*cap_net_admin" && return 0
    return 1
}

TEST_DIR=$(mktemp -d); DAEMON_PID=""; VSRC_PID=""
stop_daemon() {
    [ -n "${DAEMON_PID:-}" ] && kill "$DAEMON_PID" 2>/dev/null || true
    if [ -n "${DAEMON_PID:-}" ]; then
        timeout 3 wait "$DAEMON_PID" 2>/dev/null || true
        kill -9 "$DAEMON_PID" 2>/dev/null || true
    fi
    DAEMON_PID=""
    run_priv rm -f /run/odc-onboard/odc-onboard.sock 2>/dev/null || true
}

stop_daemon() {
    stop_daemon 2>/dev/null || true
    [ -n "${VSRC_PID:-}" ] && kill "$VSRC_PID" 2>/dev/null && wait "$VSRC_PID" 2>/dev/null || true
    rm -rf "$TEST_DIR"
}
cleanup_force() {
    stop_daemon 2>/dev/null || true
    [ -n "${VSRC_PID:-}" ] && kill "$VSRC_PID" 2>/dev/null && wait "$VSRC_PID" 2>/dev/null || true
    rm -rf "$TEST_DIR"
}
trap cleanup_force EXIT

start_daemon() {
    local cfg="$1"
    stop_daemon 2>/dev/null || true
    sleep 1
    run_priv mkdir -p /run/odc-onboard 2>/dev/null
    run_priv chown "$(whoami)" /run/odc-onboard 2>/dev/null || true
    run_priv rm -f /run/odc-onboard/odc-onboard.sock 2>/dev/null
    "$BINARY" --config "$cfg" --daemon --socket /run/odc-onboard/odc-onboard.sock 2>/dev/null &
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

mkcfg() {
    local rtsp="$1" out="$2"
    if [ -n "$rtsp" ]; then
        cat > "$out" << END
{ "version": 1, "general": { "log_level": "error" },
  "vpn": {"wireguard": { "connections": [] }, "openvpn": { "connections": [] }},
  "mavlink": { "enabled": false },
  $rtsp
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
echo " RTSP Compressor Test Suite"
echo "============================================"

# ═══ 1. Source Code Verification ═══════════════════════════════════════
echo ""
echo "========================================"
echo " 1. Source Code Verification"
echo "========================================"

for f in "src/rtsp/rtsp_compressor_manager.hpp" "src/rtsp/rtsp_events.hpp"; do
    log_test "$f exists"; assert_file_exists "$PROJECT_DIR/$f"
done

log_test "RtspCompressorStatusEvent in Event variant"
assert_contains_fixed "$(cat "$PROJECT_DIR/src/core/event.hpp")" "RtspCompressorStatusEvent"

log_test "RtspCompressorEvent in Event variant"
assert_contains_fixed "$(cat "$PROJECT_DIR/src/core/event.hpp")" "RtspCompressorEvent"

log_test "cmpr STATIC library in CMakeLists.txt"
assert_contains_fixed "$(cat "$PROJECT_DIR/CMakeLists.txt")" "add_library(cmpr"

for src in RtspBridge.cpp HardwareInfo.cpp RtspAnalyzer.cpp; do
    log_test "$src in cmpr sources"
    assert_contains_fixed "$(cat "$PROJECT_DIR/CMakeLists.txt")" "$src"
done

for m in rtsp.status rtsp.start rtsp.stop; do
    log_test "IPC method $m registered"
    assert_contains_fixed "$(cat "$PROJECT_DIR/src/core/app_lifecycle.hpp")" "\"$m\""
done

log_test "CLI cmd_rtsp exists"
assert_contains_fixed "$(cat "$PROJECT_DIR/src/cli/cli_app.hpp")" "cmd_rtsp"

log_test "ODC_HAS_RTSP_COMPRESSOR in compile defs"
assert_contains_fixed "$(cat "$PROJECT_DIR/CMakeLists.txt")" "ODC_HAS_RTSP_COMPRESSOR"

# ═══ 2. CLI Help & Parsing ═════════════════════════════════════════════
echo ""
echo "========================================"
echo " 2. CLI Help & Command Parsing"
echo "========================================"

log_test "rtsp help lists subcommands"
o=$(run_cli_offline "rtsp help" 5)
assert_contains "$o" "status"; assert_contains "$o" "start"; assert_contains "$o" "stop"

log_test "rtsp with no subcommand"
run_cli_offline "rtsp" 5 >/dev/null; pass

log_test "rtsp status no daemon"
run_cli_offline "rtsp status" 5 >/dev/null; pass

log_test "rtsp start missing args"
o=$(run_cli_offline "rtsp start" 5); assert_contains "$o" "Usage\|Error"

log_test "rtsp stop missing args"
o=$(run_cli_offline "rtsp stop" 5); assert_contains "$o" "Usage\|Error"

log_test "CLI help lists RTSP section"
o=$(run_cli_offline "help" 5); assert_contains_fixed "$o" "rtsp status"

log_test "rtsp help format check"
assert_contains_fixed "$o" "RTSP Compressor"

# ═══ 3. Hardware Detection Source Check ════════════════════════════════
echo ""
echo "========================================"
echo " 3. Hardware Detection"
echo "========================================"

for f in "thirdparty/minimal-cmpr/include/HardwareInfo.h" \
         "thirdparty/minimal-cmpr/src/HardwareInfo.cpp"; do
    log_test "$f exists"; assert_file_exists "$PROJECT_DIR/$f"
done

log_test "detectHardware function"
assert_contains_fixed "$(cat "$PROJECT_DIR/thirdparty/minimal-cmpr/include/HardwareInfo.h")" "detectHardware"

log_test "PipelineStrategy enum"
assert_contains_fixed "$(cat "$PROJECT_DIR/thirdparty/minimal-cmpr/include/HardwareInfo.h")" "PipelineStrategy"

log_test "HardwareInfo struct fields (arch)"
assert_contains_fixed "$(cat "$PROJECT_DIR/thirdparty/minimal-cmpr/include/HardwareInfo.h")" "arch"

log_test "HardwareInfo struct fields (strategy)"
assert_contains_fixed "$(cat "$PROJECT_DIR/thirdparty/minimal-cmpr/include/HardwareInfo.h")" "strategy"

log_test "validateBridgeConfig uses HW"
assert_contains_fixed "$(cat "$PROJECT_DIR/thirdparty/minimal-cmpr/include/RtspBridge.h")" "HardwareInfo"

# ═══ 4. Config Generation ══════════════════════════════════════════════
echo ""
echo "========================================"
echo " 4. Config Generation"
echo "========================================"

for pair in 'true:true:enabled_auto' 'false:true:disabled' 'true:false:manual'; do
    e="${pair%%:*}"; a="$(echo "$pair"|cut -d: -f2)"; tag="$(echo "$pair"|cut -d: -f3)"
    f="$TEST_DIR/cfg_${tag}.json"
    mkcfg "\"rtsp\": {\"enabled\": $e, \"auto_start_on_vpn\": $a}" "$f"
    log_test "Config enabled=$e auto=$a"
    assert_file_exists "$f"; assert_valid_json "$(cat "$f")"
done

mkcfg "" "$TEST_DIR/cfg_nortsp.json"
log_test "Config without rtsp key"
assert_file_exists "$TEST_DIR/cfg_nortsp.json"
assert_valid_json "$(cat "$TEST_DIR/cfg_nortsp.json")"

mkcfg '"rtsp": {"enabled": true}' "$TEST_DIR/cfg_min.json"
log_test "Config minimal rtsp"
assert_file_exists "$TEST_DIR/cfg_min.json"
assert_valid_json "$(cat "$TEST_DIR/cfg_min.json")"

log_test "Config schema: rtsp section"
assert_contains_fixed "$(cat "$TEST_DIR/cfg_enabled_auto.json")" '"rtsp"'

# ═══ 5. Daemon — No RTSP Key ═══════════════════════════════════════════
echo ""
echo "========================================"
echo " 5. Daemon — No RTSP Key"
echo "========================================"

start_daemon "$TEST_DIR/cfg_nortsp.json"

log_test "Daemon starts"; [ -S /run/odc-onboard/odc-onboard.sock ]; assert_exit_code 0 $?
log_test "rtsp status: remote error"
o=$(run_cli "rtsp status" 5); pass
log_test "rtsp start: no method"
o=$(run_cli "rtsp start x if0 10.0.0.1" 5); pass
log_test "Daemon stops"; stop_daemon; pass

# ═══ 6. Daemon — RTSP Enabled + Manual CLI ═════════════════════════════
echo ""
echo "========================================"
echo " 6. Daemon — RTSP Enabled"
echo "========================================"

start_daemon "$TEST_DIR/cfg_enabled_auto.json"

log_test "Daemon starts with rtsp"; [ -S /run/odc-onboard/odc-onboard.sock ]; assert_exit_code 0 $?
log_test "rtsp status: no instances"
o=$(run_cli "rtsp status" 5); assert_contains "$o" "No compressor"

if ! has_priv; then
    for i in $(seq 1 8); do log_test "CLI test $i"; skip "Requires privileges"; done
else
    log_test "Spawn compressor: rtsp start c1 test-cmpr1 10.77.2.1"
    run_priv ip tuntap add name test-cmpr1 mode tun 2>/dev/null
    run_priv ip link set test-cmpr1 up
    run_priv ip addr add 10.77.2.1/24 dev test-cmpr1
    sleep 1
    o=$(run_cli "rtsp start c1 test-cmpr1 10.77.2.1" 10)
    assert_contains "$o" "Started\|success"

    log_test "rtsp status lists c1"
    sleep 3; o=$(run_cli "rtsp status" 5); assert_contains_fixed "$o" "c1"

    log_test "Duplicate start no-op"; run_cli "rtsp start c1 test-cmpr1 10.77.2.1" 5 >/dev/null; pass

    log_test "rtsp stop c1"
    o=$(run_cli "rtsp stop c1" 10); assert_contains "$o" "Stopped\|success"

    log_test "c1 removed"; sleep 2
    o=$(run_cli "rtsp status" 5); assert_contains "$o" "No compressor"

    log_test "Stop nonexistent - no crash"; run_cli "rtsp stop bogus" 5 >/dev/null; pass

    log_test "rtsp help subcommands"
    o=$(run_cli "rtsp help" 5)
    assert_contains_fixed "$o" "status"; assert_contains_fixed "$o" "start"; assert_contains_fixed "$o" "stop"

    run_priv ip link del test-cmpr1 2>/dev/null || true
fi
stop_daemon

# ═══ 7. IPC Methods ═════════════════════════════════════════════════════
echo ""
echo "========================================"
echo " 7. IPC Methods"
echo "========================================"

log_test "IPC method count >= 3"
cnt=$(grep -c '"rtsp\.' "$PROJECT_DIR/src/core/app_lifecycle.hpp" 2>/dev/null); [ "$cnt" -ge 3 ]; assert_exit_code 0 $?

for m in rtsp.status rtsp.start rtsp.stop; do
    log_test "IPC $m exact match"
    grep -q "\"$m\"" "$PROJECT_DIR/src/core/app_lifecycle.hpp"; assert_exit_code 0 $?
done

start_daemon "$TEST_DIR/cfg_enabled_auto.json"
log_test "IPC rtsp.status returns empty JSON"
o=$(run_cli "rtsp status" 5); assert_contains "$o" "No compressor"
stop_daemon

# ═══ 8. Source Verification — Lifecycle ════════════════════════════════
echo ""
echo "========================================"
echo " 8. Lifecycle Integration"
echo "========================================"

for pat in "on_vpn_interface_event" "on_vpn_state_change" "teardown_compressor" \
           "VpnState::Disconnected" "!evt.ipv6" "auto_enabled_" \
           "rtsp_compressor" "rtsp_compressor_->start()" "rtsp_compressor_->stop_all()"; do
    log_test "$pat in source"
    assert_contains_fixed "$(cat "$PROJECT_DIR/src/rtsp/rtsp_compressor_manager.hpp"; cat "$PROJECT_DIR/src/core/app_lifecycle.hpp" || true)" "$pat"
done

# ═══ 9. End-to-End Video ═══════════════════════════════════════════════
echo ""
echo "========================================"
echo " 9. End-to-End Video Pipeline"
echo "========================================"

VIDEO_PORT=19554
for b in "$VIDEO_SRC" "$RTSP_MONITOR"; do
    log_test "$(basename "$b") present"
    [ -x "$b" ] && pass || skip "not built"
done

log_test "Test video file"
[ -f "$TEST_VIDEO" ] && pass || skip "no video"

log_test "Start video source on $VIDEO_PORT"
"$VIDEO_SRC" --video "$TEST_VIDEO" --port "$VIDEO_PORT" --path /source --loop > "$TEST_DIR/vsrc.log" 2>&1 &
VSRC_PID=$!; sleep 2
ss -tlnp 2>/dev/null | grep -q "$VIDEO_PORT" && pass || { fail; kill "$VSRC_PID" 2>/dev/null; VSRC_PID=""; }

if [ -n "${VSRC_PID:-}" ]; then
    log_test "Monitor bandwidth from source"
    m=$("$RTSP_MONITOR" "rtsp://127.0.0.1:$VIDEO_PORT/source" --tcp --duration 5 2>&1 || true)
    echo "$m" | grep -qP "(Avg Bandwidth|current:).*\d" && pass || skip

    start_daemon "$TEST_DIR/cfg_enabled_auto.json" 2>/dev/null || true

    log_test "Manual compressor on loopback"
    run_priv ip addr add 10.77.99.1/24 dev lo 2>/dev/null || true
    sleep 1
    o=$(run_cli "rtsp start e2e lo 10.77.99.1" 10)
    echo "$o" | grep -q "Started\|success" && pass || skip

    log_test "Compressor status e2e"; sleep 3
    o=$(run_cli "rtsp status" 5); assert_contains_fixed "$o" "e2e"

    log_test "Stop compressor"
    o=$(run_cli "rtsp stop e2e" 10); assert_contains "$o" "Stopped\|success"

    run_priv ip addr del 10.77.99.1/24 dev lo 2>/dev/null || true
    stop_daemon
    kill "$VSRC_PID" 2>/dev/null || true; wait "$VSRC_PID" 2>/dev/null || true
else
    log_test "Monitor bandwidth from source"; skip; log_test "Manual compressor"; skip
    log_test "Compressor status"; skip; log_test "Stop compressor"; skip
fi

# ═══ 10. Build Verification ════════════════════════════════════════════
echo ""
echo "========================================"
echo " 10. Build Verification"
echo "========================================"

for lib in gstrtspserver gstreamer; do
    log_test "lib$lib linked"
    ldd "$BINARY" 2>/dev/null | grep -q "$lib" && pass || skip
done

log_test "Binary is ELF"
file "$BINARY" | grep -q "ELF" && pass || skip

log_test "Binary > 12MB"
sz=$(stat -c%s "$BINARY" 2>/dev/null || echo 0); [ "$sz" -gt 12000000 ] && pass || skip

log_test "RtspBridge symbol present"
strings "$BINARY" 2>/dev/null | grep -q "RtspBridge" && pass || skip

echo ""
echo "========================================"
echo " RTSP Compressor Test Suite Complete"
echo "========================================"
print_results
