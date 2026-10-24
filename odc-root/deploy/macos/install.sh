#!/bin/bash
# ── odc-hook macOS Install Script ──────────────────────────────────────────────
set -euo pipefail

BINARY="odc-hook"
CONFIG="odc-hook.yaml"
PLIST="com.odc.hook.plist"

PREFIX_BIN="${PREFIX_BIN:-/usr/local/bin}"
PREFIX_ETC="${PREFIX_ETC:-/usr/local/etc/odc-root}"
PREFIX_LAUNCHD="${PREFIX_LAUNCHD:-/Library/LaunchDaemons}"
LOG_DIR="${LOG_DIR:-/usr/local/var/log}"

echo "=== Installing odc-hook (macOS) ==="

echo "  Binary: ${PREFIX_BIN}/${BINARY}"
install -m 755 "${BINARY}" "${PREFIX_BIN}/${BINARY}"

echo "  Config: ${PREFIX_ETC}/${CONFIG}"
mkdir -p "${PREFIX_ETC}/hook" "${PREFIX_ETC}/ca"
install -m 644 "${CONFIG}" "${PREFIX_ETC}/${CONFIG}"

echo "  Log dir: ${LOG_DIR}"
mkdir -p "${LOG_DIR}"

if [ -f "${PLIST}" ]; then
    echo "  LaunchDaemon: ${PREFIX_LAUNCHD}/${PLIST}"
    install -m 644 "${PLIST}" "${PREFIX_LAUNCHD}/${PLIST}"
    launchctl load "${PREFIX_LAUNCHD}/${PLIST}" 2>/dev/null || true
    echo "  Service installed. Check status: launchctl list com.odc.hook"
else
    echo "  No launchd plist found — skipping service setup"
    echo "  Run manually: ${PREFIX_BIN}/${BINARY} --config ${PREFIX_ETC}/${CONFIG}"
fi

echo "=== odc-hook installation complete ==="
