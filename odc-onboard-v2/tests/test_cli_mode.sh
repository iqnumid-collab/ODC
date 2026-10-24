#!/usr/bin/env bash
#
# CLI Mode Interaction Tests
# Tests both remote (IPC) and local (standalone) CLI modes
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/helpers/test_helpers.sh"

BINARY="${BINARY:-/usr/bin/odc-onboard}"
CONFIG_PATH="${CONFIG_PATH:-/etc/odc-onboard/config.json}"
SERVICE_NAME="odc-onboard"
SOCKET_PATH="/run/odc-onboard/odc-onboard.sock"

# ── Header ────────────────────────────────────────────────────────────────────
echo "============================================================"
echo " CLI Mode Interaction Tests"
echo "============================================================"
echo ""
echo "Binary: $BINARY"
echo "Config: $CONFIG_PATH"
echo ""

# ── Prerequisites ─────────────────────────────────────────────────────────────
if ! has_root; then
    echo "ERROR: This test suite requires root privileges"
    exit 1
fi

# ── Setup: Start service for remote tests ─────────────────────────────────────
systemctl daemon-reload 2>/dev/null
systemctl enable "$SERVICE_NAME" 2>/dev/null
systemctl start "$SERVICE_NAME" 2>/dev/null
sleep 2

# ============================================================
# REMOTE MODE TESTS (attached to daemon via IPC socket)
# ============================================================

echo "========================================"
echo " Remote Mode (IPC Attached)"
echo "========================================"

# ── 1. Connection ─────────────────────────────────────────────────────────────
log_test "CLI attaches to running daemon"
output=$(printf 'exit\n' | timeout 5 "$BINARY" --cli --config "$CONFIG_PATH" 2>&1) || true
assert_contains_fixed "$output" "Attached to running daemon"

# ── 2. Help Command ───────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 2. Help Command"
echo "========================================"

log_test "help shows vpn commands"
output=$(printf 'help\nexit\n' | timeout 5 "$BINARY" --cli --config "$CONFIG_PATH" 2>&1) || true
assert_contains_fixed "$output" "vpn list"

log_test "help shows mavlink commands"
assert_contains_fixed "$output" "mavlink status"

log_test "help shows system commands"
assert_contains_fixed "$output" "check-privileges"

# ── 3. VPN List ───────────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 3. VPN List"
echo "========================================"

log_test "vpn list shows test-wg"
output=$(run_cli_command "vpn list" 5)
assert_contains_fixed "$output" "test-wg"

log_test "vpn list shows test-ovpn"
assert_contains_fixed "$output" "test-ovpn"

log_test "vpn list shows wireguard type"
assert_contains_fixed "$output" "WireGuard"

log_test "vpn list shows openvpn type"
assert_contains_fixed "$output" "OpenVPN"

# ── 4. VPN Status ─────────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 4. VPN Status"
echo "========================================"

log_test "vpn status test-wg shows connection details"
output=$(run_cli_command "vpn status test-wg" 5)
assert_contains_fixed "$output" "Connection: test-wg"

log_test "vpn status test-wg shows type"
assert_contains_fixed "$output" "WireGuard"

log_test "vpn status test-ovpn shows connection details"
output=$(run_cli_command "vpn status test-ovpn" 5)
assert_contains_fixed "$output" "Connection: test-ovpn"

log_test "vpn status test-ovpn shows type"
assert_contains_fixed "$output" "OpenVPN"

log_test "vpn status nonexistent shows error"
output=$(run_cli_command "vpn status nonexistent" 5)
assert_contains "$output" "not found\|Not found\|error"

# ── 5. MAVLink Status ─────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 5. MAVLink Status"
echo "========================================"

log_test "mavlink status returns data"
output=$(run_cli_command "mavlink status" 5)
assert_contains_fixed "$output" "MAVLink Router Status"

log_test "mavlink status shows running"
assert_contains_fixed "$output" "Running:"

log_test "mavlink status shows TCP port"
assert_contains_fixed "$output" "TCP Port:"

log_test "mavlink status shows endpoint count"
assert_contains_fixed "$output" "Endpoints:"

log_test "mavlink status shows dedup status"
assert_contains_fixed "$output" "Dedup:"

# ── 6. MAVLink Endpoints ──────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 6. MAVLink Endpoints"
echo "========================================"

log_test "mavlink endpoints lists flight_controller"
output=$(run_cli_command "mavlink endpoints" 5)
assert_contains_fixed "$output" "flight_controller"

log_test "mavlink endpoints shows UART type"
assert_contains_fixed "$output" "UART"

log_test "mavlink endpoints shows ttyACM0"
assert_contains_fixed "$output" "/dev/ttyACM0"

log_test "mavlink endpoints shows statistics columns"
assert_contains_fixed "$output" "Msg In"

# ── 7. MAVLink Stats ──────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 7. MAVLink Stats"
echo "========================================"

log_test "mavlink stats returns data"
output=$(run_cli_command "mavlink stats" 5)
assert_contains_fixed "$output" "Routing Statistics"

log_test "mavlink stats shows total routed"
assert_contains_fixed "$output" "Total Routed:"

log_test "mavlink stats shows total filtered"
assert_contains_fixed "$output" "Total Filtered:"

# ── 8. MAVLink Logging ────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 8. MAVLink Logging"
echo "========================================"

