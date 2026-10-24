#!/usr/bin/env bash
#
# Master Test Runner for odc-onboard
# Builds, installs, runs all test suites, and performs full cleanup
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/helpers/test_helpers.sh"

# ── Configuration ─────────────────────────────────────────────────────────────
BINARY="${BINARY:-/usr/bin/odc-onboard}"
CONFIG_PATH="${CONFIG_PATH:-/etc/odc-onboard/config.json}"
SERVICE_NAME="odc-onboard"
BUILD_DIR="${PROJECT_DIR}/build"

# ── Colors ────────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# ── Header ────────────────────────────────────────────────────────────────────
echo ""
echo "============================================================"
echo " odc-onboard Master Test Runner"
echo "============================================================"
echo ""
echo "Project: $PROJECT_DIR"
echo "Binary: $BINARY"
echo "Config: $CONFIG_PATH"
echo ""

# ── Prerequisites ─────────────────────────────────────────────────────────────
if ! has_root; then
    echo -e "${RED}ERROR: This test suite requires root privileges${NC}"
    echo "Run with: sudo $0"
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo -e "${RED}ERROR: cmake not found${NC}"
    exit 1
fi

if ! command -v make >/dev/null 2>&1; then
    echo -e "${RED}ERROR: make not found${NC}"
    exit 1
fi

# ── Cleanup Function ──────────────────────────────────────────────────────────
full_cleanup() {
    echo ""
    echo -e "${BLUE}============================================================${NC}"
    echo -e "${BLUE} Full Cleanup${NC}"
    echo -e "${BLUE}============================================================${NC}"
    echo ""

    echo "  Stopping service..."
    systemctl stop "$SERVICE_NAME" 2>/dev/null || true
    sleep 1

    echo "  Disabling service..."
    systemctl disable "$SERVICE_NAME" 2>/dev/null || true

    echo "  Removing service file..."
    rm -f "/lib/systemd/system/${SERVICE_NAME}.service"
    systemctl daemon-reload 2>/dev/null || true

    echo "  Removing binary..."
    rm -f "$BINARY"

    echo "  Removing config directory..."
    rm -rf /etc/odc-onboard

    echo "  Removing runtime directory..."
    rm -rf /run/odc-onboard

    echo "  Removing log directory..."
    rm -rf /var/log/odc-onboard

    echo "  Removing leftover TUN devices..."
    ip link show 2>/dev/null | grep -oP 'wg\d+' | while read -r dev; do
        ip link delete "$dev" 2>/dev/null || true
    done
    ip link show 2>/dev/null | grep -oP 'ovpn-\w+' | while read -r dev; do
        ip link delete "$dev" 2>/dev/null || true
    done

    echo "  Killing orphaned odc-onboard processes..."
    pkill -9 odc-onboard 2>/dev/null || true

    echo ""
    echo -e "${GREEN}Cleanup complete${NC}"
}

# Set trap for cleanup on exit
CLEANUP_ON_EXIT="${CLEANUP_ON_EXIT:-true}"
if [ "$CLEANUP_ON_EXIT" = "true" ]; then
    trap full_cleanup EXIT
fi

# ── Step 1: Build ─────────────────────────────────────────────────────────────
echo -e "${BLUE}============================================================${NC}"
echo -e "${BLUE} Step 1: Build${NC}"
echo -e "${BLUE}============================================================${NC}"
echo ""

echo "  Configuring with cmake..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5
if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed${NC}"
    exit 1
fi

echo "  Building..."
make -j$(nproc) 2>&1 | tail -10
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed${NC}"
    exit 1
fi

if [ ! -x "$BUILD_DIR/odc-onboard" ]; then
    echo -e "${RED}Binary not found after build${NC}"
    exit 1
fi

echo -e "${GREEN}  Build successful${NC}"
echo ""

# ── Step 2: Install ───────────────────────────────────────────────────────────
echo -e "${BLUE}============================================================${NC}"
echo -e "${BLUE} Step 2: Install${NC}"
echo -e "${BLUE}============================================================${NC}"
echo ""

echo "  Installing binary..."
install -m 755 "$BUILD_DIR/odc-onboard" "$BINARY"
if [ ! -x "$BINARY" ]; then
    echo -e "${RED}Binary installation failed${NC}"
    exit 1
fi

echo "  Creating directories..."
mkdir -p /etc/odc-onboard/vpn
mkdir -p /etc/odc-onboard/mavlink/devices
mkdir -p /run/odc-onboard
mkdir -p /var/log/odc-onboard/mavlink

echo "  Deploying test config..."
cp "$SCRIPT_DIR/helpers/test_config.json" "$CONFIG_PATH"
chmod 600 "$CONFIG_PATH"

