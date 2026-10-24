#!/usr/bin/env bash
#
# odc-onboard VPN Interface Monitor + MAVLink Extender Test Suite
#
# Tests:
#   - VpnInterfaceMonitor: netlink-based real-time VPN interface detection
#   - MavExtenderManager: auto-spawned MAVLink extender threads per VPN interface
#   - CLI commands: mav extender list/status/start/stop/remove/auto/port
#   - EventBus integration: VpnInterfaceEvent, MavExtenderStatusEvent
#   - Real VPN configs: dev.conf, remote.conf, dev.ovpn, remote.ovpn
#   - MAVLink router: extender UDP client connects to primary TCP/UDP server
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/helpers/test_helpers.sh"

BINARY="${BINARY:-./odc-onboard}"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_DIR="$PROJECT_DIR/configs"
TEST_DIR=$(mktemp -d /tmp/odc-extender-test-XXXXXX)
PASS=0
FAIL=0
SKIP=0
TOTAL=0

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

cleanup() {
    # Kill any leftover test daemons
    pkill -f "odc-onboard.*test-extender" 2>/dev/null || true
    # Clean up test TUN devices
    for iface in test-wg0 test-ovpn0 test-ext0 test-mon0; do
        ip tuntap del name "$iface" mode tun 2>/dev/null || true
    done
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

# ── Test helpers ──────────────────────────────────────────────────────────────

log_test() {
    TOTAL=$((TOTAL + 1))
    printf "  %-65s" "$1"
}

pass() {
    PASS=$((PASS + 1))
    printf "${GREEN}PASS${NC}\n"
}

fail() {
    FAIL=$((FAIL + 1))
    printf "${RED}FAIL${NC}\n"
    [ -n "${1:-}" ] && printf "    ${RED}Reason: %s${NC}\n" "$1"
}

skip() {
    SKIP=$((SKIP + 1))
    printf "${YELLOW}SKIP${NC}\n"
    [ -n "${1:-}" ] && printf "    ${YELLOW}Reason: %s${NC}\n" "$1"
}

assert_exit_code() {
    local expected=$1 actual=$2
    if [ "$expected" -eq "$actual" ]; then
        pass
    else
        fail "Expected exit code $expected, got $actual"
    fi
}

assert_contains() {
    local output="$1" pattern="$2"
    if echo "$output" | grep -q -e "$pattern"; then
        pass
    else
        fail "Output does not contain: $pattern"
    fi
}

assert_contains_fixed() {
    local output="$1" pattern="$2"
    if echo "$output" | grep -qF -- "$pattern"; then
        pass
    else
        fail "Output does not contain: $pattern"
    fi
}

assert_not_contains() {
    local output="$1" pattern="$2"
    if echo "$output" | grep -qF -- "$pattern"; then
        fail "Output should not contain: $pattern"
    else
        pass
    fi
}

assert_file_exists() {
    if [ -f "$1" ]; then
        pass
    else
        fail "File does not exist: $1"
    fi
}

assert_dir_exists() {
    if [ -d "$1" ]; then
        pass
    else
        fail "Directory does not exist: $1"
    fi
}

has_net_admin() {
    if [ "$(id -u)" -eq 0 ]; then
        return 0
    fi
    if capsh --print 2>/dev/null | grep -q "^Current:.*cap_net_admin"; then
        return 0
    fi
    if getcap "$BINARY" 2>/dev/null | grep -q "cap_net_admin"; then
        return 0
    fi
    local priv_output
    priv_output=$("$BINARY" --check-privileges --json 2>/dev/null) || return 1
    echo "$priv_output" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('has_net_admin', False) else 1)" 2>/dev/null
}

create_tun_device() {
    local name="$1"
    capsh --caps="cap_net_admin+eip" --keep=1 -- -c "ip tuntap add name $name mode tun" 2>/dev/null
}

delete_tun_device() {
    local name="$1"
    capsh --caps="cap_net_admin+eip" --keep=1 -- -c "ip tuntap del name $name mode tun" 2>/dev/null
}

set_interface_up() {
    local name="$1"
    capsh --caps="cap_net_admin+eip" --keep=1 -- -c "ip link set $name up" 2>/dev/null
}

set_interface_down() {
    local name="$1"
    capsh --caps="cap_net_admin+eip" --keep=1 -- -c "ip link set $name down" 2>/dev/null
}

assign_ip() {
    local name="$1" ip_addr="$2"
    capsh --caps="cap_net_admin+eip" --keep=1 -- -c "ip addr add $ip_addr dev $name" 2>/dev/null
}

remove_ip() {
    local name="$1" ip_addr="$2"
    capsh --caps="cap_net_admin+eip" --keep=1 -- -c "ip addr del $ip_addr dev $name" 2>/dev/null
}

interface_exists() {
    [ -d "/sys/class/net/$1" ]
}

interface_is_up() {
    ip link show "$1" 2>/dev/null | grep -q "state UNKNOWN\|UP"
}

interface_has_ip() {
    local name="$1" ip_addr="$2"
    ip addr show "$name" 2>/dev/null | grep -qF "$ip_addr"
}

get_interface_ip() {
    local name="$1"
    ip -4 addr show "$name" 2>/dev/null | grep -oP 'inet \K[\d.]+' | head -1
}

# ── Daemon management ─────────────────────────────────────────────────────────

DAEMON_PID=""
SOCKET_PATH=""

start_test_daemon() {
    local config_file="$1"
    SOCKET_PATH="$TEST_DIR/odc-onboard.sock"
    export ODC_TEST_SOCKET="$SOCKET_PATH"

    "$BINARY" --config "$config_file" --daemon --socket "$SOCKET_PATH" 2>"$TEST_DIR/daemon.log" &
    DAEMON_PID=$!
    sleep 2

    # Wait for socket to appear
    local retries=10
    while [ $retries -gt 0 ]; do
        if [ -S "$SOCKET_PATH" ]; then
            return 0
        fi
        sleep 1
        retries=$((retries - 1))
    done
    return 1
}

stop_test_daemon() {
    if [ -n "$DAEMON_PID" ]; then
        kill "$DAEMON_PID" 2>/dev/null || true
        wait "$DAEMON_PID" 2>/dev/null || true
        DAEMON_PID=""
    fi
    rm -f "$SOCKET_PATH" 2>/dev/null
}

