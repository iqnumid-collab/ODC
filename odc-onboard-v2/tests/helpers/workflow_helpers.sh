#!/usr/bin/env bash
#
# Workflow-specific test helpers for odc-onboard-v2 full workflow test suite
#
set -uo pipefail

# ── Wait for a component to reach a specific state ───────────────────────────
wait_graph_component_state() {
    local socket="$1" comp_id="$2" expected_state="$3" timeout_sec="${4:-30}"
    local elapsed=0
    while [ "$elapsed" -lt "$timeout_sec" ]; do
        local status
        status=$(graph_status_json "$socket")
        if echo "$status" | python3 -c "
import sys, json
d = json.load(sys.stdin)
for c in d.get('components', []):
    if c.get('id') != '$comp_id':
        continue
    st = c.get('state', '')
    running = c.get('running', False)
    if st == '$expected_state':
        sys.exit(0)
    if '$expected_state' == 'running' and running:
        sys.exit(0)
    if '$expected_state' == 'connected' and st == 'running':
        # VPN uses 'running' state when tunnel is up
        vs = c.get('vpn_state', c.get('binding', {}).get('vpn_state', ''))
        if vs == 'connected':
            sys.exit(0)
sys.exit(1)
" 2>/dev/null; then
            return 0
        fi
        sleep 2
        elapsed=$((elapsed + 2))
    done
    return 1
}

# ── Wait for a link to reach active/inactive ──────────────────────────────────
wait_graph_link_state() {
    local socket="$1" link_id="$2" expected_active="$3" timeout_sec="${4:-30}"
    local elapsed=0
    while [ "$elapsed" -lt "$timeout_sec" ]; do
        local status
        status=$(graph_status_json "$socket")
        if echo "$status" | python3 -c "
import sys, json
d = json.load(sys.stdin)
for l in d.get('links', []):
    if l.get('id') != '$link_id':
        continue
    active = l.get('active', False)
    expected = '$expected_active'.lower() in ('true','1','yes')
    sys.exit(0 if active == expected else 1)
sys.exit(1)
" 2>/dev/null; then
            return 0
        fi
        sleep 2
        elapsed=$((elapsed + 2))
    done
    return 1
}

# ── Provision a complete VPN + Extender + Compressor + links + triggers bundle ─
provision_vpn_bundle() {
    local vpn_id="$1" vpn_type="$2" vpn_config_path="$3" ext_prefix="$4"
    local socket="${5:-${SOCKET_PATH}}"
    local ext_id="${ext_prefix:-${vpn_id}}_ext"
    local rtsp_id="${ext_prefix:-${vpn_id}}_rtsp"
    local tunnel_link="${vpn_id}_tunnel_link"
    local rtsp_link="${vpn_id}_rtsp_link"
    local router_link="${ext_id}_router_link"
    local connect_trigger="${vpn_id}_on_connect"
    local disconnect_trigger="${vpn_id}_on_disconnect"

    # Add VPN component (disabled initially)
    graph_ipc_add_component "$vpn_id" "$vpn_type" \
        "{\"config_path\":\"$vpn_config_path\"}" "false" 90 >/dev/null 2>&1 || return 1

    # Add extender
    graph_ipc_add_component "$ext_id" "mavlink.extender" \
        '{"udp_port":14580,"auto":true}' "false" 91 >/dev/null 2>&1 || return 1

    # Add RTSP compressor
    graph_ipc_add_component "$rtsp_id" "rtsp.compressor" \
        '{"output_codec":"h265","output_bitrate_kbps":800,"output_width":1280,"output_height":720,"output_framerate":30,"input_port":8554,"input_path":"/stream","output_port":8555,"output_path":"/stream","input_use_tcp":true}' \
        "false" 92 >/dev/null 2>&1 || return 1

    # Add tunnel link
    ipc_request "$socket" "graph.links.add" \
        "{\"id\":\"$tunnel_link\",\"from\":{\"component\":\"$vpn_id\",\"port\":\"tunnel\"},\"to\":{\"component\":\"$ext_id\",\"port\":\"transport\"},\"bind\":{\"fields\":[\"connection_id\",\"interface_name\",\"target_ip\"]}}" 93 >/dev/null 2>&1 || return 1

    # Add RTSP link
    ipc_request "$socket" "graph.links.add" \
        "{\"id\":\"$rtsp_link\",\"from\":{\"component\":\"$vpn_id\",\"port\":\"tunnel\"},\"to\":{\"component\":\"$rtsp_id\",\"port\":\"transport\"},\"bind\":{\"fields\":[\"connection_id\",\"interface_name\",\"target_ip\"]}}" 94 >/dev/null 2>&1 || return 1

    # Add router-to-extender link
    ipc_request "$socket" "graph.links.add" \
        "{\"id\":\"$router_link\",\"from\":{\"component\":\"mav_primary\",\"port\":\"router\"},\"to\":{\"component\":\"$ext_id\",\"port\":\"upstream\"},\"bind\":{\"fields\":[\"router_ref\"]}}" 95 >/dev/null 2>&1 || return 1

    # Add connect trigger
    ipc_request "$socket" "graph.triggers.set" \
        "{\"id\":\"$connect_trigger\",\"on\":\"vpn.connected\",\"source\":\"$vpn_id\",\"activate\":[\"$tunnel_link\",\"$rtsp_link\",\"$router_link\"]}" 96 >/dev/null 2>&1 || return 1

    # Add disconnect trigger
    ipc_request "$socket" "graph.triggers.set" \
        "{\"id\":\"$disconnect_trigger\",\"on\":\"vpn.disconnected\",\"source\":\"$vpn_id\",\"deactivate\":[\"$tunnel_link\",\"$rtsp_link\",\"$router_link\"]}" 97 >/dev/null 2>&1 || return 1

    return 0
}

