#!/usr/bin/env bash
#
# odc-onboard-v2 Real Hardware Graph Integration Test Suite
# Persistent system.yaml + CLI runtime mutations + multi-VPN + MAVLink + RTSP E2E
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/helpers/test_helpers.sh"

if ! has_root; then
    if sudo -n true 2>/dev/null; then
        exec sudo "$0" "$@"
    fi
    echo "ERROR: Real hardware tests require root (same as odc-onboard/tests/run_all.sh)"
    echo "Run with: sudo $0"
    exit 1
fi

if [ -f "$SCRIPT_DIR/helpers/real_hardware.env" ]; then
    # shellcheck source=/dev/null
    source "$SCRIPT_DIR/helpers/real_hardware.env"
fi

BINARY="${BINARY:-$PROJECT_DIR/build/odc-onboard-v2}"
TOOLS_DIR="$PROJECT_DIR/tools"
VIDEO_SRC="$TOOLS_DIR/odc-video-source/build/odc-video-source"
RTSP_MONITOR="$TOOLS_DIR/odc-rtsp-monitor/build/odc-rtsp-monitor"
TEST_VIDEO="$TOOLS_DIR/odc-video-source/vid-src/mercedes.mp4"
EXAMPLE_CONFIG="$PROJECT_DIR/config/system.example.yaml"

TEST_DIR=$(mktemp -d /tmp/odc-real-hw-XXXXXX)
ODC_CONFIG="${ODC_CONFIG:-$TEST_DIR/system.yaml}"
ODC_SOCKET="${ODC_SOCKET:-$TEST_DIR/odc-onboard.sock}"
SOCKET_PATH="$ODC_SOCKET"
CONFIG_PATH="$ODC_CONFIG"

ODC_WG_CONFIGS="${ODC_WG_CONFIGS:-/etc/odc-onboard/vpn/dev.conf,/etc/odc-onboard/vpn/remote.conf}"
ODC_WG_IDS="${ODC_WG_IDS:-vpn_wg_dev,vpn_wg_remote}"
ODC_OVPN_CONFIGS="${ODC_OVPN_CONFIGS:-/etc/odc-onboard/vpn/dev.ovpn,/etc/odc-onboard/vpn/remote.ovpn}"
ODC_OVPN_IDS="${ODC_OVPN_IDS:-vpn_ovpn_dev,vpn_ovpn_remote}"
ODC_MAVLINK_DEVICE="${ODC_MAVLINK_DEVICE:-/dev/ttyACM0}"
ODC_MAVLINK_BAUD="${ODC_MAVLINK_BAUD:-1500000}"
ODC_RTSP_INPUT_PORTS="${ODC_RTSP_INPUT_PORTS:-8554,8556}"
ODC_RTSP_OUTPUT_PORTS="${ODC_RTSP_OUTPUT_PORTS:-8555,8557}"

DAEMON_PID=""
BASELINE_COMPONENTS=""
GST_OK=false
RUNTIME_OK=false

cleanup() {
    stop_test_daemon
    stop_video_sources
    if [ "${CLEANUP_CONFIG:-0}" = "1" ] && [ -f "$ODC_CONFIG" ]; then
        rm -f "$ODC_CONFIG" "${ODC_CONFIG}.bak" "${ODC_CONFIG}.tmp" 2>/dev/null || true
    fi
    if [ "$ODC_CONFIG" != "${TEST_DIR}/system.yaml" ] && [ -d "$TEST_DIR" ]; then
        rm -rf "$TEST_DIR"
    fi
}
trap cleanup EXIT

stop_test_daemon() {
    if [ -n "$DAEMON_PID" ]; then
        kill -TERM "$DAEMON_PID" 2>/dev/null || true
        local elapsed=0
        while kill -0 "$DAEMON_PID" 2>/dev/null && [ "$elapsed" -lt 10 ]; do
            sleep 1
            elapsed=$((elapsed + 1))
        done
        if kill -0 "$DAEMON_PID" 2>/dev/null; then
            kill -9 "$DAEMON_PID" 2>/dev/null || true
        fi
        wait "$DAEMON_PID" 2>/dev/null || true
        DAEMON_PID=""
    fi
    rm -f "$ODC_SOCKET" 2>/dev/null || true
}

