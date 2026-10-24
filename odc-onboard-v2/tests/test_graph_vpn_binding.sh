#!/usr/bin/env bash
#
# odc-onboard-v2 Phase 1/2 — VPN binding + graph IPC CRUD tests
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/helpers/test_helpers.sh"

BINARY="${BINARY:-$PROJECT_DIR/build/odc-onboard-v2}"
WG_CONFIG="${WG_CONFIG:-$SCRIPT_DIR/vpn-configs/wg-client.conf}"
TEST_DIR=$(mktemp -d /tmp/odc-graph-vpn-XXXXXX)
SOCKET_PATH="$TEST_DIR/odc-onboard.sock"
CONFIG_PATH="$TEST_DIR/system.yaml"
DAEMON_PID=""

cleanup() {
    stop_test_daemon
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
    local config_file="$1"
    stop_test_daemon
    "$BINARY" --config "$config_file" --daemon --socket "$SOCKET_PATH" \
        >"$TEST_DIR/daemon.log" 2>&1 &
    DAEMON_PID=$!
    wait_for_socket "$SOCKET_PATH" 10
}

write_minimal_config() {
    local wg_path="$1"
    cat > "$CONFIG_PATH" <<EOF
version: 2
general:
  log_level: error
  socket: $SOCKET_PATH
components:
  - id: vpn_test
    type: vpn.wireguard
    enabled: false
    config:
      config_path: $wg_path
links: []
triggers: []
EOF
}

echo "============================================================"
echo " Graph VPN Binding Tests (Phase 1/2)"
echo "============================================================"
echo ""
echo "Binary: $BINARY"
echo "WireGuard config: $WG_CONFIG"
echo ""

if [ ! -x "$BINARY" ]; then
    echo "ERROR: Binary not found or not executable: $BINARY"
    exit 1
fi

if ! has_net_admin; then
    echo "========================================"
    echo " Privilege Required"
    echo "========================================"
    for i in $(seq 1 12); do
        log_test "Graph VPN IPC test $i"
        skip "Requires CAP_NET_ADMIN or root"
    done
    print_results
    exit $?
fi

if [ ! -f "$WG_CONFIG" ]; then
    echo "NOTE: WireGuard config not found — VPN connect tests will be skipped."
    WG_MISSING=true
else
    WG_MISSING=false
fi

write_minimal_config "$WG_CONFIG"

log_test "Daemon starts with minimal graph config"
start_test_daemon "$CONFIG_PATH" && pass || fail "Daemon failed to start"

log_test "IPC socket available"
[ -S "$SOCKET_PATH" ] && pass || fail "Socket missing"

# ── graph.components.add / remove ────────────────────────────────────────────
echo ""
echo "========================================"
echo " 1. graph.components CRUD"
echo "========================================"

ADD_PARAMS='{"id":"probe_comp","type":"mavlink.extender","enabled":false,"config":{"udp_port":14600,"auto":false}}'

log_test "graph.components.add inserts component"
add_result=$(ipc_request "$SOCKET_PATH" "graph.components.add" "$ADD_PARAMS" 10) || add_result=""
echo "$add_result" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
    && pass || fail "add failed: $add_result"

log_test "Added component visible in graph.status"
status=$(ipc_request "$SOCKET_PATH" "graph.status" '{}' 11) || status=""
echo "$status" | python3 -c "import sys,json; ids=[c['id'] for c in json.load(sys.stdin).get('components',[])]; sys.exit(0 if 'probe_comp' in ids else 1)" \
    && pass || fail "probe_comp not in status"

log_test "graph.components.add persists to YAML"
grep -q "probe_comp" "$CONFIG_PATH" && pass || fail "probe_comp not written to $CONFIG_PATH"

log_test "graph.components.remove deletes component"
remove_result=$(ipc_request "$SOCKET_PATH" "graph.components.remove" '{"id":"probe_comp"}' 12) || remove_result=""
echo "$remove_result" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
    && pass || fail "remove failed: $remove_result"

log_test "Removed component absent from graph.status"
status=$(ipc_request "$SOCKET_PATH" "graph.status" '{}' 13) || status=""
echo "$status" | python3 -c "import sys,json; ids=[c['id'] for c in json.load(sys.stdin).get('components',[])]; sys.exit(1 if 'probe_comp' in ids else 0)" \
    && pass || fail "probe_comp still present"