# ── Verify a TransportBinding was propagated to a target component ────────────
verify_binding_propagation() {
    local socket="$1" source_id="$2" target_id="$3" timeout_sec="${4:-20}"
    local elapsed=0
    while [ "$elapsed" -lt "$timeout_sec" ]; do
        local status
        status=$(graph_status_json "$socket")
        if echo "$status" | python3 -c "
import sys, json
d = json.load(sys.stdin)
found_target = False
src_binding = None
for c in d.get('components', []):
    if c.get('id') == '$source_id':
        src_binding = c.get('binding', {})
    if c.get('id') == '$target_id':
        found_target = True
        target_ip = c.get('target_ip', c.get('binding', {}).get('target_ip', ''))
        iface = c.get('interface_name', c.get('binding', {}).get('interface_name', ''))
        if target_ip and iface:
            sys.exit(0)
sys.exit(1)
" 2>/dev/null; then
            return 0
        fi
        sleep 2
        elapsed=$((elapsed + 2))
    done
    return 1
}

# ── Generate a fresh test YAML config ─────────────────────────────────────────
write_full_graph_config() {
    local socket_path="$1"
    local config_path="$2"
    local wg_path="${3:-}"
    local ovpn_path="${4:-}"

    cat > "$config_path" <<YAML
version: 2
general:
  log_level: info
  socket: $socket_path
components:
  - id: cellular0
    type: cellular.qmi
    enabled: false
    config:
      device: /dev/cdc-wdm0
      iface: wwan0
      auto_connect: true
      apn: internet
  - id: vpn_cloud
    type: vpn.wireguard
    enabled: false
    config:
      config_path: @WG_PATH@
  - id: mav_primary
    type: mavlink.router
    enabled: false
    config:
      tcp_server_port: 5760
      dedup_period_sec: 5
      endpoints:
        - type: uart
          name: flight_controller
          device: /dev/ttyACM0
          baud: [1500000, 115200]
        - type: udp
          name: gcs
          address: 0.0.0.0
          port: 14550
          mode: server
  - id: mav_ext_cloud
    type: mavlink.extender
    enabled: false
    config:
      udp_port: 14580
      auto: true
  - id: rtsp_cloud
    type: rtsp.compressor
    enabled: false
    config:
      output_codec: h265
      output_bitrate_kbps: 800
      output_width: 1280
      output_height: 720
      output_framerate: 30
      input_port: 8554
      input_path: /stream
      output_port: 8555
      output_path: /stream
      input_use_tcp: true
links:
  - id: cloud_mavlink
    from:
      component: vpn_cloud
      port: tunnel
    to:
      component: mav_ext_cloud
      port: transport
    bind:
      fields: [connection_id, interface_name, target_ip]
  - id: cloud_rtsp
    from:
      component: vpn_cloud
      port: tunnel
    to:
      component: rtsp_cloud
      port: transport
    bind:
      fields: [connection_id, interface_name, target_ip]
  - id: router_to_ext_cloud
    from:
      component: mav_primary
      port: router
    to:
      component: mav_ext_cloud
      port: upstream
    bind:
      fields: [router_ref]
triggers:
  - id: cloud_services_on_connect
    on: vpn.connected
    source: vpn_cloud
    activate: [cloud_mavlink, cloud_rtsp]
  - id: cloud_services_on_disconnect
    on: vpn.disconnected
    source: vpn_cloud
    deactivate: [cloud_mavlink, cloud_rtsp]
YAML

    # Replace placeholder with actual VPN config path or keep empty
    if [ -n "$wg_path" ] && [ -f "$wg_path" ]; then
        sed -i "s|@WG_PATH@|$wg_path|g" "$config_path"
    else
        sed -i "s|@WG_PATH@||g" "$config_path"
    fi
}

# ── Extract a specific field from a component's status in graph JSON ──────────
graph_component_field() {
    local status_json="$1" comp_id="$2" field="$3"
    echo "$status_json" | python3 -c "
import sys, json
d = json.load(sys.stdin)
for c in d.get('components', []):
    if c.get('id') != '$comp_id':
        continue
    val = c
    for part in '$field'.split('.'):
        if isinstance(val, dict):
            val = val.get(part, '')
        else:
            val = ''
    print(val)
    sys.exit(0)
print('')
" 2>/dev/null || echo ""
}

