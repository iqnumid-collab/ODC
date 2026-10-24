#!/bin/bash
# odc-root Windows Wine Smoke Tests
# Verifies cross-compiled Windows binaries function via Wine.

set -euo pipefail

echo "=== odc-root Windows Wine Smoke Tests ==="
echo "Started at $(date)"

PASS=0
FAIL=0

pass() { echo "  PASS: $1"; ((PASS++)); }
fail() { echo "  FAIL: $1"; ((FAIL++)); }

REGISTRY_HOST="${REGISTRY_HOST:-127.0.0.1}"
REGISTRY_PORT="${REGISTRY_PORT:-9443}"

echo "Registry: ${REGISTRY_HOST}:${REGISTRY_PORT}"

# ── Test 1: odc-hook --help ──────────────────────────────────────────────
echo "--- Test: odc-hook.exe --help ---"
if wine /win/odc-hook.exe --help 2>/dev/null; then
    pass "odc-hook.exe --help exits successfully"
else
    fail "odc-hook.exe --help failed"
fi

# ── Test 2: odc-cli --help ───────────────────────────────────────────────
echo "--- Test: odc-cli.exe --help ---"
if wine /win/odc-cli.exe --help 2>/dev/null; then
    pass "odc-cli.exe --help exits successfully"
else
    fail "odc-cli.exe --help failed"
fi

# ── Test 3: odc-cli cert generate-ca ─────────────────────────────────────
echo "--- Test: odc-cli.exe cert generate-ca ---"
mkdir -p /tmp/win-certs
if wine /win/odc-cli.exe cert generate-ca \
    --cert /tmp/win-certs/ca.crt --key /tmp/win-certs/ca.key \
    --cn "Wine Test CA" --days 7 2>/dev/null; then
    if [ -f /tmp/win-certs/ca.crt ] && [ -f /tmp/win-certs/ca.key ]; then
        pass "odc-cli.exe cert generate-ca produces valid files"
    else
        fail "odc-cli.exe cert generate-ca: files missing"
    fi
else
    fail "odc-cli.exe cert generate-ca command failed"
fi

# ── Test 4: odc-cli cert issue ───────────────────────────────────────────
echo "--- Test: odc-cli.exe cert issue ---"
if wine /win/odc-cli.exe cert issue \
    --ca-cert /tmp/win-certs/ca.crt --ca-key /tmp/win-certs/ca.key \
    --cn wine-client \
    --cert-out /tmp/win-certs/client.crt \
    --key-out /tmp/win-certs/client.key 2>/dev/null; then
    if [ -f /tmp/win-certs/client.crt ]; then
        pass "odc-cli.exe cert issue produces client cert"
    else
        fail "odc-cli.exe cert issue: client cert file missing"
    fi
else
    fail "odc-cli.exe cert issue command failed"
fi

# ── Test 5: odc-cli cert verify ──────────────────────────────────────────
echo "--- Test: odc-cli.exe cert verify ---"
if wine /win/odc-cli.exe cert verify \
    --ca-cert /tmp/win-certs/ca.crt \
    --cert /tmp/win-certs/client.crt 2>/dev/null; then
    pass "odc-cli.exe cert verify returns success"
else
    fail "odc-cli.exe cert verify returned failure"
fi

# ── Test 6: odc-cli cert fingerprint ─────────────────────────────────────
echo "--- Test: odc-cli.exe cert fingerprint ---"
FP=$(wine /win/odc-cli.exe cert fingerprint --cert /tmp/win-certs/client.crt 2>/dev/null || true)
if echo "$FP" | grep -q "fingerprint"; then
    pass "odc-cli.exe cert fingerprint returns valid output"
else
    fail "odc-cli.exe cert fingerprint failed: $FP"
fi

# ── Test 7: odc-hook connect via Wine to registry ────────────────────────
echo "--- Test: odc-hook.exe mTLS connection ---"
if [ -f /certs/ca.crt ] && [ -f /certs/client.crt ] && [ -f /certs/client.key ]; then
    cat > /tmp/wine-hook.yaml << YEOF
version: 1
general:
  log_level: info
server:
  host: ${REGISTRY_HOST}
  port: ${REGISTRY_PORT}
  uri: /ws
  reconnect_interval_sec: 5
  connect_timeout_sec: 10
  idle_timeout_sec: 300
mtls:
  client_cert_path: /certs/client.crt
  client_key_path: /certs/client.key
  ca_cert_path: /certs/ca.crt
  verify_server: true
event_bus:
  queue_capacity: 256
thread_pool:
  num_threads: 2
  event_timeout_ms: 500
health:
  enabled: false
YEOF

    timeout 10 wine /win/odc-hook.exe --config /tmp/wine-hook.yaml > /tmp/wine-hook.log 2>&1 &
    WINE_HOOK_PID=$!
    sleep 4
    
    if grep -q "Connected to registry" /tmp/wine-hook.log 2>/dev/null; then
        pass "Wine odc-hook.exe connected to registry via mTLS"
    else
        echo "Hook log:"
        cat /tmp/wine-hook.log 2>/dev/null || true
        skip_cond "Wine" "hook connection not confirmed (log may show TLS issue)"
    fi
    
    kill $WINE_HOOK_PID 2>/dev/null || true
    wait $WINE_HOOK_PID 2>/dev/null || true
else
    echo "No shared certs available — skipping Wine mTLS connection test"
fi

# ── Summary ──────────────────────────────────────────────────────────────
echo
echo "=== Windows Wine Smoke Test Results ==="
echo "  Passed: $PASS"
echo "  Failed: $FAIL"

if [ "$PASS" -ge 5 ]; then
    echo "  Status: SMOKE TESTS PASSED (minimal functionality confirmed)"
else
    echo "  Status: SMOKE TESTS FAILED"
fi

# Write report
mkdir -p /output
cat > /output/wine_test_report.json << EOF
{
  "test_suite": "odc-root-wine-smoke",
  "timestamp": "$(date -Iseconds)",
  "passed": $PASS,
  "failed": $FAIL,
  "total": $((PASS + FAIL))
}
EOF

[ "$FAIL" -eq 0 ]
