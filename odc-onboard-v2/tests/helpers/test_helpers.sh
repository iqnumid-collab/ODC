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

# ── IPC helpers ───────────────────────────────────────────────────────────────
# JSON-RPC 2.0 over Unix domain socket (newline-delimited, same as v1 IPC).
ipc_request() {
    local socket_path="$1"
    local method="$2"
    local params='{}'
    local req_id="1"
    if [ "$#" -ge 3 ]; then
        params="$3"
    fi
    if [ "$#" -ge 4 ]; then
        req_id="$4"
    fi

    python3 - "$socket_path" "$method" "$params" "$req_id" <<'PY'
import json
import socket
import sys

sock_path, method, params_json, req_id = sys.argv[1:5]
params = json.loads(params_json) if params_json else {}

request = {
    "jsonrpc": "2.0",
    "method": method,
    "params": params,
    "id": int(req_id),
}
payload = (json.dumps(request, separators=(",", ":")) + "\n").encode("utf-8")

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(15)
sock.connect(sock_path)
sock.sendall(payload)

buf = b""
while b"\n" not in buf:
    chunk = sock.recv(4096)
    if not chunk:
        break
    buf += chunk

line = buf.split(b"\n", 1)[0].decode("utf-8", errors="replace")
if not line:
    print(json.dumps({"error": "empty IPC response"}))
    sys.exit(1)

response = json.loads(line)
if response.get("error"):
    print(json.dumps(response))
    sys.exit(1)

result = response.get("result")
print(json.dumps(result if result is not None else {}))
PY
}

# ── Graph CLI helpers (real hardware suite) ───────────────────────────────────
run_graph_cli() {
    local command="$1"
    local timeout_sec="${2:-15}"
    local socket="${ODC_SOCKET:-${SOCKET_PATH:-/run/odc-onboard/odc-onboard.sock}}"
    local raw
    raw=$(printf '%s\nexit\n' "$command" | timeout "$timeout_sec" "$BINARY" --cli --socket "$socket" --json 2>&1 || true)
    echo "$raw" | python3 -c "
import sys, json
text = sys.stdin.read().strip()
if not text:
    sys.exit(0)
for line in reversed(text.splitlines()):
    line = line.strip()
    if not line:
        continue
    try:
        json.loads(line)
        print(line)
        sys.exit(0)
    except json.JSONDecodeError:
        continue
print(text)
" 2>/dev/null || echo "$raw"
}

graph_ipc_add_component() {
    local id="$1"
    local type="$2"
    local config_json="$3"
    local enabled="${4:-false}"
    local req_id="${5:-50}"
    local params
    params=$(python3 - "$id" "$type" "$config_json" "$enabled" <<'PY'
import json, sys
cid, ctype, cfg_json, enabled = sys.argv[1:5]
print(json.dumps({
    "id": cid,
    "type": ctype,
    "enabled": enabled.lower() in ("1", "true", "yes"),
    "config": json.loads(cfg_json),
}, separators=(",", ":")))
PY
)
    ipc_request "${ODC_SOCKET:-${SOCKET_PATH:-}}" "graph.components.add" "$params" "$req_id"
}

graph_cli_success() {
    local output="$1"
    echo "$output" | python3 -c "
import sys, json
d = json.load(sys.stdin)
if 'success' in d:
    sys.exit(0 if d.get('success') else 1)
sys.exit(0)
" 2>/dev/null
}

assert_yaml_contains() {
    local yaml_file="$1"
    local pattern="$2"
    if [ ! -f "$yaml_file" ]; then
        fail "YAML file missing: $yaml_file"
        return
    fi
    if grep -qF -- "$pattern" "$yaml_file"; then
        pass
    else
        fail "YAML does not contain: $pattern"
    fi
}

graph_status_json() {
    local socket="${1:-${ODC_SOCKET:-${SOCKET_PATH:-}}}"
    ipc_request "$socket" "graph.status" '{}' 1 2>/dev/null || echo '{}'
}

graph_component_ids() {
    local status_json="$1"
    echo "$status_json" | python3 -c "import sys,json; d=json.load(sys.stdin); print(','.join(c.get('id','') for c in d.get('components',[])))" 2>/dev/null || echo ""
}

graph_has_component() {
    local status_json="$1"
    local comp_id="$2"
    echo "$status_json" | python3 -c "import sys,json; d=json.load(sys.stdin); ids=[c.get('id') for c in d.get('components',[])]; sys.exit(0 if '$comp_id' in ids else 1)" 2>/dev/null
}

wait_graph_vpn_ready() {
    local socket="$1"
    local vpn_id="$2"
    local timeout_sec="${3:-60}"
    local elapsed=0
    while [ "$elapsed" -lt "$timeout_sec" ]; do
        local status
        status=$(graph_status_json "$socket")
        if echo "$status" | python3 -c "
import sys, json
d = json.load(sys.stdin)
for c in d.get('components', []):
    if c.get('id') != '$vpn_id':
        continue
    vs = c.get('vpn_state', c.get('binding', {}).get('vpn_state', ''))
    ip_ready = c.get('binding', {}).get('ip_ready', False)
    if vs == 'connected' and ip_ready:
        sys.exit(0)
sys.exit(1)
" 2>/dev/null; then
            return 0
        fi
        sleep 2
        elapsed=$((elapsed + 2))
    done
    return 1
}