# ── Check if the binary was built with GStreamer support ─────────────────────
has_rtsp_support() {
    ldd "$BINARY" 2>/dev/null | grep -q gstreamer
}

# ── Check if an RTSP URI is serving data ─────────────────────────────────────
rtsp_stream_alive() {
    local uri="$1"
    local duration="${2:-3}"
    local out
    out=$(timeout "$duration" "$RTSP_MONITOR_BIN" "$uri" --tcp --duration "$duration" 2>&1) || out=""
    echo "$out" | grep -qP "(Avg Bandwidth|current):\s*[1-9]"
}

# ── Stop daemon cleanly (SIGTERM first, then kill) ───────────────────────────
stop_test_daemon() {
    if [ -n "${DAEMON_PID:-}" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
        kill "$DAEMON_PID" 2>/dev/null || true
        local i
        for i in $(seq 1 10); do
            if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
                break
            fi
            sleep 0.5
        done
        kill -9 "$DAEMON_PID" 2>/dev/null || true
        wait "$DAEMON_PID" 2>/dev/null || true
        DAEMON_PID=""
    fi
    rm -f "${SOCKET_PATH:-}" 2>/dev/null || true
}

# ── Start daemon and wait for socket ─────────────────────────────────────────
start_test_daemon() {
    local config_file="$1"
    stop_test_daemon
    "$BINARY" --config "$config_file" --daemon --socket "$SOCKET_PATH" \
        >"$LOG_DIR/daemon.log" 2>&1 &
    DAEMON_PID=$!
    wait_for_socket "$SOCKET_PATH" 15
}

# ── Run a raw IPC request and check for success ──────────────────────────────
ipc_ok() {
    local method="$1" params="$2" req_id="${3:-20}"
    local result
    result=$(ipc_request "$SOCKET_PATH" "$method" "$params" "$req_id" 2>/dev/null) || result=""
    echo "$result" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" 2>/dev/null
}

# ── Get component count from graph status ────────────────────────────────────
graph_component_count() {
    local status_json="$1"
    echo "$status_json" | python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d.get('components',[])))" 2>/dev/null || echo "0"
}

# ── Get link count from graph status ─────────────────────────────────────────
graph_link_count() {
    local status_json="$1"
    echo "$status_json" | python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d.get('links',[])))" 2>/dev/null || echo "0"
}

# ── IPC request with custom timeout (for long-running operations like VPN start) ──
ipc_request_timeout() {
    local socket_path="$1"
    local method="$2"
    local params='{}'
    local req_id="1"
    local timeout_sec="60"
    if [ "$#" -ge 3 ]; then
        params="$3"
    fi
    if [ "$#" -ge 4 ]; then
        req_id="$4"
    fi
    if [ "$#" -ge 5 ]; then
        timeout_sec="$5"
    fi

    python3 - "$socket_path" "$method" "$params" "$req_id" "$timeout_sec" <<'PY'
import json
import socket
import sys

sock_path, method, params_json, req_id, tout = sys.argv[1:6]
params = json.loads(params_json) if params_json else {}

request = {
    "jsonrpc": "2.0",
    "method": method,
    "params": params,
    "id": int(req_id),
}
payload = (json.dumps(request, separators=(",", ":")) + "\n").encode("utf-8")

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(float(tout))
sock.connect(sock_path)
sock.sendall(payload)

buf = b""
while b"\n" not in buf:
    chunk = sock.recv(4096)
    if not chunk:
        break
    buf += chunk

line = buf.split(b"\n", 1)[0].decode("utf-8", errors="replace")
if not line:
    print(json.dumps({"error": "empty IPC response"}))
    sys.exit(1)

response = json.loads(line)
if response.get("error"):
    print(json.dumps(response))
    sys.exit(1)

result = response.get("result")
print(json.dumps(result if result is not None else {}))
PY
}

# ── Start/stop VPN via graph CLI (bypasses IPC timeout for long VPN connects) ──
graph_cli_start_vpn() {
    local vpn_id="$1"
    local timeout_sec="${2:-90}"
    local cli_cmd="graph components start $vpn_id"
    local output
    output=$(run_graph_cli "$cli_cmd" "$timeout_sec" 2>/dev/null) || output=""
    echo "$output"
}

graph_cli_stop_vpn() {
    local vpn_id="$1"
    local timeout_sec="${2:-30}"
    local cli_cmd="graph components stop $vpn_id"
    local output
    output=$(run_graph_cli "$cli_cmd" "$timeout_sec" 2>/dev/null) || output=""
    echo "$output"
}

# ── Check if VPN config targets the hexa-five lab ────────────────────────────
vpn_config_targets_lab() {
    local config_file="$1"
    grep -qE "192\.168\.10\.20[12]" "$config_file" 2>/dev/null
}