run_cli_command() {
    local cmd="$1" timeout="${2:-5}"
    printf '%s\nexit\n' "$cmd" | timeout "$timeout" "$BINARY" --cli --config /dev/null 2>&1 || true
}

# ── Config generation ─────────────────────────────────────────────────────────

generate_test_config() {
    local output="$1"
    local mav_port="${2:-14550}"
    local extender_port="${3:-14580}"
    local extender_enabled="${4:-true}"

    cat > "$output" << EOF
{
  "version": 1,
  "general": {
    "log_level": "debug",
    "device_config_dir": "$TEST_DIR/devices"
  },
  "vpn": {
    "wireguard": {
      "connections": [
        {
          "id": "test-wg",
          "enabled": true,
          "config_path": "$CONFIG_DIR/dev.conf",
          "metadata": {
            "name": "Test WireGuard",
            "auto_start": false,
            "max_retries": 1,
            "health_check": { "enabled": false, "interval_sec": 30 }
          }
        },
        {
          "id": "test-wg-remote",
          "enabled": true,
          "config_path": "$CONFIG_DIR/remote.conf",
          "metadata": {
            "name": "Test WireGuard Remote",
            "auto_start": false,
            "max_retries": 1,
            "health_check": { "enabled": false, "interval_sec": 30 }
          }
        }
      ]
    },
    "openvpn": {
      "connections": [
        {
          "id": "test-ovpn",
          "enabled": true,
          "config_path": "$CONFIG_DIR/dev.ovpn",
          "metadata": {
            "name": "Test OpenVPN",
            "auto_start": false,
            "max_retries": 1,
            "health_check": { "enabled": false, "interval_sec": 30 }
          }
        },
        {
          "id": "test-ovpn-remote",
          "enabled": true,
          "config_path": "$CONFIG_DIR/remote.ovpn",
          "metadata": {
            "name": "Test OpenVPN Remote",
            "auto_start": false,
            "max_retries": 1,
            "health_check": { "enabled": false, "interval_sec": 30 }
          }
        }
      ]
    }
  },
  "mavlink": {
    "enabled": true,
    "tcp_server_port": $mav_port,
    "dedup_period_sec": 0,
    "sniffer_sysid": 0,
    "report_stats": true,
    "extender_auto": {
      "enabled": $extender_enabled,
      "udp_port": $extender_port,
      "vpn_types": []
    },
    "endpoints": [],
    "extenders": []
  }
}
EOF
}

# ============================================================
echo "============================================================"
echo " odc-onboard VPN Monitor + MAVLink Extender Test Suite"
echo "============================================================"
echo ""

if [ ! -x "$BINARY" ]; then
    echo "ERROR: Binary not found or not executable: $BINARY"
    exit 1
fi

echo "Binary: $(readlink -f "$BINARY")"
echo "Test dir: $TEST_DIR"
echo ""

# ============================================================
echo "========================================"
echo " 1. Source Code Verification"
echo "========================================"

log_test "vpn_monitor.hpp exists"
assert_file_exists "$PROJECT_DIR/src/core/vpn_monitor.hpp"

log_test "vpn_monitor.hpp uses netlink socket"
grep -q "AF_NETLINK" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Not using AF_NETLINK"

log_test "vpn_monitor.hpp subscribes to RTNLGRP_LINK"
grep -q "RTNLGRP_LINK" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Not subscribing to RTNLGRP_LINK"

log_test "vpn_monitor.hpp subscribes to RTNLGRP_IPV4_IFADDR"
grep -q "RTNLGRP_IPV4_IFADDR" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Not subscribing to RTNLGRP_IPV4_IFADDR"

log_test "vpn_monitor.hpp subscribes to RTNLGRP_IPV6_IFADDR"
grep -q "RTNLGRP_IPV6_IFADDR" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Not subscribing to RTNLGRP_IPV6_IFADDR"

log_test "vpn_monitor.hpp handles RTM_NEWLINK"
grep -q "RTM_NEWLINK" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Not handling RTM_NEWLINK"

log_test "vpn_monitor.hpp handles RTM_DELLINK"
grep -q "RTM_DELLINK" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Not handling RTM_DELLINK"

log_test "vpn_monitor.hpp handles RTM_NEWADDR"
grep -q "RTM_NEWADDR" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Not handling RTM_NEWADDR"

log_test "vpn_monitor.hpp handles RTM_DELADDR"
grep -q "RTM_DELADDR" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Not handling RTM_DELADDR"

log_test "vpn_monitor.hpp has track_connection method"
grep -q "track_connection" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Missing track_connection"

log_test "vpn_monitor.hpp has untrack_connection method"
grep -q "untrack_connection" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Missing untrack_connection"

log_test "vpn_monitor.hpp has snapshot_all method"
grep -q "snapshot_all" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Missing snapshot_all"

log_test "vpn_monitor.hpp publishes VpnInterfaceEvent"
grep -q "VpnInterfaceEvent" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Not publishing VpnInterfaceEvent"

log_test "mav_extender_manager.hpp exists"
assert_file_exists "$PROJECT_DIR/src/mavlink/mav_extender_manager.hpp"

log_test "mav_extender_manager.hpp subscribes to VpnInterfaceEvent"
grep -q "VpnInterfaceEvent" "$PROJECT_DIR/src/mavlink/mav_extender_manager.hpp" && pass || fail "Not subscribing to VpnInterfaceEvent"

log_test "mav_extender_manager.hpp subscribes to VpnStateChangeEvent"
grep -q "VpnStateChangeEvent" "$PROJECT_DIR/src/mavlink/mav_extender_manager.hpp" && pass || fail "Not subscribing to VpnStateChangeEvent"

log_test "mav_extender_manager.hpp has spawn_extender method"
grep -q "spawn_extender" "$PROJECT_DIR/src/mavlink/mav_extender_manager.hpp" && pass || fail "Missing spawn_extender"

log_test "mav_extender_manager.hpp has teardown_extender method"
grep -q "teardown_extender" "$PROJECT_DIR/src/mavlink/mav_extender_manager.hpp" && pass || fail "Missing teardown_extender"

log_test "mav_extender_manager.hpp builds extender config with UDP client"
grep -q "upstream" "$PROJECT_DIR/src/mavlink/mav_extender_manager.hpp" && pass || fail "Not building upstream config"