echo "  Deploying VPN configs..."
cp "$PROJECT_DIR/configs/dev.conf" /etc/odc-onboard/vpn/dev.conf
cp "$PROJECT_DIR/configs/dev.ovpn" /etc/odc-onboard/vpn/dev.ovpn
chmod 600 /etc/odc-onboard/vpn/*

echo "  Installing service file..."
cp "$PROJECT_DIR/odc-onboard.service" "/lib/systemd/system/${SERVICE_NAME}.service"

echo "  Setting service binary path..."
# Ensure service file points to the installed binary
sed -i "s|ExecStart=.*|ExecStart=$BINARY --daemon --config $CONFIG_PATH|" "/lib/systemd/system/${SERVICE_NAME}.service"

systemctl daemon-reload

echo -e "${GREEN}  Installation complete${NC}"
echo ""

# ── Step 3: Run Test Suites ───────────────────────────────────────────────────
echo -e "${BLUE}============================================================${NC}"
echo -e "${BLUE} Step 3: Run Test Suites${NC}"
echo -e "${BLUE}============================================================${NC}"
echo ""

OVERALL_PASS=0
OVERALL_FAIL=0
OVERALL_SKIP=0
OVERALL_TOTAL=0
SUITE_RESULTS=()

run_suite() {
    local suite_name="$1"
    local suite_script="$2"

    echo -e "${BLUE}------------------------------------------------------------${NC}"
    echo -e "${BLUE} Running: $suite_name${NC}"
    echo -e "${BLUE}------------------------------------------------------------${NC}"
    echo ""

    if [ ! -x "$suite_script" ]; then
        chmod +x "$suite_script"
    fi

    # Run the suite and capture output
    local output
    output=$(BINARY="$BINARY" CONFIG_PATH="$CONFIG_PATH" bash "$suite_script" 2>&1)
    local rc=$?

    echo "$output"

    # Extract results from output
    local pass fail skip total
    pass=$(echo "$output" | grep -oP 'PASS: \K\d+' || echo "0")
    fail=$(echo "$output" | grep -oP 'FAIL: \K\d+' || echo "0")
    skip=$(echo "$output" | grep -oP 'SKIP: \K\d+' || echo "0")
    total=$(echo "$output" | grep -oP 'TOTAL: \K\d+' || echo "0")

    OVERALL_PASS=$((OVERALL_PASS + pass))
    OVERALL_FAIL=$((OVERALL_FAIL + fail))
    OVERALL_SKIP=$((OVERALL_SKIP + skip))
    OVERALL_TOTAL=$((OVERALL_TOTAL + total))

    SUITE_RESULTS+=("$suite_name: PASS=$pass FAIL=$fail SKIP=$skip TOTAL=$total")

    echo ""
    if [ $rc -eq 0 ]; then
        echo -e "${GREEN}  $suite_name: PASSED${NC}"
    else
        echo -e "${RED}  $suite_name: FAILED${NC}"
    fi
    echo ""
}

# Export for child scripts
export BINARY
export CONFIG_PATH

# Run suites in order
run_suite "Service Installation" "$SCRIPT_DIR/test_service_install.sh"
run_suite "Service Mode Lifecycle" "$SCRIPT_DIR/test_service_mode.sh"
run_suite "CLI Mode Interaction" "$SCRIPT_DIR/test_cli_mode.sh"
run_suite "MAVLink Hardware" "$SCRIPT_DIR/test_mavlink_hardware.sh"
run_suite "VPN Integration" "$SCRIPT_DIR/test_vpn_integration.sh"
run_suite "VPN Monitor + MAVLink Extender" "$SCRIPT_DIR/test_vpn_monitor_extender.sh"
run_suite "RTSP Compressor Integration"  "$SCRIPT_DIR/test_rtsp_compressor.sh"
run_suite "RTSP User Endpoint Tests"       "$SCRIPT_DIR/test_rtsp_user_e2e.sh"

# ── Step 4: Summary ───────────────────────────────────────────────────────────
echo ""
echo -e "${BLUE}============================================================${NC}"
echo -e "${BLUE} Test Suite Summary${NC}"
echo -e "${BLUE}============================================================${NC}"
echo ""

for result in "${SUITE_RESULTS[@]}"; do
    echo "  $result"
done

echo ""
echo -e "${BLUE}============================================================${NC}"
echo -e "${BLUE} Overall Results${NC}"
echo -e "${BLUE}============================================================${NC}"
echo ""
printf "  ${GREEN}PASS: %d${NC}\n" $OVERALL_PASS
printf "  ${RED}FAIL: %d${NC}\n" $OVERALL_FAIL
printf "  ${YELLOW}SKIP: %d${NC}\n" $OVERALL_SKIP
printf "  TOTAL: %d\n" $OVERALL_TOTAL
echo ""

if [ $OVERALL_FAIL -eq 0 ]; then
    echo -e "${GREEN}All test suites passed!${NC}"
    EXIT_CODE=0
else
    echo -e "${RED}Some test suites failed!${NC}"
    EXIT_CODE=1
fi

echo ""
echo -e "${BLUE}Cleanup will proceed automatically...${NC}"
echo ""

exit $EXIT_CODE
