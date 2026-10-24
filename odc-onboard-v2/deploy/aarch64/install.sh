#!/usr/bin/env bash
# Install odc-onboard-v2 package on a Raspberry Pi (aarch64).
# Layout matches docs-topology/hosts/odc-device-rpi-*.md
set -euo pipefail

PKG_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="${ODC_BIN_DIR:-/opt/odc/bin}"
ETC_DIR="${ODC_ETC_DIR:-/etc/odc-onboard}"
UNIT_NAME="odc-onboard-v2"
UNIT_DST="/etc/systemd/system/${UNIT_NAME}.service"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "ERROR: run as root (sudo ./install.sh)" >&2
  exit 1
fi

install -d -m 0755 "$BIN_DIR" "$ETC_DIR/vpn" "$ETC_DIR/mavlink/devices" /var/log/odc-onboard /run/odc-onboard

install -m 0755 "$PKG_DIR/odc-onboard-v2" "$BIN_DIR/odc-onboard-v2"
ln -sfn odc-onboard-v2 "$BIN_DIR/odc-cli"

if [[ -x "$PKG_DIR/tools/odc-video-source" ]]; then
  install -m 0755 "$PKG_DIR/tools/odc-video-source" "$BIN_DIR/odc-video-source"
fi
if [[ -x "$PKG_DIR/tools/odc-rtsp-monitor" ]]; then
  install -m 0755 "$PKG_DIR/tools/odc-rtsp-monitor" "$BIN_DIR/odc-rtsp-monitor"
fi

if [[ -f "$PKG_DIR/config/system.yaml" ]]; then
  install -m 0600 "$PKG_DIR/config/system.yaml" "$ETC_DIR/system.yaml"
elif [[ -f "$PKG_DIR/config/system.yaml.template" ]] && [[ ! -f "$ETC_DIR/system.yaml" ]]; then
  install -m 0600 "$PKG_DIR/config/system.yaml.template" "$ETC_DIR/system.yaml"
fi

if [[ -d "$PKG_DIR/config/vpn" ]]; then
  cp -a "$PKG_DIR/config/vpn/." "$ETC_DIR/vpn/"
  chmod 600 "$ETC_DIR/vpn/"* 2>/dev/null || true
fi

UNIT_SRC="$PKG_DIR/config/odc-onboard-v2.service"
[[ -f "$UNIT_SRC" ]] || UNIT_SRC="$PKG_DIR/odc-onboard-v2.service"
install -m 0644 "$UNIT_SRC" "$UNIT_DST"

systemctl daemon-reload
systemctl enable "$UNIT_NAME"
systemctl restart "$UNIT_NAME"

echo "Installed $BIN_DIR/odc-onboard-v2"
echo "Config:   $ETC_DIR/system.yaml"
echo "Service:  systemctl status $UNIT_NAME"
systemctl --no-pager --full status "$UNIT_NAME" || true