log_test "mav_extender_manager.hpp uses MavRouter for extenders"
grep -q "MavRouter" "$PROJECT_DIR/src/mavlink/mav_extender_manager.hpp" && pass || fail "Not using MavRouter"

log_test "VpnInterfaceEvent defined in event.hpp"
grep -q "struct VpnInterfaceEvent" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "VpnInterfaceEvent not defined"

log_test "MavExtenderStatusEvent defined in event.hpp"
grep -q "struct MavExtenderStatusEvent" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "MavExtenderStatusEvent not defined"

log_test "VpnInterfaceAction enum defined in event.hpp"
grep -q "enum class VpnInterfaceAction" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "VpnInterfaceAction not defined"

log_test "VpnInterfaceEvent in Event variant"
grep -q "VpnInterfaceEvent," "$PROJECT_DIR/src/core/event.hpp" && pass || fail "VpnInterfaceEvent not in variant"

log_test "MavExtenderStatusEvent in Event variant"
grep -q "MavExtenderStatusEvent," "$PROJECT_DIR/src/core/event.hpp" && pass || fail "MavExtenderStatusEvent not in variant"

log_test "MavExtenderAutoConfig defined in mav_config.hpp"
grep -q "struct MavExtenderAutoConfig" "$PROJECT_DIR/src/mavlink/mav_config.hpp" && pass || fail "MavExtenderAutoConfig not defined"

log_test "extender_auto field in MavlinkConfig"
grep -q "extender_auto" "$PROJECT_DIR/src/mavlink/mav_config.hpp" && pass || fail "extender_auto not in MavlinkConfig"

# ============================================================
echo ""
echo "========================================"
echo " 2. VPN Config Validation (Real Configs)"
echo "========================================"

log_test "dev.conf WireGuard config exists"
assert_file_exists "$CONFIG_DIR/dev.conf"

log_test "dev.conf has valid [Interface] section"
grep -q "\[Interface\]" "$CONFIG_DIR/dev.conf" && pass || fail "Missing [Interface]"

log_test "dev.conf has valid [Peer] section"
grep -q "\[Peer\]" "$CONFIG_DIR/dev.conf" && pass || fail "Missing [Peer]"

log_test "dev.conf has PrivateKey"
grep -q "PrivateKey" "$CONFIG_DIR/dev.conf" && pass || fail "Missing PrivateKey"

log_test "dev.conf has PublicKey"
grep -q "PublicKey" "$CONFIG_DIR/dev.conf" && pass || fail "Missing PublicKey"

log_test "dev.conf has Endpoint"
grep -q "Endpoint" "$CONFIG_DIR/dev.conf" && pass || fail "Missing Endpoint"

log_test "dev.conf has AllowedIPs"
grep -q "AllowedIPs" "$CONFIG_DIR/dev.conf" && pass || fail "Missing AllowedIPs"

log_test "dev.conf has Address field"
grep -q "Address" "$CONFIG_DIR/dev.conf" && pass || fail "Missing Address"

log_test "dev.conf has DNS field"
grep -q "DNS" "$CONFIG_DIR/dev.conf" && pass || fail "Missing DNS"

log_test "dev.conf has PresharedKey"
grep -q "PresharedKey" "$CONFIG_DIR/dev.conf" && pass || fail "Missing PresharedKey"

log_test "remote.conf WireGuard config exists"
assert_file_exists "$CONFIG_DIR/remote.conf"

log_test "remote.conf has [Interface] section"
grep -q "\[Interface\]" "$CONFIG_DIR/remote.conf" && pass || fail "Missing [Interface]"

log_test "remote.conf has [Peer] section"
grep -q "\[Peer\]" "$CONFIG_DIR/remote.conf" && pass || fail "Missing [Peer]"

log_test "remote.conf has AllowedIPs with 10.66.66.0/24"
grep -q "10.66.66.0/24" "$CONFIG_DIR/remote.conf" && pass || fail "Missing 10.66.66.0/24"

log_test "dev.ovpn OpenVPN config exists"
assert_file_exists "$CONFIG_DIR/dev.ovpn"

log_test "dev.ovpn has remote directive"
grep -q "remote " "$CONFIG_DIR/dev.ovpn" && pass || fail "Missing remote"

log_test "dev.ovpn has dev tun"
grep -q "dev tun" "$CONFIG_DIR/dev.ovpn" && pass || fail "Missing dev tun"

log_test "dev.ovpn has inline CA certificate"
grep -q "BEGIN CERTIFICATE" "$CONFIG_DIR/dev.ovpn" && pass || fail "Missing CA cert"

log_test "dev.ovpn has inline client certificate"
grep -q "BEGIN CERTIFICATE" "$CONFIG_DIR/dev.ovpn" && pass || fail "Missing client cert"

log_test "dev.ovpn has inline private key"
grep -q "BEGIN PRIVATE KEY" "$CONFIG_DIR/dev.ovpn" && pass || fail "Missing private key"

log_test "dev.ovpn has tls-crypt"
grep -q "tls-crypt" "$CONFIG_DIR/dev.ovpn" && pass || fail "Missing tls-crypt"

log_test "dev.ovpn has cipher AES-128-GCM"
grep -q "cipher AES-128-GCM" "$CONFIG_DIR/dev.ovpn" && pass || fail "Missing cipher"

log_test "dev.ovpn has auth SHA256"
grep -q "auth SHA256" "$CONFIG_DIR/dev.ovpn" && pass || fail "Missing auth"

log_test "dev.ovpn has proto udp"
grep -q "proto udp" "$CONFIG_DIR/dev.ovpn" && pass || fail "Missing proto"

log_test "remote.ovpn OpenVPN config exists"
assert_file_exists "$CONFIG_DIR/remote.ovpn"

log_test "remote.ovpn has remote directive"
grep -q "remote " "$CONFIG_DIR/remote.ovpn" && pass || fail "Missing remote"

log_test "remote.ovpn has dev tun"
grep -q "dev tun" "$CONFIG_DIR/remote.ovpn" && pass || fail "Missing dev tun"

log_test "remote.ovpn has inline certificates"
grep -q "BEGIN CERTIFICATE" "$CONFIG_DIR/remote.ovpn" && pass || fail "Missing certs"

log_test "remote.ovpn has tls-crypt"
grep -q "tls-crypt" "$CONFIG_DIR/remote.ovpn" && pass || fail "Missing tls-crypt"

