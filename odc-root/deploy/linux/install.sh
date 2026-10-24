#!/bin/bash
# ── odc-hook Linux Install Script ──────────────────────────────────────────────
set -euo pipefail

BINARY="odc-hook"
CONFIG="odc-hook.yaml"
SERVICE="odc-hook.service"

PREFIX_BIN="${PREFIX_BIN:-/usr/bin}"
PREFIX_ETC="${PREFIX_ETC:-/etc/odc-root}"
PREFIX_SYSTEMD="${PREFIX_SYSTEMD:-/etc/systemd/system}"
HOOK_USER="${HOOK_USER:-odc-root}"

echo "=== Installing odc-hook (Linux) ==="

echo "  Binary: ${PREFIX_BIN}/${BINARY}"
install -m 755 "${BINARY}" "${PREFIX_BIN}/${BINARY}"

echo "  Config: ${PREFIX_ETC}/${CONFIG}"
mkdir -p "${PREFIX_ETC}/hook" "${PREFIX_ETC}/ca"
install -m 644 "${CONFIG}" "${PREFIX_ETC}/${CONFIG}"

if [ -f "${SERVICE}" ]; then
    echo "  Service: ${PREFIX_SYSTEMD}/${SERVICE}"
    install -m 644 "${SERVICE}" "${PREFIX_SYSTEMD}/${SERVICE}"
    id -u "${HOOK_USER}" >/dev/null 2>&1 || useradd -r -s /sbin/nologin -d /nonexistent "${HOOK_USER}"
    systemctl daemon-reload
    echo "  Service installed. Enable and start with:"
    echo "    systemctl enable --now odc-hook"
else
    echo "  No systemd service file found — skipping service setup"
fi

echo "=== odc-hook installation complete ==="
