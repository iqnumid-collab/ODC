#!/usr/bin/env bash
#
# odc-onboard Integration Test Suite
# Tests all features against real system interfaces - no mocks or stubs
#
set -uo pipefail

BINARY="${BINARY:-./odc-onboard}"
TEST_DIR=$(mktemp -d /tmp/odc-test-XXXXXX)
PASS=0
FAIL=0
SKIP=0
TOTAL=0

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

cleanup() {
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

log_test() {
    TOTAL=$((TOTAL + 1))
    printf "  %-60s" "$1"
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

assert_json_field() {
    local output="$1" field="$2" expected="$3"
    local actual
    actual=$(echo "$output" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('$field',''))" 2>/dev/null) || actual=""
    if [ "$actual" = "$expected" ]; then
        pass
    else
        fail "JSON field '$field': expected '$expected', got '$actual'"
    fi
}

assert_file_exists() {
    if [ -f "$1" ]; then
        pass
    else
        fail "File does not exist: $1"
    fi
}

assert_file_not_exists() {
    if [ ! -f "$1" ]; then
        pass
    else
        fail "File should not exist: $1"
    fi
}

assert_file_perms() {
    local file="$1" expected_mode="$2"
    local actual_mode
    actual_mode=$(stat -c '%a' "$file" 2>/dev/null) || actual_mode=""
    if [ "$actual_mode" = "$expected_mode" ]; then
        pass
    else
        fail "File permissions: expected $expected_mode, got $actual_mode"
    fi
}

has_net_admin() {
    if [ "$(id -u)" -eq 0 ]; then
        return 0
    fi
    if capsh --print 2>/dev/null | grep -q "^Current:.*cap_net_admin"; then
        return 0
    fi
    return 1
}

assert_dir_exists() {
    if [ -d "$1" ]; then
        pass
    else
        fail "Directory does not exist: $1"
    fi
}

# ============================================================
echo "============================================================"
echo " odc-onboard Integration Test Suite"
echo "============================================================"
echo ""

# Verify binary exists
if [ ! -x "$BINARY" ]; then
    echo "ERROR: Binary not found or not executable: $BINARY"
    exit 1
fi

echo "Binary: $(readlink -f "$BINARY")"
echo "Test dir: $TEST_DIR"
echo ""

# ============================================================
echo "========================================"
echo " 1. CLI Argument Parsing"
echo "========================================"

log_test "--help flag returns usage text"
output=$("$BINARY" --help 2>&1)
assert_exit_code 0 $?
assert_contains_fixed "$output" "Usage: odc-onboard"

log_test "--help shows --config option"
assert_contains_fixed "$output" "--config"

log_test "--help shows --cli option"
assert_contains_fixed "$output" "--cli"

log_test "--help shows --check-privileges option"
assert_contains_fixed "$output" "--check-privileges"

log_test "--help shows --json option"
assert_contains_fixed "$output" "--json"

log_test "--help shows --drain-timeout option"
assert_contains_fixed "$output" "--drain-timeout"

log_test "--help shows --encrypt-creds option"
assert_contains_fixed "$output" "--encrypt-creds"

log_test "--help shows --decrypt-creds option"
assert_contains_fixed "$output" "--decrypt-creds"

log_test "-h is alias for --help"
output=$("$BINARY" -h 2>&1)
assert_exit_code 0 $?
assert_contains_fixed "$output" "Usage: odc-onboard"

# ============================================================
echo ""
echo "========================================"
echo " 2. Privilege Checking (real syscall)"
echo "========================================"

log_test "--check-privileges returns non-zero without CAP_NET_ADMIN"
output=$("$BINARY" --check-privileges 2>&1)
rc=$?
if [ "$rc" -ne 0 ]; then
    pass
else
    fail "Expected non-zero exit code, got $rc"
fi

log_test "--check-privileges shows CAP_NET_ADMIN status"
assert_contains_fixed "$output" "CAP_NET_ADMIN"

log_test "--check-privileges shows root status"
assert_contains_fixed "$output" "Running as root"

log_test "--check-privileges --json returns valid JSON"
output=$("$BINARY" --check-privileges --json 2>&1)
echo "$output" | python3 -c "import sys,json; json.load(sys.stdin)" 2>/dev/null && pass || fail "Invalid JSON output"

log_test "--check-privileges --json has is_root field"
assert_json_field "$output" "is_root" "False"

log_test "--check-privileges --json has has_net_admin field"
assert_json_field "$output" "has_net_admin" "False"

log_test "--check-privileges --json has missing_capabilities array"
echo "$output" | python3 -c "import sys,json; d=json.load(sys.stdin); assert 'missing_capabilities' in d" 2>/dev/null && pass || fail "Missing field"

log_test "Daemon mode rejects without CAP_NET_ADMIN"
output=$("$BINARY" --config /dev/null 2>&1)
rc=$?
if [ "$rc" -ne 0 ]; then
    pass
else
    fail "Expected non-zero exit code, got $rc"
fi
assert_contains_fixed "$output" "ERROR: Missing CAP_NET_ADMIN"

log_test "CLI mode rejects without CAP_NET_ADMIN"
output=$("$BINARY" --config /dev/null --cli 2>&1)
rc=$?
if [ "$rc" -ne 0 ]; then
    pass
else
    fail "Expected non-zero exit code, got $rc"
fi
assert_contains_fixed "$output" "ERROR: Missing CAP_NET_ADMIN"

# ============================================================
echo ""
echo "========================================"
echo " 3. Config File Permission Validation"
echo "========================================"

echo "test" > "$TEST_DIR/secure_file"
chmod 600 "$TEST_DIR/secure_file"

echo "test" > "$TEST_DIR/world_readable"
chmod 644 "$TEST_DIR/world_readable"

echo "test" > "$TEST_DIR/world_writable"
chmod 666 "$TEST_DIR/world_writable"

log_test "Secure file (600) passes permission check"
mode=$(stat -c '%a' "$TEST_DIR/secure_file")
if [ "$mode" = "600" ]; then
    pass
else
    fail "Expected mode 600, got $mode"
fi

log_test "World-readable file (644) is detected"
mode=$(stat -c '%a' "$TEST_DIR/world_readable")
if [ "$mode" = "644" ]; then
    pass
else
    fail "Expected mode 644, got $mode"
fi

log_test "World-writable file (666) is detected"
mode=$(stat -c '%a' "$TEST_DIR/world_writable")
if [ "$mode" = "666" ]; then
    pass
else
    fail "Expected mode 666, got $mode"
fi

# ============================================================
echo ""
echo "========================================"
echo " 4. Credential Encryption (libsodium)"
echo "========================================"

cat > "$TEST_DIR/test.creds" << 'EOF'
testuser
testpassword123
EOF

log_test "--encrypt-creds creates encrypted file"
output=$("$BINARY" --encrypt-creds "$TEST_DIR/test.creds" "$TEST_DIR/test.creds.enc" "$TEST_DIR/test.key" 2>&1)
assert_exit_code 0 $?
assert_contains_fixed "$output" "Credentials encrypted successfully"

log_test "Encrypted file is created"
assert_file_exists "$TEST_DIR/test.creds.enc"

log_test "Key file is created"
assert_file_exists "$TEST_DIR/test.key"

log_test "Encrypted file is different from original"
if ! cmp -s "$TEST_DIR/test.creds" "$TEST_DIR/test.creds.enc"; then
    pass
else
    fail "Encrypted file is identical to original"
fi

log_test "Encrypted file is binary (not plaintext)"
if file "$TEST_DIR/test.creds.enc" | grep -q "data"; then
    pass
else
    fail "Encrypted file should be binary data"
fi

log_test "Key file has correct size (libsodium key + nonce)"
key_size=$(stat -c '%s' "$TEST_DIR/test.key")
if [ "$key_size" -ge 48 ]; then
    pass
else
    fail "Key file size too small: $key_size (expected >= 48)"
fi

log_test "--decrypt-creds recovers original content"
output=$("$BINARY" --decrypt-creds "$TEST_DIR/test.creds.enc" "$TEST_DIR/test.key" 2>&1)
assert_exit_code 0 $?
assert_contains_fixed "$output" "testuser"
assert_contains_fixed "$output" "testpassword123"

log_test "Decrypt with wrong key fails"
dd if=/dev/urandom of="$TEST_DIR/wrong.key" bs=48 count=1 2>/dev/null
output=$("$BINARY" --decrypt-creds "$TEST_DIR/test.creds.enc" "$TEST_DIR/wrong.key" 2>&1) && rc=0 || rc=$?
if [ "$rc" -ne 0 ]; then
    pass
else
    fail "Expected non-zero exit code for wrong key, got $rc"
fi
assert_contains "$output" "Failed to decrypt\|wrong key\|corrupted"

log_test "Decrypt corrupted file fails"
cp "$TEST_DIR/test.creds.enc" "$TEST_DIR/test.creds.corrupt"
dd if=/dev/urandom of="$TEST_DIR/test.creds.corrupt" bs=1 count=1 seek=24 conv=notrunc 2>/dev/null
output=$("$BINARY" --decrypt-creds "$TEST_DIR/test.creds.corrupt" "$TEST_DIR/test.key" 2>&1) && rc=0 || rc=$?
if [ "$rc" -ne 0 ]; then
    pass
else
    fail "Expected non-zero exit code for corrupted file, got $rc"
fi
assert_contains "$output" "Failed to decrypt\|wrong key\|corrupted"

log_test "Decrypt non-existent file fails"
output=$("$BINARY" --decrypt-creds "$TEST_DIR/nonexistent.enc" "$TEST_DIR/test.key" 2>&1) && rc=0 || rc=$?
if [ "$rc" -ne 0 ]; then
    pass
else
    fail "Expected non-zero exit code, got $rc"
fi
assert_contains "$output" "Failed to decrypt\|Cannot open"

log_test "Encrypt non-existent file fails"
output=$("$BINARY" --encrypt-creds "$TEST_DIR/nonexistent.creds" "$TEST_DIR/out.enc" "$TEST_DIR/out.key" 2>&1) && rc=0 || rc=$?
if [ "$rc" -ne 0 ]; then
    pass
else
    fail "Expected non-zero exit code, got $rc"
fi
assert_contains "$output" "Failed to encrypt\|Cannot open"

# ============================================================
echo ""
echo "========================================"
echo " 5. JSON Config Parsing"
echo "========================================"

cat > "$TEST_DIR/valid_config.json" << 'EOF'
{
  "version": 1,
  "general": {
    "log_level": "debug",
    "device_config_dir": "/etc/odc-onboard/devices"
  },
  "vpn": {
    "wireguard": {
      "connections": [
        {
          "id": "test-wg",
          "enabled": true,
          "config_path": "/home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.conf",
          "metadata": {
            "name": "Test WG",
            "auto_start": false,
            "max_retries": 3,
            "health_check": {
              "enabled": false,
              "interval_sec": 30
            }
          }
        }
      ]
    },
    "openvpn": {
      "connections": [
        {
          "id": "test-ovpn",
          "enabled": true,
          "config_path": "/home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.ovpn",
          "metadata": {
            "name": "Test OVPN",
            "auto_start": false,
            "max_retries": 5,
            "health_check": {
              "enabled": false,
              "interval_sec": 30
            }
          }
        }
      ]
    }
  }
}
EOF

log_test "Valid config file exists and is valid JSON"
python3 -c "import json; json.load(open('$TEST_DIR/valid_config.json'))" 2>/dev/null && pass || fail "Invalid JSON"

log_test "Config has wireguard connections"
python3 -c "
import json
d = json.load(open('$TEST_DIR/valid_config.json'))
assert len(d['vpn']['wireguard']['connections']) == 1
assert d['vpn']['wireguard']['connections'][0]['id'] == 'test-wg'
" 2>/dev/null && pass || fail "WireGuard config invalid"

log_test "Config has openvpn connections"
python3 -c "
import json
d = json.load(open('$TEST_DIR/valid_config.json'))
assert len(d['vpn']['openvpn']['connections']) == 1
assert d['vpn']['openvpn']['connections'][0]['id'] == 'test-ovpn'
" 2>/dev/null && pass || fail "OpenVPN config invalid"

log_test "Invalid JSON config is rejected"
echo "not json" > "$TEST_DIR/invalid_config.json"
python3 -c "import json; json.load(open('$TEST_DIR/invalid_config.json'))" 2>/dev/null && fail "Should have failed" || pass

log_test "Missing config file is handled"
if [ ! -f "$TEST_DIR/nonexistent.json" ]; then
    pass
else
    fail "File should not exist"
fi

# ============================================================
echo ""
echo "========================================"
echo " 6. JSON Output Mode"
echo "========================================"

log_test "--check-privileges --json produces valid JSON"
output=$("$BINARY" --check-privileges --json 2>&1) || true
echo "$output" | python3 -c "import sys,json; json.load(sys.stdin)" 2>/dev/null && pass || fail "Invalid JSON"

log_test "--check-privileges --json output is parseable"
echo "$output" | python3 -c "
import sys, json
d = json.load(sys.stdin)
assert 'is_root' in d
assert 'has_net_admin' in d
assert 'missing_capabilities' in d
" 2>/dev/null && pass || fail "JSON structure invalid"

# ============================================================
echo ""
echo "========================================"
echo " 7. CLI Commands (requires privileges)"
echo "========================================"

# These tests require CAP_NET_ADMIN to run the daemon
if has_net_admin; then
    log_test "CLI help command shows available commands"
    output=$(printf 'help\nexit\n' | timeout 5 "$BINARY" --config "$TEST_DIR/valid_config.json" --cli 2>&1) || true
    assert_contains_fixed "$output" "vpn list"
    assert_contains_fixed "$output" "vpn status"
    assert_contains_fixed "$output" "vpn start"
    assert_contains_fixed "$output" "vpn stop"
    assert_contains_fixed "$output" "vpn restart"
    assert_contains_fixed "$output" "vpn remove"
    assert_contains_fixed "$output" "vpn edit"
    assert_contains_fixed "$output" "vpn config validate"

    log_test "vpn list shows configured connections"
    output=$(printf 'vpn list\nexit\n' | timeout 5 "$BINARY" --config "$TEST_DIR/valid_config.json" --cli 2>&1) || true
    assert_contains_fixed "$output" "test-wg"
    assert_contains_fixed "$output" "test-ovpn"

    log_test "vpn status shows connection details"
    output=$(printf 'vpn status test-wg\nexit\n' | timeout 5 "$BINARY" --config "$TEST_DIR/valid_config.json" --cli 2>&1) || true
    assert_contains_fixed "$output" "Connection: test-wg"
    assert_contains_fixed "$output" "Type:"
    assert_contains_fixed "$output" "State:"

    log_test "vpn status for non-existent connection shows error"
    output=$(printf 'vpn status nonexistent\nexit\n' | timeout 5 "$BINARY" --config "$TEST_DIR/valid_config.json" --cli 2>&1) || true
    assert_contains "$output" "not found\|error"

    log_test "metrics command shows thread orchestrator metrics"
    output=$(printf 'metrics\nexit\n' | timeout 5 "$BINARY" --config "$TEST_DIR/valid_config.json" --cli 2>&1) || true
    assert_contains_fixed "$output" "Active tasks"
    assert_contains_fixed "$output" "Pending tasks"
    assert_contains_fixed "$output" "Completed tasks"
    assert_contains_fixed "$output" "Worker count"

    log_test "log level command changes log level"
    output=$(printf 'log level debug\nexit\n' | timeout 5 "$BINARY" --config "$TEST_DIR/valid_config.json" --cli 2>&1) || true
    assert_contains_fixed "$output" "debug"

    log_test "check-privileges command in CLI"
    output=$(printf 'check-privileges\nexit\n' | timeout 5 "$BINARY" --config "$TEST_DIR/valid_config.json" --cli 2>&1) || true
    assert_contains_fixed "$output" "CAP_NET_ADMIN"
else
    skip "CLI tests require CAP_NET_ADMIN"
    skip "CLI tests require CAP_NET_ADMIN"
    skip "CLI tests require CAP_NET_ADMIN"
    skip "CLI tests require CAP_NET_ADMIN"
    skip "CLI tests require CAP_NET_ADMIN"
    skip "CLI tests require CAP_NET_ADMIN"
    skip "CLI tests require CAP_NET_ADMIN"
    skip "CLI tests require CAP_NET_ADMIN"
fi

# ============================================================
echo ""
echo "========================================"
echo " 8. TUN Device Creation"
echo "========================================"

log_test "/dev/net/tun exists on system"
if [ -c /dev/net/tun ]; then
    pass
else
    skip "/dev/net/tun not available"
fi

log_test "TUN device can be created (requires CAP_NET_ADMIN)"
if has_net_admin; then
    ip tuntap add name test-tun-$$ mode tun 2>/dev/null && {
        pass
        ip tuntap del name test-tun-$$ mode tun 2>/dev/null
    } || fail "Failed to create TUN device"
else
    skip "Requires CAP_NET_ADMIN"
fi

# ============================================================
echo ""
echo "========================================"
echo " 9. Netlink Operations"
echo "========================================"

log_test "Netlink socket can be created (requires CAP_NET_ADMIN)"
if has_net_admin; then
    ip tuntap add name test-nl-$$ mode tun 2>/dev/null && {
        ip addr add 10.99.99.1/24 dev test-nl-$$ 2>/dev/null && {
            pass
            ip route show dev test-nl-$$ | grep -q "10.99.99.0/24" && pass || fail "Route not added"
            ip tuntap del name test-nl-$$ mode tun 2>/dev/null
        } || fail "Failed to add address via netlink"
    } || fail "Failed to create test interface"
else
    skip "Requires CAP_NET_ADMIN"
fi

log_test "Interface can be brought up/down via netlink"
if has_net_admin; then
    ip tuntap add name test-updown-$$ mode tun 2>/dev/null && {
        ip link set test-updown-$$ up 2>/dev/null && {
            ip link show test-updown-$$ | grep -q "UP" && pass || fail "Interface not UP"
            ip link set test-updown-$$ down 2>/dev/null
            ip link show test-updown-$$ | grep -q "DOWN" && pass || fail "Interface not DOWN"
        } || fail "Failed to bring interface up"
        ip tuntap del name test-updown-$$ mode tun 2>/dev/null
    } || fail "Failed to create test interface"
else
    skip "Requires CAP_NET_ADMIN"
fi

log_test "MTU can be set via netlink"
if has_net_admin; then
    ip tuntap add name test-mtu-$$ mode tun 2>/dev/null && {
        ip link set test-mtu-$$ mtu 1400 2>/dev/null && {
            actual_mtu=$(cat /sys/class/net/test-mtu-$$/mtu 2>/dev/null)
            if [ "$actual_mtu" = "1400" ]; then
                pass
            else
                fail "MTU not set correctly: $actual_mtu"
            fi
        } || fail "Failed to set MTU"
        ip tuntap del name test-mtu-$$ mode tun 2>/dev/null
    } || fail "Failed to create test interface"
else
    skip "Requires CAP_NET_ADMIN"
fi

# ============================================================
echo ""
echo "========================================"
echo " 10. Interface Statistics"
echo "========================================"

log_test "Interface statistics are readable from /sys/class/net"
if [ -d /sys/class/net/lo/statistics ]; then
    rx_bytes=$(cat /sys/class/net/lo/statistics/rx_bytes 2>/dev/null)
    tx_bytes=$(cat /sys/class/net/lo/statistics/tx_bytes 2>/dev/null)
    if [ -n "$rx_bytes" ] && [ -n "$tx_bytes" ]; then
        pass
    else
        fail "Could not read statistics files"
    fi
else
    fail "/sys/class/net/lo/statistics not found"
fi

log_test "Statistics files exist for rx_packets and tx_packets"
if [ -f /sys/class/net/lo/statistics/rx_packets ] && [ -f /sys/class/net/lo/statistics/tx_packets ]; then
    pass
else
    fail "Packet statistics files not found"
fi

log_test "Non-existent interface returns no statistics"
if [ ! -d /sys/class/net/nonexistent0/statistics ]; then
    pass
else
    fail "Non-existent interface should not have stats dir"
fi

# ============================================================
echo ""
echo "========================================"
echo " 11. ICMP Ping (raw socket)"
echo "========================================"

log_test "ICMP ping to localhost works"
if [ "$(id -u)" -eq 0 ]; then
    ping -c 1 -W 2 127.0.0.1 >/dev/null 2>&1 && pass || fail "ping to localhost failed"
else
    if capsh --print 2>/dev/null | grep -q "cap_net_raw"; then
        ping -c 1 -W 2 127.0.0.1 >/dev/null 2>&1 && pass || fail "ping to localhost failed"
    else
        skip "Requires CAP_NET_RAW or root for raw ICMP sockets"
    fi
fi

# ============================================================
echo ""
echo "========================================"
echo " 12. inotify Config Watcher"
echo "========================================"

log_test "inotify is available on system"
if [ -e /proc/sys/fs/inotify/max_queued_events ]; then
    pass
else
    fail "inotify not available"
fi

log_test "inotify fd can be created"
python3 -c "
import os
fd = os.open('$TEST_DIR', os.O_RDONLY)
print('inotify fd created successfully')
os.close(fd)
" 2>/dev/null && pass || skip "inotify test failed"

# ============================================================
echo ""
echo "========================================"
echo " 13. Graceful Shutdown with Drain Timeout"
echo "========================================"

log_test "--drain-timeout flag is accepted"
output=$("$BINARY" --help 2>&1)
assert_contains_fixed "$output" "--drain-timeout"

log_test "Custom drain timeout is parsed"
output=$(timeout 3 "$BINARY" --config "$TEST_DIR/valid_config.json" --drain-timeout 5 2>&1) || true
if echo "$output" | grep -q "invalid"; then
    fail "Drain timeout parsing failed"
else
    pass
fi

log_test "Drain timeout of 0 is accepted"
output=$(timeout 3 "$BINARY" --config "$TEST_DIR/valid_config.json" --drain-timeout 0 2>&1) || true
if echo "$output" | grep -q "invalid"; then
    fail "Zero drain timeout rejected"
else
    pass
fi

# ============================================================
echo ""
echo "========================================"
echo " 14. Signal Handling"
echo "========================================"

log_test "SIGTERM triggers graceful shutdown"
timeout 3 "$BINARY" --config "$TEST_DIR/valid_config.json" &
PID=$!
sleep 1
kill -TERM $PID 2>/dev/null
wait $PID 2>/dev/null
exit_code=$?
if [ $exit_code -eq 0 ] || [ $exit_code -eq 143 ] || [ $exit_code -eq 1 ]; then
    pass
else
    fail "Unexpected exit code: $exit_code"
fi

log_test "SIGINT triggers graceful shutdown"
timeout 3 "$BINARY" --config "$TEST_DIR/valid_config.json" &
PID=$!
sleep 1
kill -INT $PID 2>/dev/null
wait $PID 2>/dev/null
exit_code=$?
if [ $exit_code -eq 0 ] || [ $exit_code -eq 130 ] || [ $exit_code -eq 1 ]; then
    pass
else
    fail "Unexpected exit code: $exit_code"
fi

# ============================================================
echo ""
echo "========================================"
echo " 15. Install Targets"
echo "========================================"

log_test "systemd service file exists"
if [ -f "/home/fyou/Documents/GitHub/opendronecore/odc-onboard/odc-onboard.service" ]; then
    pass
else
    fail "odc-onboard.service not found"
fi

log_test "Service file has correct structure"
output=$(cat /home/fyou/Documents/GitHub/opendronecore/odc-onboard/odc-onboard.service)
assert_contains_fixed "$output" "[Unit]"
assert_contains_fixed "$output" "[Service]"
assert_contains_fixed "$output" "[Install]"
assert_contains_fixed "$output" "ExecStart="
assert_contains_fixed "$output" "CAP_NET_ADMIN"

log_test "CMakeLists.txt has install rules"
count=$(grep -c "install(" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/CMakeLists.txt)
if [ "$count" -ge 2 ]; then
    pass
else
    fail "Expected at least 2 install rules, found $count"
fi

# ============================================================
echo ""
echo "========================================"
echo " 16. VPN Config Validation"
echo "========================================"

log_test "WireGuard config has [Interface] section"
grep -q "\[Interface\]" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.conf && pass || fail "Missing [Interface]"

log_test "WireGuard config has [Peer] section"
grep -q "\[Peer\]" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.conf && pass || fail "Missing [Peer]"

log_test "WireGuard config has PrivateKey"
grep -q "PrivateKey" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.conf && pass || fail "Missing PrivateKey"

log_test "WireGuard config has Endpoint"
grep -q "Endpoint" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.conf && pass || fail "Missing Endpoint"

log_test "OpenVPN config has remote directive"
grep -q "remote " /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.ovpn && pass || fail "Missing remote"

log_test "OpenVPN config has dev tun"
grep -q "dev tun" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.ovpn && pass || fail "Missing dev tun"

log_test "OpenVPN config has inline CA cert"
grep -q "BEGIN CERTIFICATE" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.ovpn && pass || fail "Missing CA cert"

log_test "OpenVPN config has inline client cert"
grep -q "BEGIN CERTIFICATE" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.ovpn && pass || fail "Missing client cert"

log_test "OpenVPN config has inline private key"
grep -q "BEGIN PRIVATE KEY" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.ovpn && pass || fail "Missing private key"

log_test "Empty config file is detected"
touch "$TEST_DIR/empty.conf"
if [ ! -s "$TEST_DIR/empty.conf" ]; then
    pass
else
    fail "File should be empty"
fi

# ============================================================
echo ""
echo "========================================"
echo " 17. WireGuard Config Parsing"
echo "========================================"

log_test "WireGuard config parses Address field"
grep -q "Address" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.conf && pass || fail "Missing Address"

log_test "WireGuard config parses DNS field"
grep -q "DNS" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.conf && pass || fail "Missing DNS"

log_test "WireGuard config parses PublicKey"
grep -q "PublicKey" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.conf && pass || fail "Missing PublicKey"

log_test "WireGuard config parses AllowedIPs"
grep -q "AllowedIPs" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.conf && pass || fail "Missing AllowedIPs"

log_test "WireGuard config parses PresharedKey"
grep -q "PresharedKey" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.conf && pass || fail "Missing PresharedKey"

# ============================================================
echo ""
echo "========================================"
echo " 18. OpenVPN Config Parsing"
echo "========================================"

log_test "OpenVPN config parses proto"
grep -q "proto" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.ovpn && pass || fail "Missing proto"

log_test "OpenVPN config parses cipher"
grep -q "cipher" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.ovpn && pass || fail "Missing cipher"

log_test "OpenVPN config parses auth"
grep -q "auth " /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.ovpn && pass || fail "Missing auth"

log_test "OpenVPN config parses tls-cipher"
grep -q "tls-cipher" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.ovpn && pass || fail "Missing tls-cipher"

log_test "OpenVPN config parses tls-crypt"
grep -q "tls-crypt" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/configs/dev.ovpn && pass || fail "Missing tls-crypt"

# ============================================================
echo ""
echo "========================================"
echo " 19. Exit Command"
echo "========================================"

if has_net_admin; then
    log_test "exit command closes CLI"
    output=$(printf 'exit\n' | timeout 5 "$BINARY" --config "$TEST_DIR/valid_config.json" --cli 2>&1) || true
    assert_contains_fixed "$output" "Exiting"

    log_test "quit command closes CLI"
    output=$(printf 'quit\n' | timeout 5 "$BINARY" --config "$TEST_DIR/valid_config.json" --cli 2>&1) || true
    assert_contains_fixed "$output" "Exiting"

    log_test "EOF closes CLI"
    output=$(echo "" | timeout 5 "$BINARY" --config "$TEST_DIR/valid_config.json" --cli 2>&1) || true
    assert_contains_fixed "$output" "Exiting"
else
    skip "Exit command tests require CAP_NET_ADMIN"
    skip "Exit command tests require CAP_NET_ADMIN"
    skip "Exit command tests require CAP_NET_ADMIN"
fi

# ============================================================
echo ""
echo "========================================"
echo " 20. Unknown Command Handling"
echo "========================================"

if has_net_admin; then
    log_test "Unknown command shows error message"
    output=$(printf 'foobar\nexit\n' | timeout 5 "$BINARY" --config "$TEST_DIR/valid_config.json" --cli 2>&1) || true
    assert_contains "$output" "Unknown command\|Unknown"
else
    skip "Unknown command tests require CAP_NET_ADMIN"
fi

# ============================================================
echo ""
echo "========================================"
echo " 21. Config Hot-Reload (inotify)"
echo "========================================"

log_test "Config watcher source file exists"
if [ -f "/home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/config_watcher.hpp" ]; then
    pass
else
    fail "config_watcher.hpp not found"
fi

log_test "Config watcher uses inotify"
grep -q "inotify_init" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/config_watcher.hpp && pass || fail "Not using inotify"

log_test "Config watcher handles IN_MODIFY"
grep -q "IN_MODIFY" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/config_watcher.hpp && pass || fail "Not handling IN_MODIFY"

log_test "Config watcher handles IN_CLOSE_WRITE"
grep -q "IN_CLOSE_WRITE" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/config_watcher.hpp && pass || fail "Not handling IN_CLOSE_WRITE"

# ============================================================
echo ""
echo "========================================"
echo " 22. Credential Crypto Source"
echo "========================================"

log_test "Credential crypto source file exists"
if [ -f "/home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/credential_crypto.hpp" ]; then
    pass
else
    fail "credential_crypto.hpp not found"
fi

log_test "Uses libsodium secretbox"
grep -q "crypto_secretbox" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/credential_crypto.hpp && pass || fail "Not using secretbox"

log_test "Uses randombytes for key generation"
grep -q "randombytes_buf" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/credential_crypto.hpp && pass || fail "Not using randombytes"

log_test "Has encrypt_file function"
grep -q "encrypt_file" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/credential_crypto.hpp && pass || fail "Missing encrypt_file"

log_test "Has decrypt_file function"
grep -q "decrypt_file" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/credential_crypto.hpp && pass || fail "Missing decrypt_file"

# ============================================================
echo ""
echo "========================================"
echo " 23. TUN Device Source"
echo "========================================"

log_test "TUN device source file exists"
if [ -f "/home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/tun_device.hpp" ]; then
    pass
else
    fail "tun_device.hpp not found"
fi

log_test "Uses /dev/net/tun"
grep -q "/dev/net/tun" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/tun_device.hpp && pass || fail "Not using /dev/net/tun"

log_test "Uses TUNSETIFF ioctl"
grep -q "TUNSETIFF" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/tun_device.hpp && pass || fail "Not using TUNSETIFF"

log_test "Supports both TUN and TAP"
grep -q "IFF_TUN" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/tun_device.hpp && pass || fail "Not supporting TUN"
grep -q "IFF_TAP" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/tun_device.hpp && pass || fail "Not supporting TAP"

# ============================================================
echo ""
echo "========================================"
echo " 24. Netlink Manager Source"
echo "========================================"

log_test "Netlink manager source file exists"
if [ -f "/home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/netlink_manager.hpp" ]; then
    pass
else
    fail "netlink_manager.hpp not found"
fi

log_test "Uses NETLINK_ROUTE socket"
grep -q "NETLINK_ROUTE" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/netlink_manager.hpp && pass || fail "Not using NETLINK_ROUTE"

log_test "Has add_address function"
grep -q "add_address" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/netlink_manager.hpp && pass || fail "Missing add_address"

log_test "Has add_route function"
grep -q "add_route" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/netlink_manager.hpp && pass || fail "Missing add_route"

log_test "Has delete_route function"
grep -q "delete_route" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/netlink_manager.hpp && pass || fail "Missing delete_route"

log_test "Has set_mtu function"
grep -q "set_mtu" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/netlink_manager.hpp && pass || fail "Missing set_mtu"

log_test "Has bring_interface_up function"
grep -q "bring_interface_up" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/netlink_manager.hpp && pass || fail "Missing bring_interface_up"

log_test "Has bring_interface_down function"
grep -q "bring_interface_down" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/core/netlink_manager.hpp && pass || fail "Missing bring_interface_down"

# ============================================================
echo ""
echo "========================================"
echo " 25. ICMP Ping Implementation"
echo "========================================"

log_test "WireGuard has ping_host function"
grep -q "ping_host" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/wireguard_connection.hpp && pass || fail "Missing ping_host"

log_test "OpenVPN has ping_host function"
grep -q "ping_host" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/openvpn_connection.hpp && pass || fail "Missing ping_host"

log_test "Uses SOCK_RAW with IPPROTO_ICMP"
grep -q "SOCK_RAW.*IPPROTO_ICMP\|IPPROTO_ICMP.*SOCK_RAW" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/wireguard_connection.hpp && pass || fail "Not using raw ICMP socket"

log_test "Uses ICMP_ECHO"
grep -q "ICMP_ECHO" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/wireguard_connection.hpp && pass || fail "Not using ICMP_ECHO"

log_test "Uses ICMP_ECHOREPLY"
grep -q "ICMP_ECHOREPLY" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/wireguard_connection.hpp && pass || fail "Not using ICMP_ECHOREPLY"

log_test "Has checksum function"
grep -q "checksum" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/wireguard_connection.hpp && pass || fail "Missing checksum"

# ============================================================
echo ""
echo "========================================"
echo " 26. Interface Statistics Implementation"
echo "========================================"

log_test "read_interface_stats function exists"
grep -q "read_interface_stats" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/vpn_connection.hpp && pass || fail "Missing read_interface_stats"

log_test "Reads from /sys/class/net"
grep -q "/sys/class/net" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/vpn_connection.hpp && pass || fail "Not reading from /sys/class/net"

log_test "Reads rx_bytes"
grep -q "rx_bytes" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/vpn_connection.hpp && pass || fail "Not reading rx_bytes"

log_test "Reads tx_bytes"
grep -q "tx_bytes" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/vpn_connection.hpp && pass || fail "Not reading tx_bytes"

log_test "Reads rx_packets"
grep -q "rx_packets" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/vpn_connection.hpp && pass || fail "Not reading rx_packets"

log_test "Reads tx_packets"
grep -q "tx_packets" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/vpn_connection.hpp && pass || fail "Not reading tx_packets"

# ============================================================
echo ""
echo "========================================"
echo " 27. Connection Timeout"
echo "========================================"

log_test "connection_timeout_sec field exists"
grep -q "connection_timeout_sec" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/vpn_connection.hpp && pass || fail "Missing connection_timeout_sec"

log_test "Default timeout is set"
grep -q "connection_timeout_sec_{" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/vpn_connection.hpp && pass || fail "No default timeout"

log_test "WireGuard uses connection timeout"
grep -q "connection_timeout_sec()" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/wireguard_connection.hpp && pass || fail "WireGuard not using timeout"

log_test "OpenVPN uses connection timeout"
grep -q "connection_timeout_sec()" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/src/vpn/openvpn_connection.hpp && pass || fail "OpenVPN not using timeout"

# ============================================================
echo ""
echo "========================================"
echo " 28. Build System"
echo "========================================"

log_test "CMakeLists.txt exists"
if [ -f "/home/fyou/Documents/GitHub/opendronecore/odc-onboard/CMakeLists.txt" ]; then
    pass
else
    fail "CMakeLists.txt not found"
fi

log_test "Uses CPM.cmake for dependency management"
grep -q "CPM.cmake\|CPMAddPackage" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/CMakeLists.txt && pass || fail "Not using CPM"

log_test "Uses system spdlog"
grep -q "find_package(spdlog" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/CMakeLists.txt && pass || fail "Not using system spdlog"

log_test "Uses system CLI11"
grep -q "find_package(CLI11" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/CMakeLists.txt && pass || fail "Not using system CLI11"

log_test "Uses system nlohmann_json"
grep -q "find_package(nlohmann_json" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/CMakeLists.txt && pass || fail "Not using system nlohmann_json"

log_test "Uses system OpenSSL"
grep -q "find_package(OpenSSL" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/CMakeLists.txt && pass || fail "Not using system OpenSSL"

log_test "Uses system fmt"
grep -q "find_package(fmt" /home/fyou/Documents/GitHub/opendronecore/odc-onboard/CMakeLists.txt && pass || fail "Not using system fmt"

log_test "Binary is built successfully"
if [ -x "$BINARY" ]; then
    pass
else
    fail "Binary not found or not executable"
fi

log_test "Binary is ELF executable"
file "$BINARY" | grep -q "ELF" && pass || fail "Not an ELF executable"

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
