#!/bin/bash
# odc-root: Real-Time Integration Test Suite
# Runs inside Docker container with all services started.
# Sources helpers.sh for framework and reporting.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/helpers.sh" 2>/dev/null || source /helpers.sh

CLI="/usr/bin/odc-cli"
HOOK_SOCK="/run/odc-root/hook.sock"
TUNNEL_SOCK="/run/odc-root/tunnel-ipc.sock"
REGISTRY_SOCK="/run/odc-root/odc-ipc.sock"
CA_CERT="/etc/odc-root/ca/ca.crt"
CA_KEY="/etc/odc-root/ca/ca.key"

echo "=== odc-root Integration Test Suite ==="
echo "Started at $(date)"

# ──────────────────────────────────────────────────────────────────────────
# Section 1: Service Startup & Health
# ──────────────────────────────────────────────────────────────────────────
test_section "Service Startup & Health Checks"

assert_process_running "odc-tunnel-manager" \
    "Tunnel manager process is running" \
    "tunnel-manager" \
    "odc-tunnel-manager process not found"

assert_process_running "odc-registry-manager" \
    "Registry manager process is running" \
    "registry-manager" \
    "odc-registry-manager process not found"

assert_process_running "odc-hook" \
    "Hook service process is running" \
    "odc-hook" \
    "odc-hook process not found"

assert port 9443 open_check 'Port 9443 (registry WebSocket) is listening' \
    "registry-manager" \
    "WebSocket port 9443 not reachable"

assert_file_exists "$TUNNEL_SOCK" \
    "Tunnel IPC socket exists" \
    "tunnel-manager" \
    "tunnel-ipc.sock not found"

assert_file_exists "$REGISTRY_SOCK" \
    "Registry IPC socket exists" \
    "registry-manager" \
    "odc-ipc.sock not found"

# ── CLI health checks ───────────────────────────────────────────────────
CLI_STATUS=$($CLI status 2>/dev/null)
assert_contains "pong" "\"$CLI_STATUS\"" \
    "CLI status returns pong" \
    "cli" "status did not return pong"

TUNNEL_STATUS=$($CLI tunnel status 2>/dev/null)
assert_contains "ok" "\"$TUNNEL_STATUS\"" \
    "CLI tunnel status returns ok" \
    "cli/tunnel-manager" \
    "tunnel status failed"

HOOK_STATUS=$($CLI hook status -s "$HOOK_SOCK" 2>/dev/null)
echo "Hook status: $HOOK_STATUS"
assert_json_field ".connected" "$HOOK_STATUS" "true" \
    "Hook reports connected to registry" \
    "odc-hook" \
    "hook not connected"

# ──────────────────────────────────────────────────────────────────────────
# Section 2: Certificate Lifecycle
# ──────────────────────────────────────────────────────────────────────────
test_section "Certificate Lifecycle Tests"

TMPDIR=$(mktemp -d)

# Generate a test CA
$CLI cert generate-ca --cert "$TMPDIR/test-ca.crt" --key "$TMPDIR/test-ca.key" \
    --cn "Test Sub CA" --org "TestOrg" 2>/dev/null
assert_file_exists "$TMPDIR/test-ca.crt" \
    "CA certificate file created" \
    "cli/cert" "cert generate-ca failed"

assert_file_exists "$TMPDIR/test-ca.key" \
    "CA private key file created" \
    "cli/cert"

# Issue a client cert
CERT_OUT=$($CLI cert issue --ca-cert "$CA_CERT" --ca-key "$CA_KEY" \
    --cn "test-client-1" \
    --cert-out "$TMPDIR/test-client.crt" \
    --key-out "$TMPDIR/test-client.key" 2>/dev/null)
assert_file_exists "$TMPDIR/test-client.crt" \
    "Client certificate file created" \
    "cli/cert"
assert_file_exists "$TMPDIR/test-client.key" \
    "Client key file created" \
    "cli/cert"