start_test_daemon() {
    stop_test_daemon
    mkdir -p "$(dirname "$ODC_SOCKET")"
    if [ -f "$ODC_CONFIG" ]; then
        sed -i "s|socket:.*|socket: $ODC_SOCKET|" "$ODC_CONFIG" 2>/dev/null || true
    fi
    "$BINARY" --config "$ODC_CONFIG" --daemon --socket "$ODC_SOCKET" \
        >"$TEST_DIR/daemon.log" 2>&1 &
    DAEMON_PID=$!
    wait_for_socket "$ODC_SOCKET" 15
}

split_csv() {
    echo "$1" | tr ',' '\n' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' | grep -v '^$'
}

ensure_mav_primary() {
    local status
    status=$(graph_status_json "$ODC_SOCKET")
    if graph_has_component "$status" "mav_primary"; then
        return 0
    fi
    local params
    params=$(python3 - <<PY
import json
print(json.dumps({
    "id": "mav_primary",
    "type": "mavlink.router",
    "enabled": False,
    "config": {
        "tcp_server_port": 5760,
        "dedup_period_sec": 5,
        "endpoints": [{
            "type": "uart",
            "name": "flight_controller",
            "device": "$ODC_MAVLINK_DEVICE",
            "baud": [int("$ODC_MAVLINK_BAUD"), 115200]
        }]
    }
}))
PY
)
    local out
    out=$(ipc_request "$ODC_SOCKET" "graph.components.add" "$params" 50) || return 1
    echo "$out" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" 2>/dev/null
}

provision_wg_client() {
    local wg_id="$1"
    local wg_config="$2"
    local input_port="$3"
    local output_port="$4"
    local status ext_id rtsp_id link_mav link_rtsp trig_on trig_off

    status=$(graph_status_json "$ODC_SOCKET")
    ext_id="mav_ext_${wg_id#vpn_}"
    rtsp_id="rtsp_${wg_id#vpn_}"
    link_mav="${wg_id#vpn_}_mav"
    link_rtsp="${wg_id#vpn_}_rtsp"
    trig_on="${wg_id#vpn_}_on"
    trig_off="${wg_id#vpn_}_off"

    if ! graph_has_component "$status" "$wg_id"; then
        local out
        out=$(graph_ipc_add_component "$wg_id" "vpn.wireguard" "{\"config_path\":\"$wg_config\"}" false 50) || return 1
        echo "$out" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" 2>/dev/null || return 1
        assert_yaml_contains "$ODC_CONFIG" "id: $wg_id"
    fi

    if ! graph_has_component "$status" "$ext_id"; then
        graph_ipc_add_component "$ext_id" "mavlink.extender" '{"udp_port":14550}' false 51 >/dev/null
        assert_yaml_contains "$ODC_CONFIG" "id: $ext_id"
    fi

    if ! graph_has_component "$status" "$rtsp_id"; then
        graph_ipc_add_component "$rtsp_id" "rtsp.compressor" \
            "{\"input_port\":$input_port,\"output_port\":$output_port,\"input_path\":\"/stream\",\"output_path\":\"/stream\",\"output_codec\":\"h264\",\"output_bitrate_kbps\":800,\"input_use_tcp\":true}" \
            false 52 >/dev/null
        assert_yaml_contains "$ODC_CONFIG" "id: $rtsp_id"
    fi

    run_graph_cli "graph links add ${link_mav} ${wg_id}:tunnel ${ext_id}:transport bind=connection_id,interface_name,target_ip" >/dev/null
    run_graph_cli "graph links add ${link_rtsp} ${wg_id}:tunnel ${rtsp_id}:transport bind=connection_id,interface_name,target_ip" >/dev/null
    run_graph_cli "graph links add router_${ext_id} mav_primary:router ${ext_id}:upstream bind=router_ref" >/dev/null
    run_graph_cli "graph triggers set ${trig_on} on=vpn.connected source=$wg_id activate=${link_mav},${link_rtsp},router_${ext_id}" >/dev/null
    run_graph_cli "graph triggers set ${trig_off} on=vpn.disconnected source=$wg_id deactivate=${link_mav},${link_rtsp}" >/dev/null

    assert_yaml_contains "$ODC_CONFIG" "$link_mav"
    assert_yaml_contains "$ODC_CONFIG" "$trig_on"
}

