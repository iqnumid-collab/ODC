#!/usr/bin/env bash
#
# Shared test helper functions for odc-onboard integration test suite
#
set -uo pipefail

# ── Counters ──────────────────────────────────────────────────────────────────
PASS=0
FAIL=0
SKIP=0
TOTAL=0

# ── Colors ────────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# ── Logging ───────────────────────────────────────────────────────────────────
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

# ── Assertions ────────────────────────────────────────────────────────────────
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
        fail "Output does not contain pattern: $pattern"
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

assert_not_contains_fixed() {
    local output="$1" pattern="$2"
    if echo "$output" | grep -qF -- "$pattern"; then
        fail "Output should not contain: $pattern"
    else
        pass
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

assert_json_value_gt() {
    local output="$1" field="$2" threshold="$3"
    local actual
    actual=$(echo "$output" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('$field',0))" 2>/dev/null) || actual="0"
    if [ "$actual" -gt "$threshold" ] 2>/dev/null; then
        pass
    else
        fail "JSON field '$field': expected > $threshold, got '$actual'"
    fi
}

assert_json_value_gte() {
    local output="$1" field="$2" threshold="$3"
    local actual
    actual=$(echo "$output" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('$field',0))" 2>/dev/null) || actual="0"
    if [ "$actual" -ge "$threshold" ] 2>/dev/null; then
        pass
    else
        fail "JSON field '$field': expected >= $threshold, got '$actual'"
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
    if [ ! -e "$1" ]; then
        pass
    else
        fail "File should not exist: $1"
    fi
}

assert_dir_exists() {
    if [ -d "$1" ]; then
        pass
    else
        fail "Directory does not exist: $1"
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

assert_file_owner() {
    local file="$1" expected_owner="$2"
    local actual_owner
    actual_owner=$(stat -c '%U' "$file" 2>/dev/null) || actual_owner=""
    if [ "$actual_owner" = "$expected_owner" ]; then
        pass
    else
        fail "File owner: expected $expected_owner, got $actual_owner"
    fi
}

assert_valid_json() {
    local output="$1"
    echo "$output" | python3 -c "import sys,json; json.load(sys.stdin)" 2>/dev/null && pass || fail "Invalid JSON output"
}

assert_service_active() {
    local status
    status=$(systemctl is-active "$1" 2>/dev/null) || status="unknown"
    if [ "$status" = "active" ]; then
        pass
    else
        fail "Service $1 is not active (status: $status)"
    fi
}

assert_service_inactive() {
    local status
    status=$(systemctl is-active "$1" 2>/dev/null) || status="inactive"
    if [ "$status" = "inactive" ] || [ "$status" = "failed" ]; then
        pass
    else
        fail "Service $1 should be inactive (status: $status)"
    fi
}

# ── Privilege checks ─────────────────────────────────────────────────────────
has_net_admin() {
    if [ "$(id -u)" -eq 0 ]; then
        return 0
    fi
    if capsh --print 2>/dev/null | grep -q "^Current:.*cap_net_admin"; then
        return 0
    fi
    return 1
}

has_root() {
    [ "$(id -u)" -eq 0 ]
}

# ── Wait helpers ──────────────────────────────────────────────────────────────
wait_for_socket() {
    local socket_path="$1"
    local timeout_sec="${2:-5}"
    local elapsed=0
    while [ $elapsed -lt $timeout_sec ]; do
        if [ -S "$socket_path" ]; then
            return 0
        fi
        sleep 0.5
        elapsed=$((elapsed + 1))
    done
    return 1
}

wait_for_process() {
    local process_name="$1"
    local timeout_sec="${2:-5}"
    local elapsed=0
    while [ $elapsed -lt $timeout_sec ]; do
        if pgrep -x "$process_name" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.5
        elapsed=$((elapsed + 1))
    done
    return 1
}

wait_for_service() {
    local service_name="$1"
    local timeout_sec="${2:-10}"
    local elapsed=0
    while [ $elapsed -lt $timeout_sec ]; do
        local status
        status=$(systemctl is-active "$service_name" 2>/dev/null) || status="unknown"
        if [ "$status" = "active" ]; then
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    return 1
}

# ── CLI helpers ───────────────────────────────────────────────────────────────
run_cli_command() {
    local command="$1"
    local timeout_sec="${2:-10}"
    printf '%s\nexit\n' "$command" | timeout "$timeout_sec" "$BINARY" --cli --config "$CONFIG_PATH" 2>&1 || true
}

run_cli_command_json() {
    local command="$1"
    local timeout_sec="${2:-10}"
    printf '%s\nexit\n' "$command" | timeout "$timeout_sec" "$BINARY" --cli --config "$CONFIG_PATH" --json 2>&1 || true
}

get_journal_lines() {
    local count="${1:-20}"
    journalctl -u odc-onboard --no-pager -n "$count" 2>/dev/null || echo ""
}

journal_contains() {
    local pattern="$1"
    journalctl -u odc-onboard --no-pager -n 50 2>/dev/null | grep -qF -- "$pattern"
}

# ── Results ───────────────────────────────────────────────────────────────────
print_results() {
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
        return 0
    else
        echo "${RED}Some tests failed!${NC}"
        return 1
    fi
}

# ── Cleanup trap ──────────────────────────────────────────────────────────────
TEST_DIR=""
cleanup_test_dir() {
    if [ -n "$TEST_DIR" ] && [ -d "$TEST_DIR" ]; then
        rm -rf "$TEST_DIR"
    fi
}
