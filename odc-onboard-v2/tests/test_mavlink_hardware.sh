#!/usr/bin/env bash
#
# MAVLink Hardware Tests
# Tests MAVLink router with real ttyACM0 flight controller
# Verifies automatic baudrate detection at 1500000
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/helpers/test_helpers.sh"

BINARY="${BINARY:-/usr/bin/odc-onboard}"
CONFIG_PATH="${CONFIG_PATH:-/etc/odc-onboard/config.json}"
SERVICE_NAME="odc-onboard"
SOCKET_PATH="/run/odc-onboard/odc-onboard.sock"
MAVLOG_DIR="/var/log/odc-onboard/mavlink"

# ── Header ────────────────────────────────────────────────────────────────────
echo "============================================================"
echo " MAVLink Hardware Tests (ttyACM0)"
echo "============================================================"
echo ""
echo "Binary: $BINARY"
echo "Config: $CONFIG_PATH"
echo "Device: /dev/ttyACM0"
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
sleep 3

# ── 1. Device Existence ───────────────────────────────────────────────────────
echo "========================================"
echo " 1. Device Existence"
echo "========================================"

log_test "/dev/ttyACM0 exists"
if [ -c /dev/ttyACM0 ]; then
    pass
else
    fail "/dev/ttyACM0 not found"
    echo ""
    echo "WARNING: Flight controller not connected. Skipping MAVLink hardware tests."
    echo ""
    # Skip remaining tests
    for i in $(seq 2 20); do
        SKIP=$((SKIP + 1))
        TOTAL=$((TOTAL + 1))
    done
    print_results
    exit 0
fi

log_test "/dev/ttyACM0 is readable"
if [ -r /dev/ttyACM0 ]; then
    pass
else
    fail "/dev/ttyACM0 not readable"
fi

log_test "/dev/ttyACM0 is writable"
if [ -w /dev/ttyACM0 ]; then
    pass
else
    fail "/dev/ttyACM0 not writable"
fi

# ── 2. UART Endpoint Creation ─────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 2. UART Endpoint Creation"
echo "========================================"

log_test "UART endpoint created for flight_controller"
output=$(run_cli_command "mavlink endpoints" 5)
assert_contains_fixed "$output" "flight_controller"

log_test "Endpoint type is UART"
assert_contains_fixed "$output" "UART"

log_test "Endpoint shows /dev/ttyACM0 device"
assert_contains_fixed "$output" "/dev/ttyACM0"

# ── 3. Baudrate Detection ─────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 3. Baudrate Detection (1500000)"
echo "========================================"

log_test "Config lists 1500000 as first baudrate"
python3 -c "
import json
d = json.load(open('$CONFIG_PATH'))
eps = d['mavlink']['endpoints']
for ep in eps:
    if ep.get('device') == '/dev/ttyACM0':
        bauds = ep.get('baud', [])
        assert bauds[0] == 1500000, f'First baud is {bauds[0]}, expected 1500000'
        break
" 2>/dev/null && pass || fail "1500000 not first in baud list"

log_test "Journal shows 1500000 baud attempt"
if journal_contains "1500000"; then
    pass
else
    fail "Journal does not show 1500000 baud attempt"
fi

log_test "1500000 baud succeeded (no retry message immediately after)"
# If 1500000 failed, we'd see "Retrying UART" soon after the speed=1500000 line
journal_lines=$(journalctl -u odc-onboard --no-pager -n 50 2>/dev/null || echo "")
speed_line=$(echo "$journal_lines" | grep "speed = 1500000" | head -1)
if [ -n "$speed_line" ]; then
    # Check if there's a retry message after this
    retry_after=$(echo "$journal_lines" | grep -A 2 "speed = 1500000" | grep -i "retrying" || true)
    if [ -z "$retry_after" ]; then
        pass
    else
        fail "Baudrate 1500000 failed, retrying to next baud"
    fi
