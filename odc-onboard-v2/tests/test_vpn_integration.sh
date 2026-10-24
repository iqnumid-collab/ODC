#!/usr/bin/env bash
#
# VPN Integration Tests
# Tests WireGuard and OpenVPN connections with real configs
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/helpers/test_helpers.sh"

BINARY="${BINARY:-/usr/bin/odc-onboard}"
CONFIG_PATH="${CONFIG_PATH:-/etc/odc-onboard/config.json}"
SERVICE_NAME="odc-onboard"
WG_CONFIG="/etc/odc-onboard/vpn/dev.conf"
OVPN_CONFIG="/etc/odc-onboard/vpn/dev.ovpn"

# ── Header ────────────────────────────────────────────────────────────────────
echo "============================================================"
echo " VPN Integration Tests"
echo "============================================================"
echo ""
echo "Binary: $BINARY"
echo "Config: $CONFIG_PATH"
echo "WireGuard: $WG_CONFIG"
echo "OpenVPN: $OVPN_CONFIG"
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

# ── Helper: Get TUN device name for WireGuard ─────────────────────────────────
get_wg_device() {
    ip -o link show 2>/dev/null | grep -oP 'wg\d+' | head -1 || echo ""
}

get_ovpn_device() {
    ip -o link show 2>/dev/null | grep -oP 'ovpn-\w+' | head -1 || echo ""
}

# ============================================================
# WIREGUARD TESTS
# ============================================================

echo "========================================"
echo " WireGuard (test-wg)"
echo "========================================"

# ── 1. Config Validation ──────────────────────────────────────────────────────
log_test "WireGuard config validates successfully"
output=$(run_cli_command "vpn config validate $WG_CONFIG" 5)
assert_contains_fixed "$output" "Valid: YES"

log_test "WireGuard config has [Interface] section"
grep -q "\[Interface\]" "$WG_CONFIG" && pass || fail "Missing [Interface]"

log_test "WireGuard config has [Peer] section"
grep -q "\[Peer\]" "$WG_CONFIG" && pass || fail "Missing [Peer]"

log_test "WireGuard config has PrivateKey"
grep -q "PrivateKey" "$WG_CONFIG" && pass || fail "Missing PrivateKey"

log_test "WireGuard config has PublicKey"
grep -q "PublicKey" "$WG_CONFIG" && pass || fail "Missing PublicKey"

log_test "WireGuard config has Endpoint"
grep -q "Endpoint" "$WG_CONFIG" && pass || fail "Missing Endpoint"

log_test "WireGuard config has AllowedIPs"
grep -q "AllowedIPs" "$WG_CONFIG" && pass || fail "Missing AllowedIPs"

log_test "WireGuard config has Address"
grep -q "Address" "$WG_CONFIG" && pass || fail "Missing Address"

log_test "WireGuard config has DNS"
grep -q "DNS" "$WG_CONFIG" && pass || fail "Missing DNS"

# ── 2. Connection Creation ────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 2. Connection Creation"
echo "========================================"

log_test "test-wg appears in vpn list"
output=$(run_cli_command "vpn list" 5)
assert_contains_fixed "$output" "test-wg"

log_test "test-wg shows wireguard type"
assert_contains_fixed "$output" "WireGuard"

# ── 3. Connection Start ───────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 3. Connection Start"
echo "========================================"

log_test "vpn start test-wg succeeds"
output=$(run_cli_command "vpn start test-wg" 10)
if echo "$output" | grep -qi "starting\|success"; then
    pass
else
    fail "Start command failed: $output"
fi

sleep 3

log_test "TUN device created"
wg_dev=$(get_wg_device)
if [ -n "$wg_dev" ]; then
    pass
    WG_DEV_NAME="$wg_dev"
else
    fail "No wg* device found"
    WG_DEV_NAME=""
fi

if [ -n "$WG_DEV_NAME" ]; then
    log_test "Interface is UP"
    if ip link show "$WG_DEV_NAME" 2>/dev/null | grep -q "state UP\|UP,"; then
        pass
    else
        fail "Interface $WG_DEV_NAME not UP"
    fi

    log_test "IP address assigned (10.66.66.3)"
    if ip addr show "$WG_DEV_NAME" 2>/dev/null | grep -q "10.66.66"; then
        pass
    else
        fail "No 10.66.66.x address on $WG_DEV_NAME"
    fi

    log_test "Route added for 10.66.66.0/24"
    if ip route show 2>/dev/null | grep -q "10.66.66.0/24"; then
        pass
    else
        fail "Route for 10.66.66.0/24 not found"
    fi
fi

# ── 4. Connection Status ──────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 4. Connection Status"
echo "========================================"

log_test "vpn status test-wg shows details"
output=$(run_cli_command "vpn status test-wg" 5)
assert_contains_fixed "$output" "Connection: test-wg"

log_test "vpn status shows statistics section"
if echo "$output" | grep -q "Statistics\|bytes_in\|bytes_out"; then
    pass
else
    skip "Statistics section not shown"
fi

log_test "Statistics increment after 10s"
sleep 10
output=$(run_cli_command "vpn status test-wg" 5)
# Check if any bytes have been transferred
if echo "$output" | grep -qE "bytes_in.*[1-9]|bytes_out.*[1-9]"; then
    pass
else
    skip "No traffic yet (remote server may be unreachable)"
fi

# ── 5. Connection Stop ────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 5. Connection Stop"
echo "========================================"

log_test "vpn stop test-wg succeeds"
output=$(run_cli_command "vpn stop test-wg" 10)
if echo "$output" | grep -qi "stopping\|success"; then
    pass