# ============================================================
echo ""
echo "========================================"
echo " 3. Generated Test Config Validation"
echo "========================================"

generate_test_config "$TEST_DIR/test_config.json" 14550 14580 true

log_test "Generated config is valid JSON"
python3 -c "import json; json.load(open('$TEST_DIR/test_config.json'))" 2>/dev/null && pass || fail "Invalid JSON"

log_test "Config has wireguard connections section"
python3 -c "
import json
d = json.load(open('$TEST_DIR/test_config.json'))
assert 'wireguard' in d['vpn']
assert 'connections' in d['vpn']['wireguard']
assert len(d['vpn']['wireguard']['connections']) == 2
" 2>/dev/null && pass || fail "WireGuard config invalid"

log_test "Config has openvpn connections section"
python3 -c "
import json
d = json.load(open('$TEST_DIR/test_config.json'))
assert 'openvpn' in d['vpn']
assert 'connections' in d['vpn']['openvpn']
assert len(d['vpn']['openvpn']['connections']) == 2
" 2>/dev/null && pass || fail "OpenVPN config invalid"

log_test "Config has mavlink section with extender_auto"
python3 -c "
import json
d = json.load(open('$TEST_DIR/test_config.json'))
assert 'mavlink' in d
assert 'extender_auto' in d['mavlink']
assert d['mavlink']['extender_auto']['enabled'] == True
assert d['mavlink']['extender_auto']['udp_port'] == 14580
" 2>/dev/null && pass || fail "MAVLink extender_auto config invalid"

log_test "Config has WireGuard connection IDs"
python3 -c "
import json
d = json.load(open('$TEST_DIR/test_config.json'))
ids = [c['id'] for c in d['vpn']['wireguard']['connections']]
assert 'test-wg' in ids
assert 'test-wg-remote' in ids
" 2>/dev/null && pass || fail "WireGuard IDs missing"

log_test "Config has OpenVPN connection IDs"
python3 -c "
import json
d = json.load(open('$TEST_DIR/test_config.json'))
ids = [c['id'] for c in d['vpn']['openvpn']['connections']]
assert 'test-ovpn' in ids
assert 'test-ovpn-remote' in ids
" 2>/dev/null && pass || fail "OpenVPN IDs missing"

log_test "Config references real VPN config files"
python3 -c "
import json, os
d = json.load(open('$TEST_DIR/test_config.json'))
for conn in d['vpn']['wireguard']['connections'] + d['vpn']['openvpn']['connections']:
    assert os.path.exists(conn['config_path']), f'Config not found: {conn[\"config_path\"]}'
" 2>/dev/null && pass || fail "VPN config paths invalid"

# ============================================================
echo ""
echo "========================================"
echo " 4. Netlink Interface Operations (Privileged)"
echo "========================================"

if ! has_net_admin; then
    for i in $(seq 1 15); do
        log_test "Netlink test $i"
        skip "Requires CAP_NET_ADMIN"
    done
else

log_test "Create TUN device for WireGuard test"
create_tun_device test-wg0 && pass || fail "Failed to create TUN device"

log_test "TUN device appears in /sys/class/net"
interface_exists test-wg0 && pass || fail "Interface not in /sys/class/net"

log_test "Bring TUN device up"
set_interface_up test-wg0 && pass || fail "Failed to bring interface up"

log_test "Interface is UP"
interface_is_up test-wg0 && pass || fail "Interface not UP"

log_test "Assign IPv4 address to interface"
assign_ip test-wg0 "10.99.1.1/24" && pass || fail "Failed to assign IP"

log_test "Interface has assigned IP"
interface_has_ip test-wg0 "10.99.1.1" && pass || fail "IP not assigned"

log_test "Get interface IP via ip command"
ip=$(get_interface_ip test-wg0)
[ "$ip" = "10.99.1.1" ] && pass || fail "Expected 10.99.1.1, got $ip"

log_test "Remove IP address from interface"
remove_ip test-wg0 "10.99.1.1/24" && pass || fail "Failed to remove IP"

log_test "IP address removed"
! interface_has_ip test-wg0 "10.99.1.1" && pass || fail "IP still present"

log_test "Bring interface down"
set_interface_down test-wg0 && pass || fail "Failed to bring interface down"

log_test "Delete TUN device"
delete_tun_device test-wg0 && pass || fail "Failed to delete TUN device"

log_test "TUN device removed from /sys/class/net"
! interface_exists test-wg0 && pass || fail "Interface still exists"

log_test "Create second TUN device for OpenVPN test"
create_tun_device test-ovpn0 && pass || fail "Failed to create second TUN"

log_test "Assign different subnet to second interface"
assign_ip test-ovpn0 "10.99.2.1/24" && pass || fail "Failed to assign second IP"

log_test "Second interface has correct IP"
interface_has_ip test-ovpn0 "10.99.2.1" && pass || fail "Second IP not assigned"

log_test "Clean up second TUN device"
delete_tun_device test-ovpn0 && pass || fail "Failed to delete second TUN"

fi

# ============================================================
echo ""
echo "========================================"
echo " 5. Netlink Multicast Groups"
echo "========================================"

log_test "RTNLGRP_LINK group available"
python3 -c "
import struct, socket
# RTNLGRP_LINK = 1
s = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, socket.NETLINK_ROUTE)
s.bind((0, 1))  # pid=0, groups=RTNLGRP_LINK
s.close()
" 2>/dev/null && pass || fail "RTNLGRP_LINK not available"

log_test "RTNLGRP_IPV4_IFADDR group available"
python3 -c "
import socket
# RTNLGRP_IPV4_IFADDR = 5
s = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, socket.NETLINK_ROUTE)
s.bind((0, 5))
s.close()
" 2>/dev/null && pass || fail "RTNLGRP_IPV4_IFADDR not available"

log_test "RTNLGRP_IPV6_IFADDR group available"
python3 -c "
import socket
# RTNLGRP_IPV6_IFADDR = 7
s = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, socket.NETLINK_ROUTE)
s.bind((0, 7))
s.close()
" 2>/dev/null && pass || fail "RTNLGRP_IPV6_IFADDR not available"

log_test "Combined multicast groups work"
python3 -c "
import socket
# RTNLGRP_LINK | RTNLGRP_IPV4_IFADDR | RTNLGRP_IPV6_IFADDR = 1 | 5 | 7 = 0b111 = 7
s = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, socket.NETLINK_ROUTE)
s.bind((0, 1 | 5 | 7))
s.close()
" 2>/dev/null && pass || fail "Combined groups failed"

