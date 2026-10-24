#!/usr/bin/env bash
#
# odc-onboard-v2 Phase 0 — Graph schema / IPC smoke tests
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/helpers/test_helpers.sh"

BINARY="${BINARY:-$PROJECT_DIR/build/odc-onboard-v2}"
EXAMPLE_CONFIG="$PROJECT_DIR/config/system.example.yaml"
TEST_DIR=$(mktemp -d /tmp/odc-graph-schema-XXXXXX)
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

prepare_example_config() {
    cp "$EXAMPLE_CONFIG" "$CONFIG_PATH"
    sed -i "s|/run/odc-onboard/odc-onboard.sock|$SOCKET_PATH|g" "$CONFIG_PATH"
}

# ipc_request is provided by helpers/test_helpers.sh (python3 + Unix socket JSON-RPC).

echo "============================================================"
echo " Graph Schema Tests (Phase 0)"
echo "============================================================"
echo ""
echo "Binary: $BINARY"
echo "Example config: $EXAMPLE_CONFIG"
echo "Test dir: $TEST_DIR"
echo ""

if [ ! -x "$BINARY" ]; then
    echo "ERROR: Binary not found or not executable: $BINARY"
    exit 1
fi

# ── 1. Fixture / file checks ─────────────────────────────────────────────────
echo "========================================"
echo " 1. Example Config"
echo "========================================"

log_test "system.example.yaml exists"
assert_file_exists "$EXAMPLE_CONFIG"

log_test "system.example.yaml declares version 2"
assert_contains_fixed "$(head -20 "$EXAMPLE_CONFIG")" "version: 2"

log_test "system.example.yaml lists components"
assert_contains_fixed "$(cat "$EXAMPLE_CONFIG")" "components:"

log_test "system.example.yaml lists links"
assert_contains_fixed "$(cat "$EXAMPLE_CONFIG")" "links:"

log_test "system.example.yaml lists triggers"
assert_contains_fixed "$(cat "$EXAMPLE_CONFIG")" "triggers:"

prepare_example_config

log_test "Prepared temp config is readable"
assert_file_exists "$CONFIG_PATH"

# ── 2. Invalid YAML rejection ────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 2. Invalid YAML Rejection"
echo "========================================"

cat > "$TEST_DIR/invalid.yaml" <<'EOF'
version: 2
components:
  - id: [unclosed
EOF

log_test "Malformed YAML fails daemon startup"
output=$("$BINARY" --config "$TEST_DIR/invalid.yaml" --daemon --socket "$TEST_DIR/bad.sock" 2>&1) || rc=$?
if [ "${rc:-0}" -ne 0 ]; then
    pass
else
    fail "Daemon should reject malformed YAML (exit ${rc:-0})"
fi

cat > "$TEST_DIR/invalid_schema.yaml" <<'EOF'
version: 2
general:
  log_level: error
components:
  - id: dup
    type: vpn.wireguard
    enabled: false
    config:
      config_path: /tmp/none.conf
  - id: dup
    type: vpn.wireguard
    enabled: false
    config:
      config_path: /tmp/none.conf
links: []
triggers: []
EOF

if has_net_admin; then
    log_test "Structurally invalid schema: daemon starts but graph.validate fails"
    start_test_daemon "$TEST_DIR/invalid_schema.yaml" && pass || fail "Daemon failed to start"
    if [ -S "$SOCKET_PATH" ]; then
        log_test "graph.validate reports invalid for duplicate component ids"
        result=$(ipc_request "$SOCKET_PATH" "graph.validate" '{}' 2) || true
        valid=$(echo "$result" | python3 -c "import sys,json; print(json.load(sys.stdin).get('valid',''))" 2>/dev/null)
        if [ "$valid" = "False" ] || [ "$valid" = "false" ]; then
            pass
        else
            fail "Expected valid=false, got '$valid'"
        fi
        stop_test_daemon
    else
        log_test "graph.validate reports invalid for duplicate component ids"
        skip "IPC socket unavailable"
    fi
else
    log_test "Structurally invalid schema: daemon starts but graph.validate fails"
    skip "Requires CAP_NET_ADMIN"
    log_test "graph.validate reports invalid for duplicate component ids"
    skip "Requires CAP_NET_ADMIN"
fi

# ── 3. Daemon + graph.status / graph.validate ────────────────────────────────
echo ""
echo "========================================"
echo " 3. graph.status + graph.validate"
echo "========================================"

if ! has_net_admin; then
    log_test "Daemon loads system.example.yaml"
    skip "Requires CAP_NET_ADMIN"
    log_test "IPC socket created in temp dir"
    skip "Requires CAP_NET_ADMIN"
    log_test "graph.status returns version=2"
    skip "Requires CAP_NET_ADMIN"
    log_test "graph.status includes expected components"
    skip "Requires CAP_NET_ADMIN"
    log_test "graph.validate returns valid=true"
    skip "Requires CAP_NET_ADMIN"
else
    prepare_example_config
    log_test "Daemon loads system.example.yaml"
    if start_test_daemon "$CONFIG_PATH"; then
        pass
    else
        fail "Daemon failed to start (see $TEST_DIR/daemon.log)"
    fi

    log_test "IPC socket created in temp dir"
    if [ -S "$SOCKET_PATH" ]; then pass; else fail "Missing socket: $SOCKET_PATH"; fi

    log_test "graph.status returns version=2"
    status=$(ipc_request "$SOCKET_PATH" "graph.status" '{}' 1) || status=""
    assert_json_field "$status" "version" "2"

    log_test "graph.status includes expected components"
    count=$(echo "$status" | python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d.get('components',[])))" 2>/dev/null) || count=0
    if [ "${count:-0}" -ge 5 ] 2>/dev/null; then
        pass
    else
        fail "Expected >= 5 components, got ${count:-0}"
    fi

    for comp in cellular0 vpn_cloud mav_primary mav_ext_cloud rtsp_cloud; do
        log_test "graph.status lists component '$comp'"
        echo "$status" | python3 -c "import sys,json; ids=[c['id'] for c in json.load(sys.stdin).get('components',[])]; sys.exit(0 if '$comp' in ids else 1)" \
            && pass || fail "Missing component $comp"
    done

    log_test "graph.validate returns valid=true"
    validate=$(ipc_request "$SOCKET_PATH" "graph.validate" '{}' 2) || validate=""
    valid=$(echo "$validate" | python3 -c "import sys,json; print(json.load(sys.stdin).get('valid',''))" 2>/dev/null)
    if [ "$valid" = "True" ] || [ "$valid" = "true" ]; then
        pass
    else
        fail "Expected valid=true, got '$valid' (response: $validate)"
    fi

    stop_test_daemon
fi

print_results
exit $?