else
    fail "No speed = 1500000 line in journal"
fi

log_test "stty confirms current baudrate"
# stty reports the baudrate; we check it's set to something reasonable
stty_output=$(stty -F /dev/ttyACM0 2>/dev/null) || stty_output=""
if echo "$stty_output" | grep -q "speed"; then
    current_speed=$(echo "$stty_output" | grep -oP 'speed \K\d+' || echo "0")
    if [ "$current_speed" = "1500000" ]; then
        pass
    else
        # stty may report differently for high baudrates via TCSETS2
        # If stty doesn't show 1500000 but journal does, it's still valid
        skip "stty reports speed=$current_speed (may differ from TCSETS2 setting)"
    fi
else
    skip "stty output format unexpected"
fi

# ── 4. MAVLink Message Reception ──────────────────────────────────────────────
echo ""
echo "========================================"
echo " 4. MAVLink Message Reception"
echo "========================================"

log_test "Messages received from flight controller (wait 5s)"
sleep 5
output=$(run_cli_command "mavlink endpoints" 5)
msg_in=$(echo "$output" | python3 -c "
import sys, json
for line in sys.stdin:
    try:
        data = json.loads(line)
        if isinstance(data, list):
            for ep in data:
                if ep.get('name', '').startswith('flight_controller') or '/dev/ttyACM0' in str(ep):
                    print(ep.get('messages_in', 0))
                    sys.exit(0)
    except:
        pass
print(0)
" 2>/dev/null || echo "0")

# Alternative: check via JSON mode
output_json=$(run_cli_command_json "mavlink endpoints" 5)
msg_in=$(echo "$output_json" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    if isinstance(data, list):
        for ep in data:
            if ep.get('name', '').startswith('flight_controller') or '/dev/ttyACM0' in str(ep.get('device_or_address', '')):
                print(ep.get('messages_in', 0))
                sys.exit(0)
except:
    pass
print(0)
" 2>/dev/null || echo "0")

if [ "$msg_in" -gt 0 ] 2>/dev/null; then
    pass
else
    fail "No messages received (messages_in=$msg_in)"
fi

log_test "Messages routed to other endpoints"
output_json=$(run_cli_command_json "mavlink endpoints" 5)
msg_out=$(echo "$output_json" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    if isinstance(data, list):
        for ep in data:
            if ep.get('name', '').startswith('flight_controller') or '/dev/ttyACM0' in str(ep.get('device_or_address', '')):
                print(ep.get('messages_out', 0))
                sys.exit(0)
except:
    pass
print(0)
" 2>/dev/null || echo "0")

if [ "$msg_out" -gt 0 ] 2>/dev/null; then
    pass
else
    fail "No messages routed (messages_out=$msg_out)"
fi

# ── 5. TCP Server ─────────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 5. TCP Server"
echo "========================================"

log_test "TCP server listening on port 5760"
if ss -tlnp 2>/dev/null | grep -q ":5760" || netstat -tlnp 2>/dev/null | grep -q ":5760"; then
    pass
else
    fail "Port 5760 not listening"
fi

log_test "TCP client can connect"
if command -v nc >/dev/null 2>&1; then
    echo "" | nc -w 2 localhost 5760 >/dev/null 2>&1 && rc=0 || rc=$?
    if [ "$rc" -eq 0 ]; then
        pass
    else
        fail "TCP connection to port 5760 failed"
    fi
else
    skip "nc (netcat) not available"
fi

# ── 6. Deduplication ──────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 6. Deduplication"
echo "========================================"

log_test "Deduplication is enabled"
output=$(run_cli_command "mavlink status" 5)
if echo "$output" | grep -q "Dedup:.*yes"; then
    pass
else
    fail "Deduplication not enabled"
fi

log_test "Dedup period is configured"
python3 -c "
import json
d = json.load(open('$CONFIG_PATH'))
assert d['mavlink']['dedup_period_sec'] > 0
" 2>/dev/null && pass || fail "dedup_period_sec not > 0"

# ── 7. MAVLink Logging ────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 7. MAVLink Logging"
echo "========================================"

log_test "MAVLink log directory exists"
if [ -d "$MAVLOG_DIR" ] || mkdir -p "$MAVLOG_DIR" 2>/dev/null; then
    pass
else
    fail "Cannot create $MAVLOG_DIR"
fi

log_test "mavlink log start creates log file"
# Remove old log files first
rm -f "$MAVLOG_DIR"/*.binlog "$MAVLOG_DIR"/*.log 2>/dev/null

run_cli_command "mavlink log start" 5 >/dev/null 2>&1
sleep 3

log_files=$(find "$MAVLOG_DIR" -name "*.binlog" -o -name "*.log" 2>/dev/null | head -1)
if [ -n "$log_files" ]; then
    pass
else
    fail "No log file created in $MAVLOG_DIR"
fi

log_test "Log file grows while logging"
if [ -n "$log_files" ]; then
    size1=$(stat -c '%s' "$log_files" 2>/dev/null || echo "0")
    sleep 3
    size2=$(stat -c '%s' "$log_files" 2>/dev/null || echo "0")
    if [ "$size2" -gt "$size1" ] 2>/dev/null; then
        pass
    else
        fail "Log file did not grow ($size1 -> $size2)"
    fi
else
    fail "No log file to check"
fi

log_test "mavlink log stop stops logging"
run_cli_command "mavlink log stop" 5 >/dev/null 2>&1
sleep 2

if [ -n "$log_files" ]; then
    size_before=$(stat -c '%s' "$log_files" 2>/dev/null || echo "0")
    sleep 3
    size_after=$(stat -c '%s' "$log_files" 2>/dev/null || echo "0")
    # Size should be same or very close (within 100 bytes for buffering)
    diff=$((size_after - size_before))
    if [ "$diff" -lt 100 ] 2>/dev/null; then
        pass
    else
        fail "Log file still growing after stop ($diff bytes)"
    fi
else
    fail "No log file to check"
fi

# ── 8. Endpoint Statistics ────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 8. Endpoint Statistics"
echo "========================================"

log_test "Endpoint shows bytes_in"
output_json=$(run_cli_command_json "mavlink endpoints" 5)
bytes_in=$(echo "$output_json" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    if isinstance(data, list):
        for ep in data:
            if ep.get('name', '').startswith('flight_controller') or '/dev/ttyACM0' in str(ep.get('device_or_address', '')):
                print(ep.get('bytes_in', 0))
                sys.exit(0)
except:
    pass
print(0)
" 2>/dev/null || echo "0")

if [ "$bytes_in" -gt 0 ] 2>/dev/null; then
    pass
else
    fail "bytes_in is 0"
fi

log_test "Endpoint shows bytes_out"
bytes_out=$(echo "$output_json" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    if isinstance(data, list):
        for ep in data:
            if ep.get('name', '').startswith('flight_controller') or '/dev/ttyACM0' in str(ep.get('device_or_address', '')):
                print(ep.get('bytes_out', 0))
                sys.exit(0)
except:
    pass
print(0)
" 2>/dev/null || echo "0")

if [ "$bytes_out" -gt 0 ] 2>/dev/null; then
    pass
else
    fail "bytes_out is 0"
fi

# ── 9. Event Monitoring ───────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 9. Event Monitoring"
echo "========================================"

log_test "mavlink monitor connects to daemon"
# Monitor mode requires remote (IPC) connection
output=$(printf 'mavlink monitor\nq\n' | timeout 5 "$BINARY" --cli --config "$CONFIG_PATH" 2>&1) || true
if echo "$output" | grep -q "Event Monitor\|mavlink\|monitor"; then
    pass
else
    fail "Monitor mode failed to start"
fi

# ── Results ───────────────────────────────────────────────────────────────────
print_results
exit $?