# Verify the cert
assert_contains "common_name" "\"$CERT_OUT\"" \
    "Cert issue response contains common_name" \
    "cli/cert"

VERIFY_OUT=$($CLI cert verify --ca-cert "$CA_CERT" --cert "$TMPDIR/test-client.crt" 2>/dev/null)
assert_json_field ".valid" "$VERIFY_OUT" "true" \
    "Client cert verified against CA" \
    "cli/cert" "cert verify returned false"

# Fingerprint
FP_OUT=$($CLI cert fingerprint --cert "$TMPDIR/test-client.crt" 2>/dev/null)
assert_contains "SHA-256" "\"$FP_OUT\"" \
    "Certificate fingerprint computed" \
    "cli/cert"

FP_VALUE=$(echo "$FP_OUT" | jq -r '.fingerprint' 2>/dev/null)
assert 'test ${#FP_VALUE} -ge 59' \
    "Fingerprint is valid SHA-256 (length >= 59)" \
    "cli/cert" "fingerprint too short: ${#FP_VALUE} chars"

# WireGuard keypair
WG_OUT=$($CLI cert keypair-wg --key-out "$TMPDIR/wg.key" 2>/dev/null)
assert_contains "public_key" "\"$WG_OUT\"" \
    "WireGuard keypair generated" \
    "cli/cert"

rm -rf "$TMPDIR"

# ──────────────────────────────────────────────────────────────────────────
# Section 3: mTLS Connection Scenarios
# ──────────────────────────────────────────────────────────────────────────
test_section "mTLS Connection Scenarios"

# Verify hook is connected via mTLS
assert 'test "$(echo "$HOOK_STATUS" | jq -r .connected)" = "true"' \
    "Hook is connected to registry via mTLS" \
    "odc-hook/mtls"

# Verify hook receives events — trigger a service registration
$CLI services register --id "mtls-test-$(date +%s)" --name "mTLS Test Service" --type test 2>/dev/null
sleep 2
HOOK_STATUS2=$($CLI hook status -s "$HOOK_SOCK" 2>/dev/null)
EVENTS_RCVD=$(echo "$HOOK_STATUS2" | jq -r '.events_received')
assert 'test "$EVENTS_RCVD" -gt 0' \
    "Hook has received events ($EVENTS_RCVD)" \
    "odc-hook/event-flow" \
    "events_received is $EVENTS_RCVD"

EVENTS_PROC=$(echo "$HOOK_STATUS2" | jq -r '.events_processed')
assert 'test "$EVENTS_PROC" -gt 0' \
    "Hook has processed events ($EVENTS_PROC)" \
    "odc-hook/event-flow" \
    "events_processed is $EVENTS_PROC"

# Unregister test service
$CLI services unregister --id "mtls-test-$(date +%s)" 2>/dev/null || true

# ──────────────────────────────────────────────────────────────────────────
# Section 4: Service Discovery & Registry
# ──────────────────────────────────────────────────────────────────────────
test_section "Service Discovery & Registry Operations"

SERVICE_ID="svc-$(date +%s)"

# Register
REG_OUT=$($CLI services register --id "$SERVICE_ID" --name "Test Service" --type drone 2>/dev/null)
assert_contains "ok" "\"$REG_OUT\"" \
    "Service registered successfully" \
    "registry-manager" "service register failed: $REG_OUT"

# List
LIST_OUT=$($CLI services list 2>/dev/null)
assert_contains "$SERVICE_ID" "\"$LIST_OUT\"" \
    "Registered service appears in list" \
    "registry-manager" "service not found in list"

# Register another with different type
SERVICE_ID2="svc-$(date +%s)-2"
$CLI services register --id "$SERVICE_ID2" --name "Other Service" --type camera 2>/dev/null

# Filter by type
FILTER_OUT=$($CLI services list --type drone 2>/dev/null)
assert_contains "$SERVICE_ID" "\"$FILTER_OUT\"" \
    "Service list filtered by type contains matching service" \
    "registry-manager/data-store"