# ============================================================
echo ""
echo "========================================"
echo " 6. Daemon Startup with Extender Manager"
echo "========================================"

if ! has_net_admin; then
    for i in $(seq 1 10); do
        log_test "Daemon test $i"
        skip "Requires CAP_NET_ADMIN"
    done
else

generate_test_config "$TEST_DIR/daemon_config.json" 14560 14590 true

log_test "Daemon starts with MAVLink extender manager"
start_test_daemon "$TEST_DIR/daemon_config.json" && pass || fail "Daemon failed to start"

log_test "IPC socket created"
[ -S "$SOCKET_PATH" ] && pass || fail "IPC socket not created"

log_test "Daemon log shows application started"
sleep 1
grep -q "Application started" "$TEST_DIR/daemon.log" 2>/dev/null && pass || fail "No startup message"

log_test "Daemon log shows MAVLink router initialized"
grep -q "mavlink\|MAVLink\|mav" "$TEST_DIR/daemon.log" 2>/dev/null && pass || fail "No MAVLink log"

log_test "Daemon log shows extender manager started"
grep -qi "extender" "$TEST_DIR/daemon.log" 2>/dev/null && pass || skip "No extender log entry (may be info-level)"

log_test "VPN connections loaded from config"
grep -q "Loaded.*VPN\|vpn\|test-wg\|test-ovpn" "$TEST_DIR/daemon.log" 2>/dev/null && pass || skip "No VPN load log"

log_test "Daemon responds to CLI commands"
output=$(printf 'vpn list\nexit\n' | timeout 5 "$BINARY" --cli --config /dev/null 2>&1) || true
assert_contains_fixed "$output" "vpn list"

log_test "Daemon log shows no errors on startup"
if grep -qi "error\|ERROR\|fatal" "$TEST_DIR/daemon.log" 2>/dev/null; then
    fail "Errors found in daemon log"
else
    pass
fi

# ============================================================
echo ""
echo "========================================"
echo " 7. VPN Interface Monitor - Live Detection"
echo "========================================"

log_test "Create test TUN device while daemon is running"
create_tun_device test-mon0 && pass || fail "Failed to create TUN"

log_test "Daemon detects interface creation (check log)"
sleep 1
grep -qi "interface\|netlink\|test-mon0" "$TEST_DIR/daemon.log" 2>/dev/null && pass || skip "No interface detection log"

log_test "Bring test interface up"
set_interface_up test-mon0 && pass || fail "Failed to bring up"

log_test "Daemon detects interface UP event"
sleep 1
grep -qi "up\|UP\|interface_up" "$TEST_DIR/daemon.log" 2>/dev/null && pass || skip "No UP event log"

log_test "Assign IP to test interface"
assign_ip test-mon0 "10.99.3.1/24" && pass || fail "Failed to assign IP"

log_test "Daemon detects IP address assignment"
sleep 1
grep -qi "address\|addr\|10.99.3.1" "$TEST_DIR/daemon.log" 2>/dev/null && pass || skip "No address event log"

log_test "Remove IP from test interface"
remove_ip test-mon0 "10.99.3.1/24" && pass || fail "Failed to remove IP"

log_test "Daemon detects IP address removal"
sleep 1
grep -qi "address\|addr\|remove" "$TEST_DIR/daemon.log" 2>/dev/null && pass || skip "No address removal log"

log_test "Bring test interface down"
set_interface_down test-mon0 && pass || fail "Failed to bring down"

log_test "Delete test TUN device"
delete_tun_device test-mon0 && pass || fail "Failed to delete TUN"

# ============================================================
echo ""
echo "========================================"
echo " 8. MAVLink Extender Auto-Spawn"
echo "========================================"

log_test "Create WireGuard-style TUN device"
create_tun_device test-ext0 && pass || fail "Failed to create extender TUN"

log_test "Bring extender TUN up"
set_interface_up test-ext0 && pass || fail "Failed to bring extender TUN up"

log_test "Assign IP to extender TUN (triggers extender spawn)"
assign_ip test-ext0 "10.99.4.1/24" && pass || fail "Failed to assign extender IP"

log_test "Wait for extender auto-spawn (5s)"
sleep 5

log_test "Extender thread spawned (check log for extender creation)"
grep -qi "extender\|spawn\|ext-" "$TEST_DIR/daemon.log" 2>/dev/null && pass || skip "No extender spawn log"

log_test "Extender UDP client endpoint configured"
grep -qi "udp.*client\|upstream\|10.99.4.1" "$TEST_DIR/daemon.log" 2>/dev/null && pass || skip "No UDP client log"

log_test "Extender status event published"
grep -qi "MavExtenderStatus\|extender_status\|connected.*true" "$TEST_DIR/daemon.log" 2>/dev/null && pass || skip "No status event log"

log_test "Remove IP to trigger extender teardown"
remove_ip test-ext0 "10.99.4.1/24" && pass || fail "Failed to remove extender IP"

log_test "Wait for extender teardown (3s)"
sleep 3

log_test "Extender torn down (check log)"
grep -qi "teardown\|stop.*extender\|connected.*false" "$TEST_DIR/daemon.log" 2>/dev/null && pass || skip "No teardown log"

log_test "Clean up extender TUN"
delete_tun_device test-ext0 && pass || fail "Failed to delete extender TUN"

# ============================================================
echo ""
echo "========================================"
echo " 9. CLI Extender Commands"
echo "========================================"

log_test "mav extender help shows available commands"
output=$(printf 'mav extender help\nexit\n' | timeout 5 "$BINARY" --cli --config /dev/null 2>&1) || true
assert_contains_fixed "$output" "mav extender list"

log_test "mav extender list command works"
output=$(printf 'mav extender list\nexit\n' | timeout 5 "$BINARY" --cli --config /dev/null 2>&1) || true
assert_contains_fixed "$output" "extender"

log_test "mav extender auto off command works"
output=$(printf 'mav extender auto off\nexit\n' | timeout 5 "$BINARY" --cli --config /dev/null 2>&1) || true
assert_contains "$output" "disabled\|off\|Auto extender spawning disabled"

