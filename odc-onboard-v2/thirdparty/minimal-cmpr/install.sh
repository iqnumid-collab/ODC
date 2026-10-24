#!/usr/bin/env bash
# ============================================================
# HexaRtspCompressor — Production Installer
# ============================================================
# Installs the compiled binary, config templates, and systemd
# service. The service is installed DISABLED — the user must
# edit the config files and then start it manually.
#
# Usage:
#   sudo ./install.sh              # install
#   sudo ./install.sh --uninstall  # remove
# ============================================================
set -euo pipefail

PREFIX="/opt/hexa"
SYSTEMD_DIR="/etc/systemd/system"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[+]${NC} $*"; }
warn()  { echo -e "${YELLOW}[!]${NC} $*"; }
error() { echo -e "${RED}[✗]${NC} $*"; exit 1; }

# ── Uninstall ────────────────────────────────────────────────
if [[ "${1:-}" == "--uninstall" ]]; then
    info "Stopping service..."
    systemctl stop hexa-bridge 2>/dev/null || true
    systemctl disable hexa-bridge 2>/dev/null || true
    rm -f "$SYSTEMD_DIR/hexa-bridge.service"
    systemctl daemon-reload
    info "Removing $PREFIX..."
    rm -rf "$PREFIX"
    info "Done. User 'hexa' preserved (remove with: sudo userdel -r hexa)"
    exit 0
fi

# ── Checks ───────────────────────────────────────────────────
[[ $EUID -eq 0 ]] || error "Run as root: sudo $0"
[[ -f "$PROJECT_DIR/build/HexaRtspCompressor" ]] || \
    error "Binary not found. Build first:\n  mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j\$(nproc)"

# ── Dependencies ─────────────────────────────────────────────
info "Checking runtime dependencies..."
MISSING=()
for cmd in gst-launch-1.0; do
    command -v "$cmd" &>/dev/null || MISSING+=("$cmd")
done
if [[ ${#MISSING[@]} -gt 0 ]]; then
    warn "Missing: ${MISSING[*]}"
    warn "Install with: sudo apt install gstreamer1.0-tools gstreamer1.0-plugins-good \\"
    warn "  gstreamer1.0-plugins-bad gstreamer1.0-rtsp libgstrtspserver-1.0-0 \\"
    warn "  gstreamer1.0-vaapi"
fi

# ── System user ──────────────────────────────────────────────
if ! id hexa &>/dev/null; then
    info "Creating system user 'hexa'..."
    useradd --system --shell /usr/sbin/nologin --home-dir "$PREFIX" \
            --create-home --groups video,render hexa 2>/dev/null || \
    useradd --system --shell /usr/sbin/nologin --home-dir "$PREFIX" \
            --create-home hexa
else
    info "User 'hexa' exists"
fi

# ── Directory layout ─────────────────────────────────────────
info "Installing to $PREFIX..."
install -d -o hexa -g hexa "$PREFIX"/{bin,config}

# Binary
install -m 755 "$PROJECT_DIR/build/HexaRtspCompressor" "$PREFIX/bin/"
info "  bin/HexaRtspCompressor"

# Config templates (never overwrite existing configs)
for tpl in bridge.json; do
    dest="$PREFIX/config/$tpl"
    src="$PROJECT_DIR/config/${tpl}.template"
    if [[ ! -f "$dest" ]]; then
        install -m 640 -o hexa -g hexa "$src" "$dest"
        info "  config/$tpl (template — edit before starting)"
    else
        warn "  config/$tpl exists — not overwritten"
    fi
done

# README for the operator
cat > "$PREFIX/README" << 'HEREDOC'
HexaRtspCompressor — RTSP Bridge Service
=========================================

Before starting the service, edit the config file:

  1. Set your RTSP source:
     sudo -u hexa nano /opt/hexa/config/bridge.json
     → Set "input_uri" to your camera's RTSP URL
     → Adjust output codec, resolution, bitrate as needed
     → Set "output_username"/"output_password" for auth (or leave empty)

  2. Start the service:
     sudo systemctl start hexa-bridge

  3. Enable on boot:
     sudo systemctl enable hexa-bridge

  4. Check status / logs:
     sudo systemctl status hexa-bridge
     journalctl -u hexa-bridge -f

  5. Analyse a stream (one-off, no service needed):
     /opt/hexa/bin/HexaRtspCompressor analyse rtsp://camera:554/stream --tcp --duration 10

Full documentation: https://github.com/your-org/HexaRtspCompressor
HEREDOC
chown hexa:hexa "$PREFIX/README"

# ── Systemd service ─────────────────────────────────────────
info "Installing systemd service..."
install -m 644 "$PROJECT_DIR/systemd/hexa-bridge.service" "$SYSTEMD_DIR/"
systemctl daemon-reload

# ── Done (NOT started, NOT enabled) ─────────────────────────
echo ""
info "Installation complete."
echo ""
echo "  Installed:"
echo "    $PREFIX/bin/HexaRtspCompressor"
echo "    $PREFIX/config/bridge.json    ← EDIT THIS"
echo "    $SYSTEMD_DIR/hexa-bridge.service"
echo ""
echo -e "  ${YELLOW}Next steps:${NC}"
echo "    1. Edit the config:    sudo -u hexa nano $PREFIX/config/bridge.json"
echo "       → Set \"input_uri\" to your RTSP source"
echo "    2. Start the service:  sudo systemctl start hexa-bridge"
echo "    3. Enable on boot:     sudo systemctl enable hexa-bridge"
echo "    4. View logs:          journalctl -u hexa-bridge -f"
echo ""
echo "  Quick test (analyse any stream without starting the service):"
echo "    $PREFIX/bin/HexaRtspCompressor analyse rtsp://camera:554/stream --tcp --duration 10"
