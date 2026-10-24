#!/bin/bash
# odc-root Integration Test — entrypoint.sh
# Starts all services inside the Docker container and runs the test suite.

set -euo pipefail

echo "=== odc-root Integration Test Entrypoint ==="
echo "Starting at $(date -Iseconds)"

# ── Runtime directories ──────────────────────────────────────────────────
mkdir -p /run/odc-root /var/lib/odc-root /var/log/odc-root
mkdir -p /etc/odc-root/{ca,hook,ws,wg,ovpn}

# ── Generate test certificates ───────────────────────────────────────────
echo "--- Generating test certificates ---"
/usr/bin/odc-cli cert generate-ca \
    --cert /etc/odc-root/ca/ca.crt \
    --key /etc/odc-root/ca/ca.key \
    --org "ODC-Test" \
    --cn "odc-root Test CA" \
    --country "XX" \
    --days 30

/usr/bin/odc-cli cert issue \
    --ca-cert /etc/odc-root/ca/ca.crt \
    --ca-key /etc/odc-root/ca/ca.key \
    --cn odc-registry \
    --cert-out /etc/odc-root/ws/server.crt \
    --key-out /etc/odc-root/ws/server.key

/usr/bin/odc-cli cert issue \
    --ca-cert /etc/odc-root/ca/ca.crt \
    --ca-key /etc/odc-root/ca/ca.key \
    --cn odc-hook \
    --cert-out /etc/odc-root/hook/client.crt \
    --key-out /etc/odc-root/hook/client.key

# Copy certs for the test report
cp /etc/odc-root/ca/ca.crt /tmp/odc-test-ca.crt
cp /etc/odc-root/hook/client.crt /tmp/odc-test-client.crt
cp /etc/odc-root/hook/client.key /tmp/odc-test-client.key

# Generate WireGuard key for tunnel-manager
/usr/bin/odc-cli cert keypair-wg --key-out /etc/odc-root/wg/server.key
chmod 600 /etc/odc-root/wg/server.key

echo "--- Certificates generated ---"

# ── Install configs ──────────────────────────────────────────────────────
if [ -f /src/odc-root/deploy/test/configs/tunnel-manager.yaml ]; then
    cp /src/odc-root/deploy/test/configs/tunnel-manager.yaml /etc/odc-root/tunnel-manager.yaml
else
    cp /src/odc-root/tunnel-manager/config/tunnel-manager.yaml /etc/odc-root/tunnel-manager.yaml
fi

if [ -f /src/odc-root/deploy/test/configs/registry-manager.yaml ]; then
    cp /src/odc-root/deploy/test/configs/registry-manager.yaml /etc/odc-root/registry-manager.yaml
else
    cp /src/odc-root/registry-manager/config/registry-manager.yaml /etc/odc-root/registry-manager.yaml
fi

if [ -f /src/odc-root/deploy/test/configs/odc-hook.yaml ]; then
    cp /src/odc-root/deploy/test/configs/odc-hook.yaml /etc/odc-root/odc-hook.yaml
else
    cp /src/odc-root/odc-hook/config/odc-hook.yaml /etc/odc-root/odc-hook.yaml
fi

# ── Check WireGuard availability ─────────────────────────────────────────
if ip link add wg-test-check type wireguard 2>/dev/null; then
    ip link del wg-test-check 2>/dev/null || true
    echo "--- WireGuard kernel module available ---"
else
    echo "--- WireGuard NOT available — VPN tests will be skipped ---"
fi

# ── Start services ───────────────────────────────────────────────────────
echo "--- Starting odc-tunnel-manager ---"
/usr/bin/odc-tunnel-manager --config /etc/odc-root/tunnel-manager.yaml --daemon &
TUNNEL_PID=$!

echo "--- Waiting for tunnel IPC socket ---"
wait_for_file /run/odc-root/tunnel-ipc.sock 15 || echo "WARNING: tunnel IPC socket not ready"

echo "--- Starting odc-registry-manager ---"
/usr/bin/odc-registry-manager --config /etc/odc-root/registry-manager.yaml --daemon &
REGISTRY_PID=$!

echo "--- Waiting for registry WS port (9443) ---"
wait_for_port 9443 15 || echo "WARNING: registry WS port not ready"

echo "--- Starting odc-hook ---"
/usr/bin/odc-hook --config /etc/odc-root/odc-hook.yaml --daemon &
HOOK_PID=$!

echo "--- Waiting for hook to connect ---"
sleep 5

# ── Run test suite ───────────────────────────────────────────────────────
echo "=== Running integration test suite ==="
set +e
cd /src/odc-root
bash /test_suite.sh
TEST_EXIT=$?
set -e

# ── Collect results ──────────────────────────────────────────────────────
echo "=== Test suite exit code: $TEST_EXIT ==="

# Copy certs for post-mortem
mkdir -p /output/certs
cp /etc/odc-root/ca/ca.crt /output/certs/ 2>/dev/null || true
cp /etc/odc-root/hook/client.crt /output/certs/ 2>/dev/null || true
cp /etc/odc-root/hook/client.key /output/certs/ 2>/dev/null || true

# ── Stop services ────────────────────────────────────────────────────────
echo "--- Stopping services ---"
kill $HOOK_PID 2>/dev/null || true
kill $REGISTRY_PID 2>/dev/null || true
kill $TUNNEL_PID 2>/dev/null || true
sleep 2

# Force kill any remaining
kill -9 $HOOK_PID $REGISTRY_PID $TUNNEL_PID 2>/dev/null || true

echo "=== Integration test complete ==="
exit $TEST_EXIT