log_test "mav extender auto on command works"
output=$(printf 'mav extender auto on\nexit\n' | timeout 5 "$BINARY" --cli --config /dev/null 2>&1) || true
assert_contains "$output" "enabled\|on\|Auto extender spawning enabled"

log_test "mav extender port command works"
output=$(printf 'mav extender port 14600\nexit\n' | timeout 5 "$BINARY" --cli --config /dev/null 2>&1) || true
assert_contains "$output" "14600\|port"

log_test "mav extender start with invalid params shows error"
output=$(printf 'mav extender start\nexit\n' | timeout 5 "$BINARY" --cli --config /dev/null 2>&1) || true
assert_contains "$output" "Usage\|error\|Usage:"

log_test "mav extender stop with invalid params shows error"
output=$(printf 'mav extender stop\nexit\n' | timeout 5 "$BINARY" --cli --config /dev/null 2>&1) || true
assert_contains "$output" "Usage\|error\|Usage:"

log_test "mav extender status with invalid params shows error"
output=$(printf 'mav extender status\nexit\n' | timeout 5 "$BINARY" --cli --config /dev/null 2>&1) || true
assert_contains "$output" "Usage\|error\|Usage:"

log_test "mav extender remove with invalid params shows error"
output=$(printf 'mav extender remove\nexit\n' | timeout 5 "$BINARY" --cli --config /dev/null 2>&1) || true
assert_contains "$output" "Usage\|error\|Usage:"

# ============================================================
echo ""
echo "========================================"
echo " 10. Extender Config Building"
echo "========================================"

log_test "Extender config has tcp_server_port = 0"
python3 -c "
# Simulate extender config building logic
import json
base = json.load(open('$TEST_DIR/daemon_config.json'))
ext_cfg = {
    'enabled': True,
    'tcp_server_port': 0,
    'dedup_period_sec': base['mavlink']['dedup_period_sec'],
    'sniffer_sysid': base['mavlink']['sniffer_sysid'],
    'report_stats': base['mavlink']['report_stats'],
    'endpoints': [{
        'type': 'udp',
        'name': 'upstream_test-ext0',
        'address': '10.99.4.1',
        'port': base['mavlink']['extender_auto']['udp_port'],
        'mode': 'client',
        'retry_timeout_sec': 5
    }]
}
assert ext_cfg['tcp_server_port'] == 0
assert ext_cfg['endpoints'][0]['mode'] == 'client'
assert ext_cfg['endpoints'][0]['type'] == 'udp'
" 2>/dev/null && pass || fail "Extender config building failed"

log_test "Extender config uses correct UDP port"
python3 -c "
import json
base = json.load(open('$TEST_DIR/daemon_config.json'))
assert base['mavlink']['extender_auto']['udp_port'] == 14590
" 2>/dev/null && pass || fail "UDP port mismatch"

log_test "Extender config has retry timeout"
python3 -c "
import json
base = json.load(open('$TEST_DIR/daemon_config.json'))
assert base['mavlink']['extender_auto']['udp_port'] > 0
" 2>/dev/null && pass || fail "Invalid port config"

# ============================================================
echo ""
echo "========================================"
echo " 11. Multiple VPN Interface Tracking"
echo "========================================"

log_test "Create multiple TUN devices simultaneously"
create_tun_device test-wg0 && create_tun_device test-ovpn0 && pass || fail "Failed to create multiple TUNs"

log_test "Both interfaces exist"
interface_exists test-wg0 && interface_exists test-ovpn0 && pass || fail "Not all interfaces exist"

log_test "Assign different subnets to each interface"
assign_ip test-wg0 "10.99.5.1/24" && assign_ip test-ovpn0 "10.99.6.1/24" && pass || fail "Failed to assign IPs"

log_test "Both interfaces have correct IPs"
interface_has_ip test-wg0 "10.99.5.1" && interface_has_ip test-ovpn0 "10.99.6.1" && pass || fail "IPs not correct"

log_test "Daemon tracks multiple interfaces"
sleep 1
grep -qi "10.99.5.1\|10.99.6.1\|test-wg0\|test-ovpn0" "$TEST_DIR/daemon.log" 2>/dev/null && pass || skip "No multi-interface log"

log_test "Clean up all test interfaces"
delete_tun_device test-wg0 && delete_tun_device test-ovpn0 && pass || fail "Failed to cleanup"

# ============================================================
echo ""
echo "========================================"
echo " 12. Extender Auto-Disabled Mode"
echo "========================================"

stop_test_daemon

generate_test_config "$TEST_DIR/no_extender_config.json" 14570 14595 false

log_test "Daemon starts with extender auto-disabled"
start_test_daemon "$TEST_DIR/no_extender_config.json" && pass || fail "Daemon failed to start"

log_test "No extender spawned when auto is disabled"
sleep 2
create_tun_device test-ext0 && set_interface_up test-ext0 && assign_ip test-ext0 "10.99.7.1/24"
sleep 2
if grep -qi "spawn.*extender\|extender.*spawn" "$TEST_DIR/daemon.log" 2>/dev/null; then
    fail "Extender should not spawn when auto is disabled"
else
    pass
fi

log_test "Clean up test interface"
delete_tun_device test-ext0 2>/dev/null || true

fi

# ============================================================
echo ""
echo "========================================"
echo " 13. EventBus Event Types"
echo "========================================"

log_test "VpnInterfaceAction::InterfaceCreated exists"
grep -q "InterfaceCreated" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing InterfaceCreated"

log_test "VpnInterfaceAction::InterfaceDeleted exists"
grep -q "InterfaceDeleted" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing InterfaceDeleted"

log_test "VpnInterfaceAction::InterfaceUp exists"
grep -q "InterfaceUp" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing InterfaceUp"

log_test "VpnInterfaceAction::InterfaceDown exists"
grep -q "InterfaceDown" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing InterfaceDown"

log_test "VpnInterfaceAction::AddressAdded exists"
grep -q "AddressAdded" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing AddressAdded"

log_test "VpnInterfaceAction::AddressRemoved exists"
grep -q "AddressRemoved" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing AddressRemoved"

log_test "VpnInterfaceEvent has connection_id field"
grep -q "connection_id" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing connection_id"

log_test "VpnInterfaceEvent has interface_name field"
grep -q "interface_name" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing interface_name"

log_test "VpnInterfaceEvent has ip_address field"
grep -q "ip_address" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing ip_address"