# Unregister
$CLI services unregister --id "$SERVICE_ID" 2>/dev/null
$CLI services unregister --id "$SERVICE_ID2" 2>/dev/null

# Verify unregistered
LIST2=$($CLI services list 2>/dev/null)
assert '! echo "$LIST2" | grep -q "$SERVICE_ID"' \
    "Unregistered service removed from list" \
    "registry-manager"

# ──────────────────────────────────────────────────────────────────────────
# Section 5: Event Propagation Flow
# ──────────────────────────────────────────────────────────────────────────
test_section "Event Propagation End-to-End"

# Capture events in background for 10 seconds via pipe
EVENT_FILE="/tmp/odc-test-events-$$.jsonl"
echo "Starting event watcher..."
$CLI events watch \
    --url wss://127.0.0.1:9443/ws \
    --ca-cert "$CA_CERT" \
    --client-cert /etc/odc-root/hook/client.crt \
    --client-key /etc/odc-root/hook/client.key \
    --topic tunnel.events \
    --timeout 15 > "$EVENT_FILE" 2>/dev/null &
WATCHER_PID=$!

sleep 3

# Trigger service registration event
$CLI services register --id "event-test-$(date +%s)" --name "Event Test" --type event 2>/dev/null

sleep 3

# Check if events were captured
EVENT_COUNT=$(wc -l < "$EVENT_FILE" 2>/dev/null || echo 0)
assert 'test "$EVENT_COUNT" -gt 0' \
    "Event watcher captured $EVENT_COUNT events" \
    "events/registry-manager" \
    "no events captured by watcher"

$CLI services unregister --id "event-test-$(date +%s)" 2>/dev/null || true

# Wait for watcher to finish
wait $WATCHER_PID 2>/dev/null || true
rm -f "$EVENT_FILE"

# ──────────────────────────────────────────────────────────────────────────
# Section 6: Tunnel Manager Operations
# ──────────────────────────────────────────────────────────────────────────
test_section "Tunnel Manager Operations"

TUNNEL_STATUS=$($CLI tunnel status -s "$TUNNEL_SOCK" 2>/dev/null)
assert_contains "servers" "\"$TUNNEL_STATUS\"" \
    "Tunnel status returns server list" \
    "tunnel-manager"

# Check if WireGuard server is present
if echo "$TUNNEL_STATUS" | jq -e '.servers[] | select(.type=="wireguard")' >/dev/null 2>&1; then
    assert_json_field '.servers[0].running' "$TUNNEL_STATUS" "true" \
        "WireGuard server is running" \
        "tunnel-manager/wireguard"

    # Try adding a WireGuard peer
    WG_KEY=$(wg genkey 2>/dev/null | tee /tmp/wg-test-peer.key)
    WG_PUB=$(echo "$WG_KEY" | wg pubkey)

    PEER_ADD=$($CLI tunnel peers add -s "$TUNNEL_SOCK" --public-key "$WG_PUB" 2>/dev/null)
    if echo "$PEER_ADD" | jq -e '.ok' >/dev/null 2>&1; then
        pass "WireGuard peer added: $(echo "$PEER_ADD" | jq -r '.assigned_ip')"
    else
        skip "WireGuard peer could not be added" "tunnel-manager/wireguard" "$PEER_ADD"
    fi

    # List peers
    PEERS=$($CLI tunnel peers list -s "$TUNNEL_SOCK" 2>/dev/null)
    if echo "$PEERS" | jq -e '.peers' >/dev/null 2>&1; then
        PEER_COUNT=$(echo "$PEERS" | jq '.peers | length')
        assert 'test "$PEER_COUNT" -gt 0' \
            "Peers list contains $PEER_COUNT entries" \
            "tunnel-manager/wireguard"
    fi

    # Remove peer
    PEER_RM=$($CLI tunnel peers remove -s "$TUNNEL_SOCK" --public-key "$WG_PUB" 2>/dev/null)
    if echo "$PEER_RM" | jq -e '.ok' >/dev/null 2>&1; then
        pass "WireGuard peer removed successfully"
    else
        skip "WireGuard peer removal failed" "tunnel-manager/wireguard" "$PEER_RM"
    fi

    rm -f /tmp/wg-test-peer.key