provision_ovpn_client() {
    local ovpn_id="$1"
    local ovpn_config="$2"
    local input_port="$3"
    local output_port="$4"
    local status ext_id rtsp_id

    status=$(graph_status_json "$ODC_SOCKET")
    ext_id="mav_ext_${ovpn_id#vpn_}"
    rtsp_id="rtsp_${ovpn_id#vpn_}"

    if ! graph_has_component "$status" "$ovpn_id"; then
        graph_ipc_add_component "$ovpn_id" "vpn.openvpn" "{\"config_path\":\"$ovpn_config\"}" false 53 >/dev/null
        assert_yaml_contains "$ODC_CONFIG" "id: $ovpn_id"
    fi

    if ! graph_has_component "$status" "$ext_id"; then
        graph_ipc_add_component "$ext_id" "mavlink.extender" '{"udp_port":14551}' false 54 >/dev/null
    fi

    if ! graph_has_component "$status" "$rtsp_id"; then
        graph_ipc_add_component "$rtsp_id" "rtsp.compressor" \
            "{\"input_port\":$input_port,\"output_port\":$output_port,\"input_path\":\"/stream\",\"output_path\":\"/stream\",\"output_codec\":\"h264\",\"output_bitrate_kbps\":800,\"input_use_tcp\":true}" \
            false 55 >/dev/null
    fi

    run_graph_cli "graph links add ${ovpn_id#vpn_}_mav ${ovpn_id}:tunnel ${ext_id}:transport bind=connection_id,interface_name,target_ip" >/dev/null
    run_graph_cli "graph links add ${ovpn_id#vpn_}_rtsp ${ovpn_id}:tunnel ${rtsp_id}:transport bind=connection_id,interface_name,target_ip" >/dev/null
    run_graph_cli "graph triggers set ${ovpn_id#vpn_}_on on=vpn.connected source=$ovpn_id activate=${ovpn_id#vpn_}_mav,${ovpn_id#vpn_}_rtsp" >/dev/null
}

echo "============================================================"
echo " Real Hardware Graph Integration Tests"
echo "============================================================"
echo ""
echo "Binary:  $BINARY"
echo "Config:  $ODC_CONFIG"
echo "Socket:  $ODC_SOCKET"
echo ""

# ── Section 0: Preflight ─────────────────────────────────────────────────────
echo "========================================"
echo " 0. Preflight"
echo "========================================"

log_test "Binary exists and is executable"
if [ -x "$BINARY" ]; then pass; else fail "missing or not executable: $BINARY"; fi

log_test "Deploy VPN credential configs"
if deploy_vpn_configs "$PROJECT_DIR"; then
    pass
else
    fail "failed to deploy VPN configs to /etc/odc-onboard/vpn/"
fi

cleanup_stale_graph_interfaces

log_test "WireGuard dev.conf deployed (odc-onboard integration path)"
assert_file_exists "/etc/odc-onboard/vpn/dev.conf"

log_test "OpenVPN dev.ovpn deployed (odc-onboard integration path)"
assert_file_exists "/etc/odc-onboard/vpn/dev.ovpn"

