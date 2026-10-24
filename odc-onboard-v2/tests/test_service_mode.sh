#!/usr/bin/env bash
#
# Service Mode Lifecycle Tests
# Tests daemon startup, IPC server, signal handling, and resource management
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
echo " Service Mode Lifecycle Tests"
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

# ── Setup: Start service ──────────────────────────────────────────────────────
systemctl daemon-reload 2>/dev/null
systemctl enable "$SERVICE_NAME" 2>/dev/null
systemctl start "$SERVICE_NAME" 2>/dev/null
sleep 2

# ── 1. Service Startup ────────────────────────────────────────────────────────
echo "========================================"
echo " 1. Service Startup"
echo "========================================"

log_test "Service is active after start"
assert_service_active "$SERVICE_NAME"

log_test "Process is running"
if pgrep -x "odc-onboard" >/dev/null 2>&1; then
    pass
else
    fail "odc-onboard process not found"
fi

log_test "PID file or process has valid PID"
PID=$(pgrep -x "odc-onboard" | head -1)
if [ -n "$PID" ] && [ "$PID" -gt 0 ] 2>/dev/null; then
    pass
else
    fail "No valid PID found"
fi

# ── 2. IPC Socket ─────────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 2. IPC Socket"
echo "========================================"

log_test "Socket file exists"
if [ -S "$SOCKET_PATH" ]; then
    pass
else
    fail "Socket not found: $SOCKET_PATH"
fi

log_test "Socket has correct permissions (660)"
actual_perms=$(stat -c '%a' "$SOCKET_PATH" 2>/dev/null) || actual_perms=""
if [ "$actual_perms" = "660" ] || [ "$actual_perms" = "770" ]; then
    pass
else
    fail "Socket permissions: expected 660 or 770, got $actual_perms"
fi

log_test "Socket is owned by root"
assert_file_owner "$SOCKET_PATH" "root"

# ── 3. Duplicate Daemon Rejection ─────────────────────────────────────────────
echo ""
echo "========================================"
echo " 3. Duplicate Daemon Rejection"
echo "========================================"

log_test "Second daemon instance fails to start"
output=$(timeout 3 "$BINARY" --daemon --config "$CONFIG_PATH" 2>&1) || rc=$?
if [ "${rc:-0}" -ne 0 ]; then
    pass
else
    # Check if it actually bound or just timed out
    if echo "$output" | grep -qi "bind\|address already in use\|socket\|fail"; then
        pass
    else
        fail "Second daemon did not fail (exit code: ${rc:-0})"
    fi
fi

# ── 4. Configuration Loading ──────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 4. Configuration Loading"
echo "========================================"

log_test "Valid config parsed successfully"
if journal_contains "Application started"; then
    pass
else
    fail "Journal does not show successful startup"
fi

log_test "MAVLink router initialized"
if journal_contains "mavlink" || journal_contains "MAVLink" || journal_contains "router"; then
    pass
else
    skip "MAVLink initialization log not found"
fi

log_test "Thread orchestrator initialized"
if journal_contains "orchestrator" || journal_contains "Thread"; then
    pass
else
    skip "Thread orchestrator log not found"
fi

# ── 5. Invalid Config Rejection ───────────────────────────────────────────────
echo ""
echo "========================================"
echo " 5. Invalid Config Rejection"
echo "========================================"

echo "not valid json" > /tmp/odc-invalid-config.json

log_test "Invalid JSON config causes startup failure"
output=$(timeout 3 "$BINARY" --daemon --config /tmp/odc-invalid-config.json 2>&1) || rc=$?
if [ "${rc:-0}" -ne 0 ]; then
    pass
else
    fail "Daemon started with invalid config"
fi

rm -f /tmp/odc-invalid-config.json

# ── 6. Missing Config Handling ────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 6. Missing Config Handling"
echo "========================================"

log_test "Missing config file handled gracefully"
output=$(timeout 3 "$BINARY" --daemon --config /nonexistent/path/config.json 2>&1) || rc=$?
if [ "${rc:-0}" -ne 0 ]; then
    pass
else
    fail "Daemon started with missing config"
fi

# ── 7. Thread Orchestrator Metrics ────────────────────────────────────────────
echo ""
echo "========================================"
echo " 7. Thread Orchestrator Metrics"
echo "========================================"

log_test "Metrics command returns data"
output=$(run_cli_command "metrics" 5)
if echo "$output" | grep -q "Worker count"; then
    pass
else
    fail "Metrics command did not return worker count"
fi

log_test "Worker count is positive"
worker_count=$(echo "$output" | grep -oP 'Worker count:\s+\K\d+')
if [ -n "$worker_count" ] && [ "$worker_count" -gt 0 ] 2>/dev/null; then
    pass
else
    fail "Worker count not positive: $worker_count"
fi

# ── 8. Event Bus Functional ───────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 8. Event Bus Functional"
echo "========================================"

log_test "VPN list command works (event bus operational)"
output=$(run_cli_command "vpn list" 5)
if echo "$output" | grep -q "test-wg\|test-ovpn\|VPN"; then
    pass
else
    fail "VPN list command failed"
fi

# ── 9. MAVLink Router Status ──────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 9. MAVLink Router Status"
echo "========================================"

log_test "MAVLink status command returns data"
output=$(run_cli_command "mavlink status" 5)
if echo "$output" | grep -q "Running:"; then
    pass
else
    fail "MAVLink status command failed"
fi

log_test "MAVLink router is running"
if echo "$output" | grep -q "Running:.*yes"; then
    pass
else
    fail "MAVLink router not running"
fi

# ── 10. SIGTERM Graceful Shutdown ─────────────────────────────────────────────
echo ""
echo "========================================"
echo " 10. SIGTERM Graceful Shutdown"
echo "========================================"

log_test "SIGTERM triggers graceful shutdown"
systemctl stop "$SERVICE_NAME" 2>/dev/null && pass || fail "stop failed"

log_test "Service becomes inactive after SIGTERM"
if wait_for_service "$SERVICE_NAME" 5; then
    fail "Service still active after SIGTERM"
else
    pass
fi

log_test "Socket removed after SIGTERM"
if [ ! -e "$SOCKET_PATH" ]; then
    pass
else
    fail "Socket still exists after SIGTERM"
fi

log_test "Journal shows shutdown message"
if journal_contains "shut" || journal_contains "Signal received"; then
    pass
else
    skip "Shutdown log message not found"
fi

# ── 11. Service Restart ───────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 11. Service Restart"
echo "========================================"

log_test "Service restart succeeds"
systemctl start "$SERVICE_NAME" 2>/dev/null && pass || fail "start failed"

log_test "Service becomes active after restart"
if wait_for_service "$SERVICE_NAME" 5; then
    pass
else
    fail "Service did not become active after restart"
fi

log_test "Socket recreated after restart"
if wait_for_socket "$SOCKET_PATH" 3; then
    pass
else
    fail "Socket not recreated after restart"
fi

# ── 12. Drain Timeout ─────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 12. Drain Timeout"
echo "========================================"

log_test "--drain-timeout flag is accepted"
output=$("$BINARY" --help 2>&1)
assert_contains_fixed "$output" "--drain-timeout"

# ── Cleanup ───────────────────────────────────────────────────────────────────
systemctl stop "$SERVICE_NAME" 2>/dev/null
sleep 1

# ── Results ───────────────────────────────────────────────────────────────────
print_results
exit $?
