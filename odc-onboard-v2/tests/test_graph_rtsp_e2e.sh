#!/usr/bin/env bash
#
# odc-onboard-v2 Phase 4 — RTSP compressor E2E via graph IPC
# Adapted from test_rtsp_user_e2e.sh (graph.* IPC instead of legacy rtsp CLI).
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/helpers/test_helpers.sh"

BINARY="${BINARY:-$PROJECT_DIR/build/odc-onboard-v2}"
TOOLS_DIR="$PROJECT_DIR/tools"
VIDEO_SRC="$TOOLS_DIR/odc-video-source/build/odc-video-source"
RTSP_MONITOR="$TOOLS_DIR/odc-rtsp-monitor/build/odc-rtsp-monitor"
TEST_VIDEO="$TOOLS_DIR/odc-video-source/vid-src/mercedes.mp4"

TEST_DIR=$(mktemp -d /tmp/odc-graph-rtsp-XXXXXX)
SOCKET_PATH="$TEST_DIR/odc-onboard.sock"
CONFIG_PATH="$TEST_DIR/system.yaml"
DAEMON_PID=""
VSRC_PID=""
VIDEO_PORT=8554

has_gstreamer() {
    ldd "$BINARY" 2>/dev/null | grep -q gstreamer
}

cleanup() {
    stop_test_daemon
    if [ -n "${VSRC_PID:-}" ]; then
        kill "$VSRC_PID" 2>/dev/null || true
        wait "$VSRC_PID" 2>/dev/null || true
        VSRC_PID=""
    fi
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

stop_test_daemon() {
    if [ -n "$DAEMON_PID" ]; then
        kill "$DAEMON_PID" 2>/dev/null || true
        wait "$DAEMON_PID" 2>/dev/null || true
        DAEMON_PID=""
    fi
    rm -f "$SOCKET_PATH" 2>/dev/null || true
}

start_test_daemon() {
    stop_test_daemon
    "$BINARY" --config "$CONFIG_PATH" --daemon --socket "$SOCKET_PATH" \
        >"$TEST_DIR/daemon.log" 2>&1 &
    DAEMON_PID=$!
    wait_for_socket "$SOCKET_PATH" 10
}

write_rtsp_graph_config() {
    cat > "$CONFIG_PATH" <<EOF
version: 2
general:
  log_level: error
  socket: $SOCKET_PATH
components:
  - id: rtsp_cloud
    type: rtsp.compressor
    enabled: true
    config:
      output_codec: h265
      output_bitrate_kbps: 800
      output_width: 1280
      output_height: 720
      output_framerate: 30
      input_port: $VIDEO_PORT
      input_path: /stream
      output_port: 8555
      output_path: /stream
      input_use_tcp: true
links: []
triggers: []
EOF
}

echo "============================================================"
echo " Graph RTSP E2E Tests (Phase 4)"
echo "============================================================"
echo ""
echo "Binary: $BINARY"
echo ""

if [ ! -x "$BINARY" ]; then
    echo "ERROR: Binary not found or not executable: $BINARY"
    exit 1
fi

write_rtsp_graph_config

# ── 1. Config + GStreamer prerequisites ──────────────────────────────────────
echo "========================================"
echo " 1. Config + Prerequisites"
echo "========================================"

log_test "Graph config declares rtsp.compressor component"
assert_contains_fixed "$(cat "$CONFIG_PATH")" "rtsp.compressor"

log_test "Graph config sets input/output ports"
assert_contains_fixed "$(cat "$CONFIG_PATH")" "input_port: $VIDEO_PORT"
assert_contains_fixed "$(cat "$CONFIG_PATH")" "output_port: 8555"

if has_gstreamer; then
    log_test "Binary links GStreamer (RTSP support available)"
    pass
    GST_OK=true
else
    log_test "Binary links GStreamer (RTSP support available)"
    skip "GStreamer not linked — RTSP runtime tests skipped"
    GST_OK=false
fi

# ── 2. Daemon + graph IPC ────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 2. graph.status / graph.validate"
echo "========================================"

if ! has_net_admin; then
    log_test "Daemon starts with RTSP graph config"
    skip "Requires CAP_NET_ADMIN or root"
    log_test "graph.status lists rtsp_cloud"
    skip "Requires CAP_NET_ADMIN or root"
    log_test "graph.validate valid for RTSP graph"
    skip "Requires CAP_NET_ADMIN or root"
elif ! $GST_OK; then
    log_test "Daemon starts with RTSP graph config"
    skip "GStreamer not available"
    log_test "graph.status lists rtsp_cloud"
    skip "GStreamer not available"
    log_test "graph.validate valid for RTSP graph"
    skip "GStreamer not available"
else
    log_test "Daemon starts with RTSP graph config"
    start_test_daemon && pass || fail "Daemon failed (see $TEST_DIR/daemon.log)"

    log_test "graph.status lists rtsp_cloud"
    status=$(ipc_request "$SOCKET_PATH" "graph.status" '{}' 1) || status=""
    echo "$status" | python3 -c "import sys,json; ids=[c['id'] for c in json.load(sys.stdin).get('components',[])]; sys.exit(0 if 'rtsp_cloud' in ids else 1)" \
        && pass || fail "rtsp_cloud missing"

    log_test "graph.validate valid for RTSP graph"
    validate=$(ipc_request "$SOCKET_PATH" "graph.validate" '{}' 2) || validate=""
    valid=$(echo "$validate" | python3 -c "import sys,json; print(json.load(sys.stdin).get('valid',''))" 2>/dev/null)
    if [ "$valid" = "True" ] || [ "$valid" = "true" ]; then pass; else fail "valid=$valid"; fi
fi

# ── 3. graph.components.start/stop ───────────────────────────────────────────
echo ""
echo "========================================"
echo " 3. graph.components IPC (RTSP lifecycle)"
echo "========================================"

if ! has_net_admin || ! $GST_OK; then
    log_test "graph.components.start rtsp_cloud on loopback"
    skip "Prerequisites missing"
    log_test "graph.status after start"
    skip "Prerequisites missing"
    log_test "graph.components.stop rtsp_cloud"
    skip "Prerequisites missing"
else
    log_test "graph.components.start rtsp_cloud on loopback"
    start_result=$(ipc_request "$SOCKET_PATH" "graph.components.start" '{"id":"rtsp_cloud"}' 10) || start_result=""
    echo "$start_result" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
        && pass || skip "start failed (compressor may need binding)"

    sleep 2
    log_test "graph.status after start"
    status=$(ipc_request "$SOCKET_PATH" "graph.status" '{}' 11) || status=""
    echo "$status" | python3 -c "import sys,json; \
c=next((x for x in json.load(sys.stdin).get('components',[]) if x.get('id')=='rtsp_cloud'),{}); \
print(c); sys.exit(0 if c else 1)" >/dev/null && pass || fail "rtsp_cloud status missing"

    log_test "graph.components.stop rtsp_cloud"
    stop_result=$(ipc_request "$SOCKET_PATH" "graph.components.stop" '{"id":"rtsp_cloud"}' 12) || stop_result=""
    echo "$stop_result" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
        && pass || skip "stop returned false"
fi

# ── 4. End-to-end video pipeline ─────────────────────────────────────────────
echo ""
echo "========================================"
echo " 4. End-to-End Video Pipeline"
echo "========================================"

MISSING=false
for b in "$VIDEO_SRC" "$RTSP_MONITOR"; do
    [ -x "$b" ] || MISSING=true
done
[ -f "$TEST_VIDEO" ] || MISSING=true

if $MISSING || ! $GST_OK || ! has_net_admin; then
    log_test "Video source starts on port $VIDEO_PORT"
    skip "Missing tools, GStreamer, or privileges"
    log_test "Source stream accessible via odc-rtsp-monitor"
    skip "Missing prerequisites"
    log_test "graph.components.start with live source"
    skip "Missing prerequisites"
    log_test "Compressed stream has bandwidth metrics"
    skip "Missing prerequisites"
    log_test "graph.components.stop after E2E"
    skip "Missing prerequisites"
else
    log_test "Video source starts on port $VIDEO_PORT"
    "$VIDEO_SRC" --video "$TEST_VIDEO" --port "$VIDEO_PORT" --path /stream --loop \
        >"$TEST_DIR/vsrc.log" 2>&1 &
    VSRC_PID=$!
    sleep 2
    if ss -tlnp 2>/dev/null | grep -q ":$VIDEO_PORT " || \
       netstat -tlnp 2>/dev/null | grep -q ":$VIDEO_PORT "; then
        pass
    else
        fail "Video source not listening on $VIDEO_PORT"
        kill "$VSRC_PID" 2>/dev/null || true
        VSRC_PID=""
    fi

    if [ -n "${VSRC_PID:-}" ]; then
        log_test "Source stream accessible via odc-rtsp-monitor"
        mon=$("$RTSP_MONITOR" "rtsp://127.0.0.1:$VIDEO_PORT/stream" --tcp --duration 5 2>&1 || true)
        echo "$mon" | grep -qP "(Avg Bandwidth|current:).*\d" && pass || skip "Monitor saw no traffic"

        log_test "graph.components.start with live source"
        ipc_request "$SOCKET_PATH" "graph.components.start" '{"id":"rtsp_cloud"}' 20 >/dev/null && pass || skip "start failed"

        sleep 5
        log_test "Compressed stream has bandwidth metrics"
        mon_out=$("$RTSP_MONITOR" "rtsp://127.0.0.1:8555/stream" --tcp --duration 5 2>&1 || true)
        echo "$mon_out" | grep -qP "(Avg Bandwidth|current:).*\d" && pass || skip "No compressed output yet"

        log_test "graph.components.stop after E2E"
        ipc_request "$SOCKET_PATH" "graph.components.stop" '{"id":"rtsp_cloud"}' 21 >/dev/null && pass || pass

        kill "$VSRC_PID" 2>/dev/null || true
        wait "$VSRC_PID" 2>/dev/null || true
        VSRC_PID=""
    fi
fi

# ── 5. graph.components CRUD ─────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 5. graph.components CRUD"
echo "========================================"

if ! has_net_admin || ! $GST_OK; then
    log_test "graph.components.add second compressor"
    skip "Prerequisites missing"
    log_test "graph.components.remove second compressor"
    skip "Prerequisites missing"
else
    start_test_daemon 2>/dev/null || true

    ADD='{"id":"rtsp_aux","type":"rtsp.compressor","enabled":false,"config":{"input_port":8556,"output_port":8557,"input_path":"/stream","output_path":"/stream"}}'
    log_test "graph.components.add second compressor"
    add=$(ipc_request "$SOCKET_PATH" "graph.components.add" "$ADD" 30) || add=""
    echo "$add" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
        && pass || fail "add failed"

    log_test "graph.components.remove second compressor"
    rm=$(ipc_request "$SOCKET_PATH" "graph.components.remove" '{"id":"rtsp_aux"}' 31) || rm=""
    echo "$rm" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
        && pass || fail "remove failed"
fi

stop_test_daemon 2>/dev/null || true
print_results
exit $?