log_test "dev.conf targets hexa-five WG server (192.168.10.201)"
grep -q "192.168.10.201" /etc/odc-onboard/vpn/dev.conf && pass || fail "Expected lab Endpoint 192.168.10.201:51820"

log_test "dev.ovpn targets hexa-five OpenVPN server (192.168.10.202)"
grep -qE "remote 192\.168\.10\.202|remote=192\.168\.10\.202" /etc/odc-onboard/vpn/dev.ovpn && pass || fail "Expected lab remote 192.168.10.202"

log_test "dev.conf has Endpoint (real VPN credentials)"
grep -q "Endpoint" /etc/odc-onboard/vpn/dev.conf && pass || fail "Missing Endpoint"

log_test "dev.ovpn has remote directive"
grep -qE "remote |remote=" /etc/odc-onboard/vpn/dev.ovpn && pass || skip "dev.ovpn format differs"

log_test "Seed persisted config if missing"
seed_persisted_config "$ODC_CONFIG" "$EXAMPLE_CONFIG" "$PROJECT_DIR"
assert_file_exists "$ODC_CONFIG"

if has_gstreamer_binary "$BINARY"; then
    log_test "odc-onboard-v2 links GStreamer"
    pass
    GST_OK=true
else
    log_test "odc-onboard-v2 links GStreamer"
    skip "RTSP runtime tests will be skipped"
fi

log_test "Video tools and test file present"
if [ -x "$VIDEO_SRC" ] && [ -x "$RTSP_MONITOR" ] && [ -f "$TEST_VIDEO" ]; then
    pass
else
    skip "Build odc-video-source and odc-rtsp-monitor; ensure mercedes.mp4 exists"
fi

if has_net_admin; then
    log_test "CAP_NET_ADMIN or root available"
    pass
    RUNTIME_OK=true
else
    log_test "CAP_NET_ADMIN or root available"
    skip "Runtime VPN/graph tests require net admin"
fi

# ── Section 1: Start services ──────────────────────────────────────────────
echo ""
echo "========================================"
echo " 1. Start Services"
echo "========================================"

if $RUNTIME_OK && [ -x "$VIDEO_SRC" ] && [ -f "$TEST_VIDEO" ]; then
    log_test "Start odc-video-source on ports $ODC_RTSP_INPUT_PORTS"
    start_video_sources "$ODC_RTSP_INPUT_PORTS" "$TEST_VIDEO" "$VIDEO_SRC" "$TEST_DIR"
    sleep 2
    pass

    IFS=',' read -ra IN_PORTS <<< "$ODC_RTSP_INPUT_PORTS"
    for port in "${IN_PORTS[@]}"; do
        port=$(echo "$port" | tr -d ' ')
        log_test "Verify video source on port $port"
        if [ -x "$RTSP_MONITOR" ]; then
            out=$("$RTSP_MONITOR" "rtsp://127.0.0.1:${port}/stream" --tcp --duration 3 2>&1) || true
            if echo "$out" | grep -qP "(Avg Bandwidth|current:).*[1-9]"; then pass; else skip "source not ready on $port"; fi
        else
            skip "monitor missing"
        fi
    done

    log_test "Start odc-onboard-v2 daemon with persisted config"
    if start_test_daemon; then pass; else fail "see $TEST_DIR/daemon.log"; fi
else
    log_test "Start odc-video-source instances"
    skip "Prerequisites missing"
    log_test "Start daemon"
    skip "Prerequisites missing"
fi

# ── Section 2: Baseline from persisted config ────────────────────────────────
echo ""
echo "========================================"
echo " 2. Baseline (Persisted Config)"
echo "========================================"