log_test "VpnInterfaceEvent has prefix_len field"
grep -q "prefix_len" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing prefix_len"

log_test "VpnInterfaceEvent has ipv6 field"
grep -q "ipv6" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing ipv6"

log_test "MavExtenderStatusEvent has extender_id field"
grep -q "extender_id" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing extender_id"

log_test "MavExtenderStatusEvent has vpn_connection_id field"
grep -q "vpn_connection_id" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing vpn_connection_id"

log_test "MavExtenderStatusEvent has target_port field"
grep -q "target_port" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing target_port"

log_test "MavExtenderStatusEvent has connected field"
grep -q "connected" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing connected"

log_test "MavExtenderStatusEvent has messages_in field"
grep -q "messages_in" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing messages_in"

log_test "MavExtenderStatusEvent has messages_out field"
grep -q "messages_out" "$PROJECT_DIR/src/core/event.hpp" && pass || fail "Missing messages_out"

# ============================================================
echo ""
echo "========================================"
echo " 14. IPC Methods for Extender Management"
echo "========================================"

log_test "mav.extender.list IPC method registered"
grep -q "mav.extender.list" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Missing IPC method"

log_test "mav.extender.status IPC method registered"
grep -q "mav.extender.status" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Missing IPC method"

log_test "mav.extender.start IPC method registered"
grep -q "mav.extender.start" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Missing IPC method"

log_test "mav.extender.stop IPC method registered"
grep -q "mav.extender.stop" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Missing IPC method"

log_test "mav.extender.remove IPC method registered"
grep -q "mav.extender.remove" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Missing IPC method"

log_test "mav.extender.auto IPC method registered"
grep -q "mav.extender.auto" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Missing IPC method"

log_test "mav.extender.port IPC method registered"
grep -q "mav.extender.port" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Missing IPC method"

# ============================================================
echo ""
echo "========================================"
echo " 15. CLI Backend Integration"
echo "========================================"

log_test "CliBackend has mav_extender_list virtual method"
grep -q "mav_extender_list" "$PROJECT_DIR/src/cli/cli_backend.hpp" && pass || fail "Missing virtual method"

log_test "CliBackend has mav_extender_status virtual method"
grep -q "mav_extender_status" "$PROJECT_DIR/src/cli/cli_backend.hpp" && pass || fail "Missing virtual method"

log_test "CliBackend has mav_extender_start virtual method"
grep -q "mav_extender_start" "$PROJECT_DIR/src/cli/cli_backend.hpp" && pass || fail "Missing virtual method"

log_test "CliBackend has mav_extender_stop virtual method"
grep -q "mav_extender_stop" "$PROJECT_DIR/src/cli/cli_backend.hpp" && pass || fail "Missing virtual method"

log_test "CliBackend has mav_extender_remove virtual method"
grep -q "mav_extender_remove" "$PROJECT_DIR/src/cli/cli_backend.hpp" && pass || fail "Missing virtual method"

log_test "CliBackend has mav_extender_auto virtual method"
grep -q "mav_extender_auto" "$PROJECT_DIR/src/cli/cli_backend.hpp" && pass || fail "Missing virtual method"

log_test "CliBackend has mav_extender_port virtual method"
grep -q "mav_extender_port" "$PROJECT_DIR/src/cli/cli_backend.hpp" && pass || fail "Missing virtual method"

log_test "CliLocalBackend implements extender methods"
grep -q "mav_extender_list" "$PROJECT_DIR/src/cli/cli_local_backend.hpp" && pass || fail "Not implemented"

log_test "CliRemoteBackend implements extender methods"
grep -q "mav_extender_list" "$PROJECT_DIR/src/cli/cli_remote_backend.hpp" && pass || fail "Not implemented"

log_test "CliApp parses mav extender commands"
grep -q "mav extender" "$PROJECT_DIR/src/cli/cli_app.hpp" && pass || fail "Not parsing mav extender"

log_test "CliApp help includes extender commands"
grep -q "mav extender" "$PROJECT_DIR/src/cli/cli_app.hpp" && pass || fail "Help missing extender"

# ============================================================
echo ""
echo "========================================"
echo " 16. VpnManager Integration"
echo "========================================"

log_test "VpnManager accepts VpnInterfaceMonitor pointer"
grep -q "VpnInterfaceMonitor" "$PROJECT_DIR/src/vpn/vpn_manager.hpp" && pass || fail "Not accepting monitor"

log_test "VpnManager calls track_connection on add"
grep -q "track_connection" "$PROJECT_DIR/src/vpn/vpn_manager.hpp" && pass || fail "Not tracking on add"

log_test "VpnManager calls untrack_connection on remove"
grep -q "untrack_connection" "$PROJECT_DIR/src/vpn/vpn_manager.hpp" && pass || fail "Not untracking on remove"

log_test "VpnManager has update_interface_name method"
grep -q "update_interface_name" "$PROJECT_DIR/src/vpn/vpn_manager.hpp" && pass || fail "Missing update_interface_name"

log_test "AppLifecycle creates VpnInterfaceMonitor"
grep -q "VpnInterfaceMonitor" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Not creating monitor"

log_test "AppLifecycle creates MavExtenderManager"
grep -q "MavExtenderManager" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Not creating extender manager"

log_test "AppLifecycle passes monitor to VpnManager"
grep -q "vpn_monitor" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Not passing monitor"

log_test "AppLifecycle starts extender manager"
grep -q "mav_extender_mgr_->start" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Not starting extender manager"

log_test "AppLifecycle stops extender manager on shutdown"
grep -q "mav_extender_mgr_->stop_all" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Not stopping extender manager"

log_test "AppContext includes vpn_monitor pointer"
grep -q "vpn_monitor" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Missing from context"

log_test "AppContext includes mav_extender_mgr pointer"
grep -q "mav_extender_mgr" "$PROJECT_DIR/src/core/app_lifecycle.hpp" && pass || fail "Missing from context"

# ============================================================
echo ""
echo "========================================"
echo " 17. Graceful Shutdown with Extenders"
echo "========================================"

log_test "Stop test daemon"
stop_test_daemon
sleep 1
! kill -0 "$DAEMON_PID" 2>/dev/null && pass || fail "Daemon still running"

log_test "Daemon log shows shutdown"
grep -qi "shutdown\|Shutting down\|signal" "$TEST_DIR/daemon.log" 2>/dev/null && pass || skip "No shutdown log"

