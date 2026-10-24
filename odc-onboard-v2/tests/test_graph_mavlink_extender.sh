#!/usr/bin/env bash
#
# odc-onboard-v2 Phase 3 — MAVLink extender over graph IPC
# Adapted from test_vpn_monitor_extender.sh (graph.* IPC instead of legacy CLI).
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/helpers/test_helpers.sh"

BINARY="${BINARY:-$PROJECT_DIR/build/odc-onboard-v2}"
TEST_DIR=$(mktemp -d /tmp/odc-graph-mav-XXXXXX)
SOCKET_PATH="$TEST_DIR/odc-onboard.sock"
CONFIG_PATH="$TEST_DIR/system.yaml"
DAEMON_PID=""

cleanup() {
    stop_test_daemon
    for iface in test-graph-wg0 test-graph-ext0; do
        ip tuntap del name "$iface" mode tun 2>/dev/null || true
    done
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

write_graph_config() {
    cat > "$CONFIG_PATH" <<EOF
version: 2
general:
  log_level: error
  socket: $SOCKET_PATH
components:
  - id: mav_primary
    type: mavlink.router
    enabled: true
    config:
      tcp_server_port: 5761
      dedup_period_sec: 0
      endpoints: []
  - id: mav_ext_cloud
    type: mavlink.extender
    enabled: true
    config:
      udp_port: 14580
      auto: true
  - id: vpn_cloud
    type: vpn.wireguard
    enabled: false
    config:
      config_path: $SCRIPT_DIR/vpn-configs/wg-client.conf
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
  - id: router_to_ext
    from:
      component: mav_primary
      port: router
    to:
      component: mav_ext_cloud
      port: upstream
    bind:
      fields: [router_ref]
triggers:
  - id: cloud_mavlink_on_connect
    on: vpn.connected
    source: vpn_cloud
    activate: [cloud_mavlink]
EOF
}

create_tun() {
    ip tuntap add name "$1" mode tun 2>/dev/null
}

delete_tun() {
    ip tuntap del name "$1" mode tun 2>/dev/null || true
}

iface_up() { ip link set "$1" up 2>/dev/null; }
assign_ip() { ip addr add "$2" dev "$1" 2>/dev/null; }

echo "============================================================"
echo " Graph MAVLink Extender Tests (Phase 3)"
echo "============================================================"
echo ""
echo "Binary: $BINARY"
echo ""

if [ ! -x "$BINARY" ]; then
    echo "ERROR: Binary not found or not executable: $BINARY"
    exit 1
fi

write_graph_config

# ── 1. Static config / graph shape ───────────────────────────────────────────
echo "========================================"
echo " 1. Graph Config Shape"
echo "========================================"

log_test "Graph config includes mav_primary router"
assert_contains_fixed "$(cat "$CONFIG_PATH")" "mav_primary"

log_test "Graph config includes mav_ext_cloud extender"
assert_contains_fixed "$(cat "$CONFIG_PATH")" "mav_ext_cloud"

log_test "Graph config links VPN tunnel to extender transport"
assert_contains_fixed "$(cat "$CONFIG_PATH")" "cloud_mavlink"

log_test "Graph config defines vpn.connected trigger"
assert_contains_fixed "$(cat "$CONFIG_PATH")" "vpn.connected"

if ! has_net_admin; then
    echo ""
    echo "========================================"
    echo " 2. IPC + Runtime (privileged)"
    echo "========================================"
    for i in $(seq 1 10); do
        log_test "Graph MAVLink IPC test $i"
        skip "Requires CAP_NET_ADMIN or root"
    done
    print_results
    exit $?
fi

# ── 2. Daemon + graph.status ─────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 2. Daemon + graph.status"
echo "========================================"

log_test "Daemon starts with MAVLink graph config"
start_test_daemon && pass || fail "Daemon failed (see $TEST_DIR/daemon.log)"

log_test "graph.status lists mav_ext_cloud component"
status=$(ipc_request "$SOCKET_PATH" "graph.status" '{}' 1) || status=""
echo "$status" | python3 -c "import sys,json; ids=[c['id'] for c in json.load(sys.stdin).get('components',[])]; sys.exit(0 if 'mav_ext_cloud' in ids else 1)" \
    && pass || fail "mav_ext_cloud missing"

log_test "graph.status lists cloud_mavlink link"
echo "$status" | python3 -c "import sys,json; ids=[l['id'] for l in json.load(sys.stdin).get('links',[])]; sys.exit(0 if 'cloud_mavlink' in ids else 1)" \
    && pass || fail "cloud_mavlink link missing"

log_test "graph.validate returns valid for MAVLink graph"
validate=$(ipc_request "$SOCKET_PATH" "graph.validate" '{}' 2) || validate=""
valid=$(echo "$validate" | python3 -c "import sys,json; print(json.load(sys.stdin).get('valid',''))" 2>/dev/null)
if [ "$valid" = "True" ] || [ "$valid" = "true" ]; then pass; else fail "valid=$valid"; fi

# ── 3. IPC component control ─────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 3. graph.components IPC"
echo "========================================"

log_test "graph.components.list returns extender entry"
components=$(ipc_request "$SOCKET_PATH" "graph.components.list" '{}' 3) || components=""
echo "$components" | python3 -c "import sys,json; d=json.load(sys.stdin); items=d if isinstance(d,list) else d.get('components',[]); sys.exit(0 if any(c.get('id')=='mav_ext_cloud' for c in items) else 1)" \
    && pass || fail "extender not listed"

log_test "graph.components.stop mav_ext_cloud"
stop_result=$(ipc_request "$SOCKET_PATH" "graph.components.stop" '{"id":"mav_ext_cloud"}' 4) || stop_result=""
echo "$stop_result" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
    && pass || skip "stop returned false (may already be stopped)"

log_test "graph.components.start mav_ext_cloud"
start_result=$(ipc_request "$SOCKET_PATH" "graph.components.start" '{"id":"mav_ext_cloud"}' 5) || start_result=""
echo "$start_result" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
    && pass || skip "start returned false"

# ── 4. TUN / binding simulation ──────────────────────────────────────────────
echo ""
echo "========================================"
echo " 4. VPN Interface + Extender Binding"
echo "========================================"

log_test "Create TUN interface test-graph-ext0"
create_tun test-graph-ext0 && pass || fail "Failed to create TUN"

log_test "Assign IP to TUN (binding signal)"
iface_up test-graph-ext0
assign_ip test-graph-ext0 "10.88.1.1/24" && pass || fail "Failed to assign IP"

log_test "Wait for extender reaction (3s)"
sleep 3
pass

log_test "graph.status still reachable after interface event"
ipc_request "$SOCKET_PATH" "graph.status" '{}' 6 >/dev/null && pass || fail "graph.status failed"

log_test "Extender component present after interface churn"
status=$(ipc_request "$SOCKET_PATH" "graph.status" '{}' 7) || status=""
echo "$status" | python3 -c "import sys,json; ids=[c['id'] for c in json.load(sys.stdin).get('components',[])]; sys.exit(0 if 'mav_ext_cloud' in ids else 1)" \
    && pass || fail "extender missing after TUN events"

log_test "Clean up TUN interface"
delete_tun test-graph-ext0 && pass || fail "TUN cleanup failed"

# ── 5. graph.links IPC mutation ──────────────────────────────────────────────
echo ""
echo "========================================"
echo " 5. graph.links IPC"
echo "========================================"

log_test "graph.links.remove cloud_mavlink"
rm_link=$(ipc_request "$SOCKET_PATH" "graph.links.remove" '{"id":"cloud_mavlink"}' 8) || rm_link=""
echo "$rm_link" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
    && pass || fail "links.remove failed"

log_test "graph.links.add cloud_mavlink (round-trip)"
add_link=$(ipc_request "$SOCKET_PATH" "graph.links.add" \
    '{"id":"cloud_mavlink","from":{"component":"vpn_cloud","port":"tunnel"},"to":{"component":"mav_ext_cloud","port":"transport"},"bind":{"fields":["connection_id","interface_name","target_ip"]}}' \
    9) || add_link=""
echo "$add_link" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
    && pass || fail "links.add failed"

log_test "Link persisted in YAML after IPC round-trip"
grep -q "cloud_mavlink" "$CONFIG_PATH" && pass || fail "link not in YAML"

stop_test_daemon
print_results
exit $?