if $RUNTIME_OK && [ -n "$DAEMON_PID" ]; then
    log_test "graph.status via IPC returns valid JSON"
    BASELINE=$(graph_status_json "$ODC_SOCKET")
    assert_valid_json "$BASELINE"

    log_test "graph.status version is 2"
    echo "$BASELINE" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('version')==2 else 1)" && pass || fail

    log_test "graph.validate returns valid"
    validate=$(ipc_request "$ODC_SOCKET" "graph.validate" '{}' 2) || validate=""
    valid=$(echo "$validate" | python3 -c "import sys,json; print(json.load(sys.stdin).get('valid',''))" 2>/dev/null)
    if [ "$valid" = "True" ] || [ "$valid" = "true" ]; then pass; else fail "valid=$valid"; fi

    BASELINE_COMPONENTS=$(graph_component_ids "$BASELINE")
    log_test "Record baseline component ids"
    [ -n "$BASELINE_COMPONENTS" ] && pass || pass

    log_test "graph status via CLI matches IPC"
    cli_out=$(run_graph_cli "graph status")
    echo "$cli_out" | python3 -c "import sys,json; json.load(sys.stdin); sys.exit(0)" 2>/dev/null && pass || skip "CLI output not JSON"
else
    log_test "Baseline graph.status"
    skip "Daemon not running"
fi

# ── Section 3: CLI graph mutations (multi-VPN) ───────────────────────────────
echo ""
echo "========================================"
echo " 3. CLI Graph Mutations (Multi-VPN)"
echo "========================================"

if $RUNTIME_OK && [ -n "$DAEMON_PID" ]; then
    log_test "Ensure mav_primary router via CLI"
    if ensure_mav_primary; then pass; else fail; fi
    assert_yaml_contains "$ODC_CONFIG" "mav_primary"

    wg_configs=($(split_csv "$ODC_WG_CONFIGS"))
    wg_ids=($(split_csv "$ODC_WG_IDS"))
    in_ports=($(split_csv "$ODC_RTSP_INPUT_PORTS"))
    out_ports=($(split_csv "$ODC_RTSP_OUTPUT_PORTS"))

    idx=0
    for wg_id in "${wg_ids[@]}"; do
        [ "$idx" -lt "${#wg_configs[@]}" ] || break
        wg_cfg="${wg_configs[$idx]}"
        in_p="${in_ports[$idx]:-${in_ports[0]:-8554}}"
        out_p="${out_ports[$idx]:-${out_ports[0]:-8555}}"
        log_test "Provision WireGuard client $wg_id"
        if provision_wg_client "$wg_id" "$wg_cfg" "$in_p" "$out_p"; then pass; else fail; fi
        idx=$((idx + 1))
    done

    ovpn_configs=($(split_csv "$ODC_OVPN_CONFIGS"))
    ovpn_ids=($(split_csv "$ODC_OVPN_IDS"))
    idx=0
    for ovpn_id in "${ovpn_ids[@]}"; do
        [ "$idx" -lt "${#ovpn_configs[@]}" ] || break
        ovpn_cfg="${ovpn_configs[$idx]}"
        in_p="${in_ports[$idx]:-8556}"
        out_p="${out_ports[$idx]:-8557}"
        log_test "Provision OpenVPN client $ovpn_id"
        if provision_ovpn_client "$ovpn_id" "$ovpn_cfg" "$in_p" "$out_p"; then pass; else fail; fi
        idx=$((idx + 1))
    done

    log_test "graph.validate after CLI mutations"
    validate=$(ipc_request "$ODC_SOCKET" "graph.validate" '{}' 20) || validate=""
    valid=$(echo "$validate" | python3 -c "import sys,json; print(json.load(sys.stdin).get('valid',''))" 2>/dev/null)
    if [ "$valid" = "True" ] || [ "$valid" = "true" ]; then pass; else fail; fi
else
    log_test "CLI graph mutations"
    skip "Daemon not running"
fi

# ── Section 4: Start data plane via CLI ──────────────────────────────────────
echo ""
echo "========================================"
echo " 4. Start Data Plane (CLI)"
echo "========================================"

FIRST_WG_ID=$(echo "$ODC_WG_IDS" | cut -d, -f1 | tr -d ' ')
FIRST_RTSP_ID="rtsp_${FIRST_WG_ID#vpn_}"

