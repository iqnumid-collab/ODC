#!/usr/bin/env bash
# ================================================================
# RTSP Loop Server for mercedes.mp4 – using MediaMTX + on-demand FFmpeg
# ================================================================
# Features:
# • Infinite loop of mercedes.mp4 as RTSP
# • On-demand: FFmpeg starts only on first client connection
# • No re-encoding (copy) → low CPU, original quality
# • Auto-downloads latest MediaMTX if not present
# • Listens on :8554
#
# Usage:
#   1. Place script + mercedes.mp4 in same folder
#   2. chmod +x this_script.sh
#   3. ./this_script.sh
#   4. Open in player:
#      rtsp://YOUR_IP:8554/mercede    (or rtspt://... for TCP)
#
# Stop: Ctrl+C
# ================================================================

set -euo pipefail

VIDEO_FILE="mercedes.mp4"
STREAM_PATH="mercede"
RTSP_PORT=8554
RTSP_LOCAL="rtsp://localhost:${RTSP_PORT}/${STREAM_PATH}"

# ─────────────────────────────── Checks ───────────────────────────────
if [[ ! -f "${VIDEO_FILE}" ]]; then
    echo "Error: '${VIDEO_FILE}' not found!"
    echo "   Place the video file in this directory."
    exit 1
fi

if ! command -v ffmpeg &> /dev/null; then
    echo "ffmpeg not installed."
    echo "   → sudo apt install ffmpeg   (or your package manager)"
    exit 1
fi

# ─────────────────────── Download MediaMTX if missing ───────────────────────
if [[ ! -x ./mediamtx && ! $(command -v mediamtx) ]]; then
    echo "Downloading latest MediaMTX..."

    ARCH=$(uname -m)
    case "${ARCH}" in
        x86_64|amd64)   ARCH="amd64" ;;
        aarch64|arm64*) ARCH="arm64v8" ;;
        armv7l|armv7*)  ARCH="armv7" ;;
        *) echo "Unsupported arch: ${ARCH} → manual download needed"; exit 1 ;;
    esac

    VERSION=$(curl -s https://api.github.com/repos/bluenviron/mediamtx/releases/latest \
        | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/' | sed 's/^v//')

    TARBALL="mediamtx_v${VERSION}_linux_${ARCH}.tar.gz"

    echo "   Downloading: ${TARBALL}"
    curl -L --progress-bar -o "${TARBALL}" \
        "https://github.com/bluenviron/mediamtx/releases/download/v${VERSION}/${TARBALL}"

    tar -xzf "${TARBALL}"
    chmod +x mediamtx
    rm -f "${TARBALL}" *.example *.md 2>/dev/null || true

    echo "MediaMTX v${VERSION} ready"
fi

# ─────────────────────── Minimal modern config ───────────────────────
cat > mediamtx.yml << EOF
# Auto-generated – compatible with MediaMTX v1.10+ (2025–2026)

logLevel: info

# RTSP listener on port ${RTSP_PORT} (TCP/UDP both allowed by default)
rtspAddress: :${RTSP_PORT}

# Single on-demand path (ffmpeg starts when client connects)
paths:
  ${STREAM_PATH}:
    runOnDemand: >-
      ffmpeg -re -stream_loop -1 -i ${VIDEO_FILE}
      -c copy -rtsp_transport tcp -f rtsp
      rtsp://0.0.0.0:\$RTSP_PORT/\$MTX_PATH
    runOnDemandRestart: yes
    runOnDemandCloseAfter: 15s
EOF

echo "Config created (TCP preferred via client / env var)"

# ─────────────────────── Start with TCP preference ───────────────────────
echo ""
echo "Launching MediaMTX RTSP loop server..."
echo "   Local:     ${RTSP_LOCAL}"
echo "   Network:   rtsp://$(hostname -I | awk '{print $1}'):${RTSP_PORT}/${STREAM_PATH}"
echo "              (use rtspt://... in VLC/ffplay for forced TCP)"
echo ""
echo "   → Test with:   ffplay -rtsp_transport tcp rtsp://localhost:8554/mercede"
echo "   → Video loops forever, near-zero CPU when idle"
echo ""
echo "   Ctrl+C to stop"

# Prefer local binary + force TCP transport via env
if [[ -x ./mediamtx ]]; then
    MTX_RTSPTRANSPORTS=tcp exec ./mediamtx
else
    MTX_RTSPTRANSPORTS=tcp exec mediamtx
fi