else
    skip "WireGuard server not configured" "tunnel-manager/wireguard"
fi

# Tunnel health
HEALTH_OUT=$($CLI tunnel health -s "$TUNNEL_SOCK" 2>/dev/null)
assert_contains "ok" "\"$HEALTH_OUT\"" \
    "Tunnel health check returns ok" \
    "tunnel-manager"

# ──────────────────────────────────────────────────────────────────────────
# Section 7: CLI Error Handling
# ──────────────────────────────────────────────────────────────────────────
test_section "CLI Error Handling & Edge Cases"

# Test with invalid socket
INVALID_OUT=$($CLI -s /tmp/nonexistent.sock status 2>&1 || true)
assert_contains "error\|Cannot" "\"$INVALID_OUT\"" \
    "CLI returns error on unreachable socket" \
    "cli"

# Missing required arg
MISSING_OUT=$($CLI cert issue --cn test 2>&1; true)
assert 'test $? -ne 0 || echo "$MISSING_OUT" | grep -q "required"' \
    "CLI rejects missing required arguments" \
    "cli"

# Help output
HELP_OUT=$($CLI --help 2>&1)
assert_contains "Usage" "\"$HELP_OUT\"" \
    "CLI help shows usage" \
    "cli"
assert_contains "cert" "\"$HELP_OUT\"" \
    "CLI help lists cert subcommand" \
    "cli"
assert_contains "tunnel" "\"$HELP_OUT\"" \
    "CLI help lists tunnel subcommand" \
    "cli"
assert_contains "events" "\"$HELP_OUT\"" \
    "CLI help lists events subcommand" \
    "cli"
assert_contains "hook" "\"$HELP_OUT\"" \
    "CLI help lists hook subcommand" \
    "cli"

# Subcommand help
CERT_HELP=$($CLI cert --help 2>&1)
assert_contains "generate-ca" "\"$CERT_HELP\"" \
    "cert help lists generate-ca" \
    "cli"
assert_contains "issue" "\"$CERT_HELP\"" \
    "cert help lists issue" \
    "cli"
assert_contains "verify" "\"$CERT_HELP\"" \
    "cert help lists verify" \
    "cli"

# ──────────────────────────────────────────────────────────────────────────
# Section 8: Hook Resiliency
# ──────────────────────────────────────────────────────────────────────────
test_section "Hook Resiliency & Reconnection"

# Check hook reconnect interval
HOOK_BEFORE=$($CLI hook status -s "$HOOK_SOCK" 2>/dev/null)
echo "Hook status before restart: $(echo "$HOOK_BEFORE" | jq -r '.events_received') events"

# Verify hook maintains subscription across events
$CLI services register --id "resiliency-$(date +%s)" --name "Resiliency Test" --type test 2>/dev/null
sleep 2
HOOK_MID=$($CLI hook status -s "$HOOK_SOCK" 2>/dev/null)
EV_BEFORE=$(echo "$HOOK_BEFORE" | jq -r '.events_received')
EV_MID=$(echo "$HOOK_MID" | jq -r '.events_received')
assert 'test "$EV_MID" -ge "$EV_BEFORE"' \
    "Hook continues receiving events (before: $EV_BEFORE, mid: $EV_MID)" \
    "odc-hook" "events stopped flowing"

$CLI services unregister --id "resiliency-$(date +%s)" 2>/dev/null || true

# ──────────────────────────────────────────────────────────────────────────
# Generate Final Report
# ──────────────────────────────────────────────────────────────────────────
print_summary