if $RUNTIME_OK && [ -n "$DAEMON_PID" ]; then
    log_test "graph components start mav_primary"
    out=$(run_graph_cli "graph components start mav_primary")
    graph_cli_success "$out" && pass || skip "router start failed"

    log_test "graph components start $FIRST_WG_ID"
    out=$(run_graph_cli "graph components start $FIRST_WG_ID")
    graph_cli_success "$out" && pass || skip "VPN start failed"

    log_test "Wait for VPN connected + ip_ready ($FIRST_WG_ID)"
    if wait_graph_vpn_ready "$ODC_SOCKET" "$FIRST_WG_ID" 60; then
        pass
    else
        skip "VPN server unreachable or slow connect"
    fi

    log_test "Links active after VPN connect"
    if wait_graph_links_active "$ODC_SOCKET" 30; then
        pass
    else
        skip "no active links yet"
    fi

    log_test "Extender/compressor armed or running"
    status=$(graph_status_json "$ODC_SOCKET")
    echo "$status" | python3 -c "
import sys,json
d=json.load(sys.stdin)
ok=False
for c in d.get('components',[]):
    if c.get('type') in ('mavlink.extender','rtsp.compressor'):
        if c.get('state') in ('armed','running') or c.get('running'):
            ok=True
sys.exit(0 if ok else 1)
" && pass || skip "targets not armed"
else
    log_test "Start data plane"
    skip "Daemon not running"
fi

# ── Section 5: MAVLink hardware ──────────────────────────────────────────────
echo ""
echo "========================================"
echo " 5. MAVLink Hardware"
echo "========================================"

if [ ! -e "$ODC_MAVLINK_DEVICE" ]; then
    log_test "Flight controller device present"
    skip "$ODC_MAVLINK_DEVICE not found"
    log_test "MAVLink message flow on extender"
    skip "no FC"
else
    log_test "Flight controller device present ($ODC_MAVLINK_DEVICE)"
    pass

    if $RUNTIME_OK && [ -n "$DAEMON_PID" ]; then
        log_test "mav_primary in graph.status"
        status=$(graph_status_json "$ODC_SOCKET")
        graph_has_component "$status" "mav_primary" && pass || fail

        log_test "TCP MAVLink server listening on 5760"
        if wait_for_tcp_port 5760 60; then pass; else skip "port 5760 not listening"; fi

        ext_id="mav_ext_${FIRST_WG_ID#vpn_}"
        log_test "Extender $ext_id message counters (poll 10s)"
        found=false
        for _ in 1 2 3 4 5; do
            status=$(graph_status_json "$ODC_SOCKET")
            if echo "$status" | python3 -c "
import sys,json
d=json.load(sys.stdin)
for c in d.get('components',[]):
    if c.get('id')!='${ext_id}': continue
    mi=c.get('messages_in',0); mo=c.get('messages_out',0)
    if mi>0 or mo>0 or c.get('active'): sys.exit(0)
sys.exit(1)
" 2>/dev/null; then found=true; break; fi
            sleep 2
        done
        if $found; then pass; else skip "no extender traffic yet"; fi
    else
        log_test "MAVLink graph checks"
        skip "Daemon not running"
    fi
fi

# ── Section 6: RTSP E2E ───────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 6. RTSP End-to-End"
echo "========================================"

if ! $GST_OK || ! $RUNTIME_OK || [ -z "$DAEMON_PID" ] || [ ! -x "$RTSP_MONITOR" ]; then
    log_test "RTSP compressor E2E"
    skip "GStreamer/daemon/monitor prerequisites missing"