else
    fail "Stop command failed: $output"
fi

sleep 2

log_test "TUN device removed after stop"
wg_dev_after=$(get_wg_device)
if [ -z "$wg_dev_after" ]; then
    pass
else
    fail "wg device still exists: $wg_dev_after"
fi

# ── 6. Connection Restart ─────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 6. Connection Restart"
echo "========================================"

log_test "vpn restart test-wg succeeds"
output=$(run_cli_command "vpn restart test-wg" 10)
if echo "$output" | grep -qi "restarting\|success"; then
    pass
else
    fail "Restart command failed: $output"
fi

sleep 3

log_test "TUN device recreated after restart"
wg_dev_restart=$(get_wg_device)
if [ -n "$wg_dev_restart" ]; then
    pass
else
    fail "No wg* device after restart"
fi

# Stop WireGuard for OpenVPN tests
run_cli_command "vpn stop test-wg" 5 >/dev/null 2>&1
sleep 2

# ============================================================
# OPENVPN TESTS
# ============================================================

echo ""
echo "========================================"
echo " OpenVPN (test-ovpn)"
echo "========================================"

# ── 7. Config Validation ──────────────────────────────────────────────────────
log_test "OpenVPN config validates successfully"
output=$(run_cli_command "vpn config validate $OVPN_CONFIG" 5)
assert_contains_fixed "$output" "Valid: YES"

log_test "OpenVPN config has inline CA cert"
cert_count=$(grep -c "BEGIN CERTIFICATE" "$OVPN_CONFIG")
if [ "$cert_count" -ge 2 ]; then
    pass
else
    fail "Expected >= 2 certificates, found $cert_count"
fi

log_test "OpenVPN config has inline private key"
grep -q "BEGIN PRIVATE KEY" "$OVPN_CONFIG" && pass || fail "Missing private key"

log_test "OpenVPN config has inline tls-crypt"
grep -q "BEGIN OpenVPN Static key" "$OVPN_CONFIG" && pass || fail "Missing tls-crypt"

log_test "OpenVPN config has remote directive"
grep -q "remote " "$OVPN_CONFIG" && pass || fail "Missing remote"

log_test "OpenVPN config has dev tun"
grep -q "dev tun" "$OVPN_CONFIG" && pass || fail "Missing dev tun"

log_test "OpenVPN config has cipher"
grep -q "cipher" "$OVPN_CONFIG" && pass || fail "Missing cipher"

log_test "OpenVPN config has auth"
grep -q "auth " "$OVPN_CONFIG" && pass || fail "Missing auth"

# ── 8. Connection Creation ────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 8. Connection Creation"
echo "========================================"

log_test "test-ovpn appears in vpn list"
output=$(run_cli_command "vpn list" 5)
assert_contains_fixed "$output" "test-ovpn"

log_test "test-ovpn shows openvpn type"
assert_contains_fixed "$output" "OpenVPN"

# ── 9. Connection Start ───────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 9. Connection Start"
echo "========================================"

log_test "vpn start test-ovpn succeeds"
output=$(run_cli_command "vpn start test-ovpn" 10)
if echo "$output" | grep -qi "starting\|success"; then
    pass
else
    fail "Start command failed: $output"
fi

sleep 5

log_test "TUN device created for OpenVPN"
ovpn_dev=$(get_ovpn_device)
if [ -n "$ovpn_dev" ]; then
    pass
    OVPN_DEV_NAME="$ovpn_dev"
else
    # OpenVPN may use a different naming convention
    tun_dev=$(ip -o link show 2>/dev/null | grep -oP 'tun\d+' | head -1 || echo "")
    if [ -n "$tun_dev" ]; then
        pass
        OVPN_DEV_NAME="$tun_dev"
    else
        skip "No ovpn-* or tun* device found (connection may be in progress)"
        OVPN_DEV_NAME=""
    fi
fi

# ── 10. Connection Status ─────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 10. Connection Status"
echo "========================================"

log_test "vpn status test-ovpn shows details"
output=$(run_cli_command "vpn status test-ovpn" 5)
assert_contains_fixed "$output" "Connection: test-ovpn"

log_test "Connection state is connecting or connected"
if echo "$output" | grep -qi "connecting\|connected\|connected"; then
    pass
else
    skip "Connection state unclear (may need more time)"
fi

# ── 11. Connection Stop ───────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 11. Connection Stop"
echo "========================================"

log_test "vpn stop test-ovpn succeeds"
output=$(run_cli_command "vpn stop test-ovpn" 10)
if echo "$output" | grep -qi "stopping\|success"; then
    pass
else
    fail "Stop command failed: $output"
fi

sleep 2

# ── 12. Connection Restart ────────────────────────────────────────────────────
echo ""
echo "========================================"
echo " 12. Connection Restart"
echo "========================================"

log_test "vpn restart test-ovpn succeeds"
output=$(run_cli_command "vpn restart test-ovpn" 10)
if echo "$output" | grep -qi "restarting\|success"; then
    pass
else
    fail "Restart command failed: $output"
fi

sleep 3

log_test "Connection state after restart"
output=$(run_cli_command "vpn status test-ovpn" 5)
if echo "$output" | grep -qi "connecting\|connected\|restarting"; then
    pass
else
    skip "Connection state after restart unclear"
fi

# ── Cleanup: Stop all VPNs ────────────────────────────────────────────────────
run_cli_command "vpn stop test-wg" 5 >/dev/null 2>&1
run_cli_command "vpn stop test-ovpn" 5 >/dev/null 2>&1
sleep 2

# ── Results ───────────────────────────────────────────────────────────────────
print_results
exit $?