wait_graph_rtsp_running() {
    local socket="$1"
    local comp_id="$2"
    local timeout_sec="${3:-30}"
    local elapsed=0
    while [ "$elapsed" -lt "$timeout_sec" ]; do
        local status
        status=$(graph_status_json "$socket")
        if echo "$status" | python3 -c "
import sys, json
d = json.load(sys.stdin)
for c in d.get('components', []):
    if c.get('id') != '$comp_id':
        continue
    if c.get('running'):
        sys.exit(0)
    if c.get('state') == 'running' and c.get('type') == 'rtsp.compressor':
        sys.exit(0)
sys.exit(1)
" 2>/dev/null; then
            return 0
        fi
        sleep 2
        elapsed=$((elapsed + 2))
    done
    return 1
}

wait_graph_links_active() {
    local socket="$1"
    local timeout_sec="${2:-30}"
    local elapsed=0
    while [ "$elapsed" -lt "$timeout_sec" ]; do
        local status
        status=$(graph_status_json "$socket")
        if echo "$status" | python3 -c "
import sys, json
d = json.load(sys.stdin)
sys.exit(0 if any(l.get('active') for l in d.get('links', [])) else 1)
" 2>/dev/null; then
            return 0
        fi
        sleep 2
        elapsed=$((elapsed + 2))
    done
    return 1
}

wait_for_tcp_port() {
    local port="$1"
    local timeout_sec="${2:-30}"
    local elapsed=0
    while [ "$elapsed" -lt "$timeout_sec" ]; do
        if ss -Htan "sport = :${port}" 2>/dev/null | grep -q LISTEN; then
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    return 1
}

start_video_sources() {
    local ports_csv="$1"
    local video_file="$2"
    local video_src_bin="$3"
    local log_dir="$4"
    VIDEO_SOURCE_PIDS=()
    IFS=',' read -ra PORTS <<< "$ports_csv"
    for port in "${PORTS[@]}"; do
        port=$(echo "$port" | tr -d ' ')
        [ -n "$port" ] || continue
        "$video_src_bin" --video "$video_file" --port "$port" --path /stream --loop \
            >"$log_dir/video-source-${port}.log" 2>&1 &
        VIDEO_SOURCE_PIDS+=($!)
        sleep 1
    done
}

stop_video_sources() {
    if [ -n "${VIDEO_SOURCE_PIDS:-}" ]; then
        for pid in "${VIDEO_SOURCE_PIDS[@]}"; do
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        done
        VIDEO_SOURCE_PIDS=()
    fi
}

assert_rtsp_bandwidth() {
    local monitor_bin="$1"
    local uri="$2"
    local duration="${3:-8}"
    local output
    output=$("$monitor_bin" "$uri" --tcp --duration "$duration" 2>&1) || true
    if echo "$output" | grep -qP "Avg Bandwidth:\s*[1-9]"; then
        pass
    elif echo "$output" | grep -qP "current:\s*[1-9]"; then
        pass
    else
        fail "No RTSP bandwidth detected for $uri"
    fi
}

deploy_vpn_configs() {
    local project_dir="$1"
    local dest_dir="${2:-/etc/odc-onboard/vpn}"
    local vpn_src="${3:-$project_dir/configs}"
    local f src

    if ! mkdir -p "$dest_dir" 2>/dev/null; then
        echo "Cannot create $dest_dir (run with sudo)" >&2
        return 1
    fi

    for f in dev.conf dev.ovpn remote.conf remote.ovpn; do
        src="$vpn_src/$f"
        if [ ! -f "$src" ]; then
            echo "Missing VPN config source: $src" >&2
            return 1
        fi
        if ! cp "$src" "$dest_dir/$f" 2>/dev/null; then
            echo "Cannot deploy $src -> $dest_dir/$f (run with sudo)" >&2
            return 1
        fi
        chmod 600 "$dest_dir/$f" 2>/dev/null || true
    done

    if ! grep -q "192.168.10.201" "$dest_dir/dev.conf" 2>/dev/null; then
        echo "dev.conf does not target hexa-five lab WG server (192.168.10.201)" >&2
        return 1
    fi
    if ! grep -qE "remote 192\.168\.10\.202|remote=192\.168\.10\.202" "$dest_dir/dev.ovpn" 2>/dev/null; then
        echo "dev.ovpn does not target hexa-five lab OpenVPN server (192.168.10.202)" >&2
        return 1
    fi

    return 0
}

cleanup_stale_graph_interfaces() {
    local iface
    while read -r iface; do
        [ -n "$iface" ] || continue
        ip link del "$iface" 2>/dev/null || true
    done < <(ip -o link show 2>/dev/null | grep -oE 'wg-vpn_[^: ]+|ovpn-[^: ]+' || true)
}

has_gstreamer_binary() {
    local bin="$1"
    ldd "$bin" 2>/dev/null | grep -q gstreamer
}

seed_persisted_config() {
    local dest="$1"
    local example="$2"
    local project_dir="$3"
    mkdir -p "$(dirname "$dest")"
    if [ -f "$dest" ]; then
        return 0
    fi

    local sock="${ODC_SOCKET:-/run/odc-test/odc-onboard.sock}"
    cat > "$dest" <<EOF
version: 2
general:
  log_level: info
  socket: ${sock}
components: []
links: []
triggers: []
EOF
    return 0
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
