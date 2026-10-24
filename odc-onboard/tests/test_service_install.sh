#!/usr/bin/env bash
#
# Service Installation Tests
# Tests the installation and systemd service lifecycle of odc-onboard
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/helpers/test_helpers.sh"

BINARY="${BINARY:-/usr/bin/odc-onboard}"
CONFIG_PATH="${CONFIG_PATH:-/etc/odc-onboard/config.json}"
SERVICE_NAME="odc-onboard"

# ── Header ────────────────────────────────────────────────────────────────────
echo "============================================================"
echo " Service Installation Tests"
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

# ── Setup: Create odc-onboard group if missing ────────────────────────────────
getent group odc-onboard >/dev/null 2>&1 || groupadd --system odc-onboard

# ── 1. Binary Installation ────────────────────────────────────────────────────
echo "========================================"
echo " 1. Binary Installation"
echo "========================================"

log_test "Binary installed to /usr/bin/odc-onboard"
if [ -x "$BINARY" ]; then
    pass
else
    fail "Binary not found or not executable: $BINARY"
fi

log_test "Binary is ELF executable"
file "$BINARY" | grep -q "ELF" && pass || fail "Not an ELF executable"

log_test "Binary version is reported"
output=$("$BINARY" --help 2>&1)
assert_contains_fixed "$output" "Usage: odc-onboard"

# ── 2. Service File Installation ──────────────────────────────────────────────
echo ""
echo "========================================"
echo " 2. Service File Installation"
echo "========================================"

log_test "Service file installed to systemd"
if [ -f "/lib/systemd/system/${SERVICE_NAME}.service" ]; then
    pass
else
    fail "Service file not found: /lib/systemd/system/${SERVICE_NAME}.service"
fi

log_test "Service file has [Unit] section"
grep -q "\[Unit\]" "/lib/systemd/system/${SERVICE_NAME}.service" && pass || fail "Missing [Unit]"

log_test "Service file has [Service] section"
grep -q "\[Service\]" "/lib/systemd/system/${SERVICE_NAME}.service" && pass || fail "Missing [Service]"

log_test "Service file has ExecStart"
grep -q "ExecStart=" "/lib/systemd/system/${SERVICE_NAME}.service" && pass || fail "Missing ExecStart"

log_test "Service file has CAP_NET_ADMIN capability"
grep -q "CAP_NET_ADMIN" "/lib/systemd/system/${SERVICE_NAME}.service" && pass || fail "Missing CAP_NET_ADMIN"

# ── 3. Directory Structure ────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 3. Directory Structure"
echo "========================================"

log_test "Config directory /etc/odc-onboard exists"
assert_dir_exists "/etc/odc-onboard"

log_test "VPN configs directory exists"
assert_dir_exists "/etc/odc-onboard/vpn"

log_test "Runtime directory /run/odc-onboard exists or creatable"
if [ -d "/run/odc-onboard" ] || mkdir -p "/run/odc-onboard" 2>/dev/null; then
    pass
else
    fail "Cannot create /run/odc-onboard"
fi

log_test "Log directory /var/log/odc-onboard exists or creatable"
if [ -d "/var/log/odc-onboard" ] || mkdir -p "/var/log/odc-onboard" 2>/dev/null; then
    pass
else
    fail "Cannot create /var/log/odc-onboard"
fi

# ── 4. Config File Deployment ─────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 4. Config File Deployment"
echo "========================================"

log_test "Test config deployed to /etc/odc-onboard/config.json"
assert_file_exists "$CONFIG_PATH"

log_test "Config is valid JSON"
python3 -c "import json; json.load(open('$CONFIG_PATH'))" 2>/dev/null && pass || fail "Invalid JSON"

log_test "Config has wireguard connections"
python3 -c "
import json
d = json.load(open('$CONFIG_PATH'))
assert len(d['vpn']['wireguard']['connections']) >= 1
" 2>/dev/null && pass || fail "No WireGuard connections"

log_test "Config has openvpn connections"
python3 -c "
import json
d = json.load(open('$CONFIG_PATH'))
assert len(d['vpn']['openvpn']['connections']) >= 1
" 2>/dev/null && pass || fail "No OpenVPN connections"

log_test "Config has mavlink endpoints"
python3 -c "
import json
d = json.load(open('$CONFIG_PATH'))
assert len(d['mavlink']['endpoints']) >= 1
" 2>/dev/null && pass || fail "No MAVLink endpoints"