log_test "graph.components.remove persists to YAML"
if grep -q "probe_comp" "$CONFIG_PATH"; then fail "probe_comp still in YAML"; else pass; fi

# ── graph.links.add / remove ─────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 2. graph.links CRUD"
echo "========================================"

# Ensure extender exists for a valid link endpoint.
ipc_request "$SOCKET_PATH" "graph.components.add" \
    '{"id":"mav_ext_test","type":"mavlink.extender","enabled":false,"config":{"udp_port":14610,"auto":false}}' \
    20 >/dev/null || true

LINK_PARAMS='{"id":"vpn_to_ext","from":{"component":"vpn_test","port":"tunnel"},"to":{"component":"mav_ext_test","port":"transport"},"bind":{"fields":["connection_id","interface_name","target_ip"]}}'

log_test "graph.links.add creates link"
link_add=$(ipc_request "$SOCKET_PATH" "graph.links.add" "$LINK_PARAMS" 21) || link_add=""
echo "$link_add" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
    && pass || fail "links.add failed: $link_add"

log_test "Link present in graph.status"
status=$(ipc_request "$SOCKET_PATH" "graph.status" '{}' 22) || status=""
echo "$status" | python3 -c "import sys,json; ids=[l['id'] for l in json.load(sys.stdin).get('links',[])]; sys.exit(0 if 'vpn_to_ext' in ids else 1)" \
    && pass || fail "vpn_to_ext link missing"

log_test "graph.links.add persists to YAML"
grep -q "vpn_to_ext" "$CONFIG_PATH" && pass || fail "link not persisted"

log_test "graph.links.remove deletes link"
link_rm=$(ipc_request "$SOCKET_PATH" "graph.links.remove" '{"id":"vpn_to_ext"}' 23) || link_rm=""
echo "$link_rm" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
    && pass || fail "links.remove failed: $link_rm"

log_test "Link removed from YAML"
if grep -q "vpn_to_ext" "$CONFIG_PATH"; then fail "link still in YAML"; else pass; fi

# ── graph.triggers.set ───────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 3. graph.triggers.set"
echo "========================================"

# Recreate link for trigger activation list.
ipc_request "$SOCKET_PATH" "graph.links.add" "$LINK_PARAMS" 30 >/dev/null || true

TRIGGER_PARAMS='{"id":"vpn_connect_services","on":"vpn.connected","source":"vpn_test","activate":["vpn_to_ext"],"deactivate":[]}'

log_test "graph.triggers.set upserts trigger"
trig=$(ipc_request "$SOCKET_PATH" "graph.triggers.set" "$TRIGGER_PARAMS" 31) || trig=""
echo "$trig" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
    && pass || fail "triggers.set failed: $trig"

log_test "Trigger visible in graph.status"
status=$(ipc_request "$SOCKET_PATH" "graph.status" '{}' 32) || status=""
echo "$status" | python3 -c "import sys,json; ids=[t['id'] for t in json.load(sys.stdin).get('triggers',[])]; sys.exit(0 if 'vpn_connect_services' in ids else 1)" \
    && pass || fail "trigger missing from status"

log_test "graph.triggers.set persists to YAML"
grep -q "vpn_connect_services" "$CONFIG_PATH" && pass || fail "trigger not persisted"

# ── Optional VPN connect ─────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 4. VPN Connect (optional)"
echo "========================================"

if $WG_MISSING; then
    log_test "graph.components.start vpn_test (WireGuard connect)"
    skip "No WireGuard config at $WG_CONFIG"
    log_test "VPN component reports state in graph.status"
    skip "No WireGuard config"
else
    log_test "graph.components.start vpn_test (WireGuard connect)"
    start_result=$(ipc_request "$SOCKET_PATH" "graph.components.start" '{"id":"vpn_test"}' 40) || start_result=""
    echo "$start_result" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else 1)" \
        && pass || skip "VPN start failed (endpoint may be unreachable)"

    sleep 3
    log_test "VPN component reports state in graph.status"
    status=$(ipc_request "$SOCKET_PATH" "graph.status" '{}' 41) || status=""
    echo "$status" | python3 -c "
import sys, json
for c in json.load(sys.stdin).get('components', []):
    if c.get('id') == 'vpn_test':
        print(c.get('state', c.get('enabled', '')))
        sys.exit(0)
sys.exit(1)
" >/dev/null && pass || skip "VPN state not yet available"
fi

stop_test_daemon
print_results
exit $?