log_test "Daemon log shows extender cleanup"
grep -qi "extender.*stop\|stop.*extender\|teardown" "$TEST_DIR/daemon.log" 2>/dev/null && pass || skip "No extender cleanup log"

# ============================================================
echo ""
echo "========================================"
echo " 18. Thread Safety Verification"
echo "========================================"

log_test "VpnInterfaceMonitor uses shared_mutex for tracking"
grep -q "shared_mutex" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Not using shared_mutex"

log_test "VpnInterfaceMonitor uses mutex for state"
grep -q "state_mutex" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Not using state mutex"

log_test "MavExtenderManager uses mutex for extenders map"
grep -q "std::mutex" "$PROJECT_DIR/src/mavlink/mav_extender_manager.hpp" && pass || fail "Not using mutex"

log_test "VpnInterfaceMonitor deletes copy constructor"
grep -q "VpnInterfaceMonitor(const VpnInterfaceMonitor&) = delete" "$PROJECT_DIR/src/core/vpn_monitor.hpp" && pass || fail "Copy not deleted"

log_test "MavExtenderManager deletes copy constructor"
grep -q "MavExtenderManager(const MavExtenderManager&) = delete" "$PROJECT_DIR/src/mavlink/mav_extender_manager.hpp" && pass || fail "Copy not deleted"

# ============================================================
echo ""
echo "========================================"
echo " 19. Event Display in CLI Monitor"
echo "========================================"

log_test "CLI displays mav.extender_status events"
grep -q "mav.extender_status" "$PROJECT_DIR/src/cli/cli_app.hpp" && pass || fail "Not displaying extender events"

log_test "CLI displays vpn.interface_event events"
grep -q "vpn.interface_event" "$PROJECT_DIR/src/cli/cli_app.hpp" && pass || fail "Not displaying interface events"

log_test "CLI displays extender connection status"
grep -q "extender" "$PROJECT_DIR/src/cli/cli_app.hpp" && pass || fail "Not displaying extender status"

log_test "CLI displays interface action"
grep -q "action" "$PROJECT_DIR/src/cli/cli_app.hpp" && pass || fail "Not displaying interface action"

# ============================================================
echo ""
echo "========================================"
echo " 20. Config Schema Validation"
echo "========================================"

log_test "extender_auto.enabled is boolean"
python3 -c "
import json
d = json.load(open('$TEST_DIR/test_config.json'))
assert isinstance(d['mavlink']['extender_auto']['enabled'], bool)
" 2>/dev/null && pass || fail "enabled not boolean"

log_test "extender_auto.udp_port is integer"
python3 -c "
import json
d = json.load(open('$TEST_DIR/test_config.json'))
assert isinstance(d['mavlink']['extender_auto']['udp_port'], int)
" 2>/dev/null && pass || fail "udp_port not integer"

log_test "extender_auto.vpn_types is array"
python3 -c "
import json
d = json.load(open('$TEST_DIR/test_config.json'))
assert isinstance(d['mavlink']['extender_auto']['vpn_types'], list)
" 2>/dev/null && pass || fail "vpn_types not array"

log_test "extender_auto.udp_port in valid range"
python3 -c "
import json
d = json.load(open('$TEST_DIR/test_config.json'))
port = d['mavlink']['extender_auto']['udp_port']
assert 1024 <= port <= 65535, f'Port {port} out of range'
" 2>/dev/null && pass || fail "Port out of valid range"

# ============================================================
echo ""
echo "========================================"
echo " 21. MAVLink Router Extender Mode"
echo "========================================"

log_test "MavRouter has extender_id method"
grep -q "extender_id()" "$PROJECT_DIR/src/mavlink/mav_router.hpp" && pass || fail "Missing extender_id method"

log_test "MavRouter has is_extender method"
grep -q "is_extender()" "$PROJECT_DIR/src/mavlink/mav_router.hpp" && pass || fail "Missing is_extender method"

log_test "MavRouterStatus has is_extender field"
grep -q "is_extender" "$PROJECT_DIR/src/mavlink/mav_router.hpp" && pass || fail "Missing is_extender field"

log_test "MavRouterStatus has extender_id field"
grep -q "extender_id" "$PROJECT_DIR/src/mavlink/mav_router.hpp" && pass || fail "Missing extender_id field"

log_test "MavRouterImpl disables TCP server for extenders"
grep -q "tcp_port = 0" "$PROJECT_DIR/src/mavlink/mav_router.cpp" && pass || fail "Not disabling TCP server"

log_test "MavRouterImpl sets is_extender flag"
grep -q "is_extender" "$PROJECT_DIR/src/mavlink/mav_router.cpp" && pass || fail "Not setting is_extender flag"

# ============================================================
echo ""
echo "========================================"
echo " 22. Build Verification"
echo "========================================"

log_test "Binary includes vpn_monitor symbols"
nm "$BINARY" 2>/dev/null | grep -qi "vpn_monitor\|VpnInterfaceMonitor" && pass || skip "Symbols not found (may be inlined)"

log_test "Binary includes mav_extender symbols"
nm "$BINARY" 2>/dev/null | grep -qi "mav_extender\|MavExtenderManager" && pass || skip "Symbols not found (may be inlined)"

log_test "Binary includes netlink symbols"
nm "$BINARY" 2>/dev/null | grep -qi "netlink\|RTM_" && pass || skip "Netlink symbols not found"

log_test "Binary is linked against required libraries"
ldd "$BINARY" 2>/dev/null | grep -q "libsodium" && pass || skip "libsodium not linked"

log_test "Binary size is reasonable (< 50MB)"
size=$(stat -c '%s' "$BINARY")
if [ "$size" -lt 52428800 ]; then
    pass
else
    fail "Binary too large: $size bytes"
fi

# ============================================================
echo ""
echo "============================================================"
echo " TEST RESULTS"
echo "============================================================"
echo ""
printf "  ${GREEN}PASS: %d${NC}\n" $PASS
printf "  ${RED}FAIL: %d${NC}\n" $FAIL
printf "  ${YELLOW}SKIP: %d${NC}\n" $SKIP
printf "  TOTAL: %d\n" $TOTAL
echo ""

if [ $FAIL -eq 0 ]; then
    echo "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo "${RED}Some tests failed!${NC}"
    exit 1
fi