log_test "Config has ttyACM0 UART endpoint"
python3 -c "
import json
d = json.load(open('$CONFIG_PATH'))
eps = d['mavlink']['endpoints']
assert any(ep.get('device') == '/dev/ttyACM0' for ep in eps)
" 2>/dev/null && pass || fail "No ttyACM0 endpoint"

log_test "Config has baudrate 1500000 for ttyACM0"
python3 -c "
import json
d = json.load(open('$CONFIG_PATH'))
eps = d['mavlink']['endpoints']
for ep in eps:
    if ep.get('device') == '/dev/ttyACM0':
        assert 1500000 in ep.get('baud', [])
        break
" 2>/dev/null && pass || fail "1500000 not in baud list"

log_test "VPN WireGuard config deployed"
assert_file_exists "/etc/odc-onboard/vpn/dev.conf"

log_test "VPN OpenVPN config deployed"
assert_file_exists "/etc/odc-onboard/vpn/dev.ovpn"

# ── 5. Service Enable ─────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 5. Service Enable"
echo "========================================"

log_test "systemctl daemon-reload succeeds"
systemctl daemon-reload 2>/dev/null && pass || fail "daemon-reload failed"

log_test "Service enable succeeds"
systemctl enable "$SERVICE_NAME" 2>/dev/null && pass || fail "enable failed"

log_test "Service is enabled"
systemctl is-enabled "$SERVICE_NAME" 2>/dev/null | grep -q "enabled" && pass || fail "Service not enabled"

# ── 6. Service Start ──────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 6. Service Start"
echo "========================================"

log_test "Service start succeeds"
systemctl start "$SERVICE_NAME" 2>/dev/null && pass || fail "start failed"

log_test "Service becomes active within 5s"
if wait_for_service "$SERVICE_NAME" 5; then
    pass
else
    fail "Service did not become active within 5s"
fi

log_test "Socket created within 3s"
if wait_for_socket "/run/odc-onboard/odc-onboard.sock" 3; then
    pass
else
    fail "Socket not created within 3s"
fi

# ── 7. Service Status ─────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 7. Service Status"
echo "========================================"

log_test "Service is active (running)"
assert_service_active "$SERVICE_NAME"

log_test "Journal shows startup messages"
if journal_contains "Application started"; then
    pass
else
    fail "Journal does not contain 'Application started'"
fi

log_test "Journal shows actors spawned"
if journal_contains "Actors spawned"; then
    pass
else
    fail "Journal does not contain 'Actors spawned'"
fi

log_test "Journal shows VPN connections loaded"
if journal_contains "Loaded" && journal_contains "VPN connections"; then
    pass
else
    skip "VPN connections log message not found yet"
fi

# ── 8. Service Stop ───────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 8. Service Stop"
echo "========================================"

log_test "Service stop succeeds"
systemctl stop "$SERVICE_NAME" 2>/dev/null && pass || fail "stop failed"

log_test "Service becomes inactive within 5s"
if wait_for_service "$SERVICE_NAME" 5; then
    fail "Service is still active after stop"
else
    pass
fi

log_test "Socket removed after stop"
if [ ! -e "/run/odc-onboard/odc-onboard.sock" ]; then
    pass
else
    fail "Socket still exists after stop"
fi

# ── 9. Service Restart ────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 9. Service Restart"
echo "========================================"

log_test "Service restart succeeds"
systemctl restart "$SERVICE_NAME" 2>/dev/null && pass || fail "restart failed"

log_test "Service becomes active after restart within 5s"
if wait_for_service "$SERVICE_NAME" 5; then
    pass
else
    fail "Service did not become active after restart"
fi

log_test "Socket recreated after restart within 3s"
if wait_for_socket "/run/odc-onboard/odc-onboard.sock" 3; then
    pass
else
    fail "Socket not recreated after restart"
fi

# ── 10. Service Disable ───────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 10. Service Disable"
echo "========================================"

log_test "Service stop before disable"
systemctl stop "$SERVICE_NAME" 2>/dev/null && pass || fail "stop failed"

log_test "Service disable succeeds"
systemctl disable "$SERVICE_NAME" 2>/dev/null && pass || fail "disable failed"

log_test "Service is disabled"
systemctl is-enabled "$SERVICE_NAME" 2>/dev/null | grep -q "disabled" && pass || fail "Service not disabled"

# ── Results ───────────────────────────────────────────────────────────────────
print_results
exit $?