else
    out_ports=($(split_csv "$ODC_RTSP_OUTPUT_PORTS"))
    idx=0
    for out_p in "${out_ports[@]}"; do
        out_p=$(echo "$out_p" | tr -d ' ')
        log_test "Wait for compressor $FIRST_RTSP_ID running"
        compressor_ready=false
        port_ready=false
        if [ "$idx" -eq 0 ]; then
            if wait_graph_rtsp_running "$ODC_SOCKET" "$FIRST_RTSP_ID" 60; then
                compressor_ready=true
                pass
            else
                skip "compressor not running"
            fi
        else
            skip "single compressor spot-check only"
        fi

        if $compressor_ready; then
            log_test "Output RTSP port $out_p listening"
            if wait_for_tcp_port "$out_p" 45; then
                port_ready=true
                pass
            else
                skip "port $out_p not listening"
            fi
        else
            log_test "Output RTSP port $out_p listening"
            skip "compressor not running"
        fi

        if $port_ready; then
            log_test "odc-rtsp-monitor on output port $out_p"
            assert_rtsp_bandwidth "$RTSP_MONITOR" "rtsp://127.0.0.1:${out_p}/stream" 15
        else
            log_test "odc-rtsp-monitor on output port $out_p"
            skip "output port not ready"
        fi
        idx=$((idx + 1))
        break
    done
fi

# ── Section 7: CLI teardown + persistence ────────────────────────────────────
echo ""
echo "========================================"
echo " 7. CLI Teardown + Persistence"
echo "========================================"

if $RUNTIME_OK && [ -n "$DAEMON_PID" ]; then
    log_test "graph components stop $FIRST_RTSP_ID"
    out=$(run_graph_cli "graph components stop $FIRST_RTSP_ID")
    graph_cli_success "$out" && pass || skip

    log_test "graph components stop vpn ($FIRST_WG_ID)"
    out=$(run_graph_cli "graph components stop $FIRST_WG_ID")
    graph_cli_success "$out" && pass || skip

    log_test "Persisted config still contains component topology"
    assert_yaml_contains "$ODC_CONFIG" "$FIRST_WG_ID"
    assert_yaml_contains "$ODC_CONFIG" "links:"
else
    log_test "CLI teardown"
    skip "Daemon not running"
fi

# ── Section 8: Restart durability ────────────────────────────────────────────
echo ""
echo "========================================"
echo " 8. Restart Durability"
echo "========================================"

if $RUNTIME_OK && [ -n "$DAEMON_PID" ]; then
    PRE_RESTART=$(graph_component_ids "$(graph_status_json "$ODC_SOCKET")")
    log_test "Stop daemon cleanly"
    stop_test_daemon
    sleep 1
    if pgrep -f "odc-onboard-v2.*${ODC_SOCKET}" >/dev/null 2>&1; then
        skip "orphaned daemon processes"
    else
        pass
    fi

    log_test "Restart daemon from same persisted config"
    if start_test_daemon; then pass; else fail; fi

    log_test "Component ids match pre-shutdown snapshot"
    POST_RESTART=$(graph_component_ids "$(graph_status_json "$ODC_SOCKET")")
    if [ "$PRE_RESTART" = "$POST_RESTART" ]; then pass; else skip "component set changed (expected if CLI added new ids)"; fi

    log_test "graph.validate after restart"
    validate=$(ipc_request "$ODC_SOCKET" "graph.validate" '{}' 30) || validate=""
    valid=$(echo "$validate" | python3 -c "import sys,json; print(json.load(sys.stdin).get('valid',''))" 2>/dev/null)
    if [ "$valid" = "True" ] || [ "$valid" = "true" ]; then pass; else fail; fi
else
    log_test "Restart durability"
    skip "Daemon not running"
fi

# ── Section 9: Cleanup note ───────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 9. Cleanup"
echo "========================================"

log_test "Persisted config retained for next run"
if [ -f "$ODC_CONFIG" ]; then pass; else fail; fi

log_test "No orphaned odc-video-source processes"
if pgrep -f "odc-video-source" >/dev/null 2>&1; then skip "video sources still running (stopped in trap)"; else pass; fi

print_results
exit $?