log_test "mavlink log start succeeds"
output=$(run_cli_command "mavlink log start" 5)
if echo "$output" | grep -qi "started\|success"; then
    pass
else
    fail "Log start failed: $output"
fi

sleep 2

log_test "mavlink log stop succeeds"
output=$(run_cli_command "mavlink log stop" 5)
if echo "$output" | grep -qi "stopped\|success"; then
    pass
else
    fail "Log stop failed: $output"
fi

# ── 9. Metrics ────────────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 9. Metrics"
echo "========================================"

log_test "metrics shows active tasks"
output=$(run_cli_command "metrics" 5)
assert_contains_fixed "$output" "Active tasks"

log_test "metrics shows worker count"
assert_contains_fixed "$output" "Worker count"

# ── 10. Log Level ─────────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 10. Log Level"
echo "========================================"

log_test "log level debug succeeds"
output=$(run_cli_command "log level debug" 5)
if echo "$output" | grep -qi "debug"; then
    pass
else
    fail "Log level change failed: $output"
fi

# ── 11. Check Privileges ──────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 11. Check Privileges"
echo "========================================"

log_test "check-privileges shows CAP_NET_ADMIN"
output=$(run_cli_command "check-privileges" 5)
assert_contains_fixed "$output" "CAP_NET_ADMIN"

# ── 12. VPN Config Validation ─────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 12. VPN Config Validation"
echo "========================================"

log_test "vpn config validate WireGuard succeeds"
output=$(run_cli_command "vpn config validate /etc/odc-onboard/vpn/dev.conf" 5)
assert_contains_fixed "$output" "Valid: YES"

log_test "vpn config validate OpenVPN succeeds"
output=$(run_cli_command "vpn config validate /etc/odc-onboard/vpn/dev.ovpn" 5)
assert_contains_fixed "$output" "Valid: YES"

# ── 13. Unknown Command ───────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 13. Unknown Command"
echo "========================================"

log_test "Unknown command shows error"
output=$(printf 'foobar\nexit\n' | timeout 5 "$BINARY" --cli --config "$CONFIG_PATH" 2>&1) || true
assert_contains "$output" "Unknown"

# ── 14. Exit Commands ─────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 14. Exit Commands"
echo "========================================"

log_test "exit command closes CLI"
output=$(printf 'exit\n' | timeout 5 "$BINARY" --cli --config "$CONFIG_PATH" 2>&1) || true
assert_contains_fixed "$output" "Exiting"

log_test "quit command closes CLI"
output=$(printf 'quit\n' | timeout 5 "$BINARY" --cli --config "$CONFIG_PATH" 2>&1) || true
assert_contains_fixed "$output" "Exiting"

log_test "EOF closes CLI"
output=$(echo "" | timeout 5 "$BINARY" --cli --config "$CONFIG_PATH" 2>&1) || true
assert_contains_fixed "$output" "Exiting"

# ── 15. JSON Output Mode ──────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 15. JSON Output Mode"
echo "========================================"

log_test "--json mode: vpn list produces valid JSON"
output=$(run_cli_command_json "vpn list" 5)
assert_valid_json "$output"

log_test "--json mode: mavlink status produces valid JSON"
output=$(run_cli_command_json "mavlink status" 5)
assert_valid_json "$output"

log_test "--json mode: mavlink status has running field"
echo "$output" | python3 -c "import sys,json; d=json.load(sys.stdin); assert 'running' in d" 2>/dev/null && pass || fail "Missing 'running' field"

log_test "--json mode: metrics produces valid JSON"
output=$(run_cli_command_json "metrics" 5)
assert_valid_json "$output"

log_test "--json mode: metrics has worker_count field"
echo "$output" | python3 -c "import sys,json; d=json.load(sys.stdin); assert 'worker_count' in d" 2>/dev/null && pass || fail "Missing 'worker_count' field"

# ============================================================
# LOCAL MODE TESTS (standalone, no daemon)
# ============================================================

echo ""
echo "========================================"
echo " Local Mode (Standalone)"
echo "========================================"

# Stop daemon for local mode tests
systemctl stop "$SERVICE_NAME" 2>/dev/null
sleep 2

log_test "Socket removed before local mode test"
if [ ! -e "$SOCKET_PATH" ]; then
    pass
else
    fail "Socket still exists"
fi

log_test "CLI starts in standalone mode"
output=$(printf 'exit\n' | timeout 10 "$BINARY" --cli --config "$CONFIG_PATH" 2>&1) || true
assert_contains_fixed "$output" "Standalone mode"

log_test "Local mode: vpn list works"
output=$(printf 'vpn list\nexit\n' | timeout 10 "$BINARY" --cli --config "$CONFIG_PATH" 2>&1) || true
if echo "$output" | grep -q "test-wg\|test-ovpn\|VPN\|No VPN"; then
    pass
else
    fail "VPN list failed in local mode"
fi

log_test "Local mode: mavlink status works"
output=$(printf 'mavlink status\nexit\n' | timeout 10 "$BINARY" --cli --config "$CONFIG_PATH" 2>&1) || true
if echo "$output" | grep -q "MAVLink\|Running:"; then
    pass
else
    fail "MAVLink status failed in local mode"
fi

# ── Restart daemon for subsequent tests ───────────────────────────────────────
systemctl start "$SERVICE_NAME" 2>/dev/null
sleep 2

# ── Results ───────────────────────────────────────────────────────────────────
print_results
exit $?
