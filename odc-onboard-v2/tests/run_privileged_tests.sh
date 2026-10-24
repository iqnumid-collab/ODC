#!/usr/bin/env bash
#
# Privileged Test Runner - Run with: sudo ./tests/run_privileged_tests.sh
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/helpers/test_helpers.sh"

BINARY="${BINARY:-${PROJECT_DIR}/build/odc-onboard}"
CONFIG_PATH="${CONFIG_PATH:-/etc/odc-onboard/config.json}"
SERVICE_NAME="odc-onboard"

# ── Prerequisites ─────────────────────────────────────────────────────────────
if ! has_root; then
    echo "ERROR: Run with sudo: sudo $0"
    exit 1
fi

if [ ! -x "$BINARY" ]; then
    echo "ERROR: Binary not found: $BINARY"
    echo "Run: cd $PROJECT_DIR/build && make"
    exit 1
fi

echo "============================================================"
echo " Privileged Test Runner"
echo "============================================================"
echo ""
echo "Binary: $BINARY"
echo "Config: $CONFIG_PATH"
echo ""

# ── Setup ─────────────────────────────────────────────────────────────────────
echo "Setting up test environment..."
mkdir -p /etc/odc-onboard/vpn /etc/odc-onboard/mavlink/devices
mkdir -p /run/odc-onboard
mkdir -p /var/log/odc-onboard/mavlink
cp "$SCRIPT_DIR/helpers/test_config.json" "$CONFIG_PATH"
chmod 600 "$CONFIG_PATH"
cp "$PROJECT_DIR/configs/dev.conf" /etc/odc-onboard/vpn/dev.conf
cp "$PROJECT_DIR/configs/dev.ovpn" /etc/odc-onboard/vpn/dev.ovpn
chmod 600 /etc/odc-onboard/vpn/*
cp "$PROJECT_DIR/odc-onboard.service" "/lib/systemd/system/${SERVICE_NAME}.service"
sed -i "s|ExecStart=.*|ExecStart=$BINARY --daemon --config $CONFIG_PATH|" "/lib/systemd/system/${SERVICE_NAME}.service"
systemctl daemon-reload

export BINARY CONFIG_PATH

# ── Run suites ────────────────────────────────────────────────────────────────
OVERALL_PASS=0; OVERALL_FAIL=0; OVERALL_SKIP=0; OVERALL_TOTAL=0

run_suite() {
    local name="$1" script="$2"
    echo ""
    echo "============================================================"
    echo " $name"
    echo "============================================================"
    echo ""

    local output rc pass fail skip total
    output=$(bash "$script" 2>&1)
    rc=$?
    echo "$output"

    pass=$(echo "$output" | grep -oP 'PASS: \K\d+' || echo "0")
    fail=$(echo "$output" | grep -oP 'FAIL: \K\d+' || echo "0")
    skip=$(echo "$output" | grep -oP 'SKIP: \K\d+' || echo "0")
    total=$(echo "$output" | grep -oP 'TOTAL: \K\d+' || echo "0")

    OVERALL_PASS=$((OVERALL_PASS + pass))
    OVERALL_FAIL=$((OVERALL_FAIL + fail))
    OVERALL_SKIP=$((OVERALL_SKIP + skip))
    OVERALL_TOTAL=$((OVERALL_TOTAL + total))

    echo ""
    if [ $rc -eq 0 ]; then
        echo "  RESULT: ${name} PASSED"
    else
        echo "  RESULT: ${name} FAILED"
    fi
}

run_suite "Service Installation" "$SCRIPT_DIR/test_service_install.sh"
run_suite "Service Mode Lifecycle" "$SCRIPT_DIR/test_service_mode.sh"
run_suite "CLI Mode Interaction" "$SCRIPT_DIR/test_cli_mode.sh"
run_suite "MAVLink Hardware" "$SCRIPT_DIR/test_mavlink_hardware.sh"
run_suite "VPN Integration" "$SCRIPT_DIR/test_vpn_integration.sh"

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "============================================================"
echo " Overall Results"
echo "============================================================"
echo ""
printf "  PASS: %d\n" $OVERALL_PASS
printf "  FAIL: %d\n" $OVERALL_FAIL
printf "  SKIP: %d\n" $OVERALL_SKIP
printf "  TOTAL: %d\n" $OVERALL_TOTAL
echo ""

# ── Cleanup ───────────────────────────────────────────────────────────────────
echo "Cleaning up..."
systemctl stop "$SERVICE_NAME" 2>/dev/null || true
systemctl disable "$SERVICE_NAME" 2>/dev/null || true
rm -f "/lib/systemd/system/${SERVICE_NAME}.service"
systemctl daemon-reload 2>/dev/null || true
rm -f "$BINARY" 2>/dev/null || true
rm -rf /etc/odc-onboard /run/odc-onboard /var/log/odc-onboard
ip link show 2>/dev/null | grep -oP 'wg\d+' | while read -r dev; do ip link delete "$dev" 2>/dev/null; done
pkill -9 odc-onboard 2>/dev/null || true
echo "Cleanup complete."

if [ $OVERALL_FAIL -eq 0 ]; then
    echo "All test suites passed!"
    exit 0
else
    echo "Some test suites failed!"
    exit 1
fi
