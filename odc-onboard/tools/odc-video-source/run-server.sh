#!/usr/bin/env bash
set -euo pipefail

# ─── odc-video-source Multi RTSP Server Launcher ────────────────────────
# Serves 1–4 MP4 videos as looping RTSP streams for local testing.
#
# Default (no flags): launches all 4 streams
#   Stream 1: mercedes.mp4 → rtsp://127.0.0.1:8554/stream
#   Stream 2: Porsche.mp4  → rtsp://127.0.0.1:8555/stream
#   Stream 3: lexus.mp4    → rtsp://127.0.0.1:8556/stream
#   Stream 4: audi.mp4     → rtsp://127.0.0.1:8557/stream
#
# Options:
#   --streams N     Launch N streams (1–4, default: 4)
#   --video <file>  Launch single stream (disables --streams)
#   --port <port>   Base port (default: 8554)
#   --build-only    Build the binary, then exit

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VID_SRC="$SCRIPT_DIR/vid-src"

# Root/monorepo build places the binary beside this script; standalone build uses ./build/
if [[ -x "$SCRIPT_DIR/odc-video-source" ]]; then
    BINARY="$SCRIPT_DIR/odc-video-source"
    BUILD_DIR="$SCRIPT_DIR/build"
else
    BUILD_DIR="$SCRIPT_DIR/build"
    BINARY="$BUILD_DIR/odc-video-source"
fi

# All 4 videos in order
ALL_VIDEOS=(
    "$VID_SRC/mercedes.mp4"
    "$VID_SRC/Porsche.mp4"
    "$VID_SRC/lexus.mp4"
    "$VID_SRC/audi.mp4"
)

BASE_PORT=8554
NUM_STREAMS=4
SINGLE_MODE=false
BUILD_ONLY=false
EXTRA_ARGS=()

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Launch local RTSP servers streaming MP4 files on loop."
    echo ""
    echo "Options:"
    echo "  --streams N       Number of streams to launch (1–4, default: 4)"
    echo "  --video <file>    Single video file (overrides --streams)"
    echo "  --port <port>     Base RTSP port (default: 8554, streams increment from there)"
    echo "  --path <path>     RTSP mount path (default: /stream)"
    echo "  --no-loop         Play once and exit"
    echo "  --bandwidth <kbps> Max bandwidth limit in kbps"
    echo "  --build-only      Build the binary, then exit"
    echo "  -h, --help        Show this help"
    echo ""
    echo "Default (no flags):"
    echo "  4 streams on ports 8554–8557:"
    echo "    mercedes → rtsp://127.0.0.1:8554/stream"
    echo "    Porsche  → rtsp://127.0.0.1:8555/stream"
    echo "    lexus    → rtsp://127.0.0.1:8556/stream"
    echo "    audi     → rtsp://127.0.0.1:8557/stream"
    exit 0
}

PATH_STR="/stream"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --streams)
            NUM_STREAMS="$2"
            if [[ "$NUM_STREAMS" -lt 1 || "$NUM_STREAMS" -gt 4 ]]; then
                echo "Error: --streams must be 1–4" >&2
                exit 1
            fi
            shift 2 ;;
        --video)
            VIDEO_OVERRIDE="$2"; SINGLE_MODE=true; shift 2 ;;
        --port)
            BASE_PORT="$2"; shift 2 ;;
        --path)
            PATH_STR="$2"; shift 2 ;;
        --no-loop)
            EXTRA_ARGS+=(--no-loop); shift ;;
        --bandwidth)
            EXTRA_ARGS+=(--max-bandwidth "$2"); shift 2 ;;
        --build-only)
            BUILD_ONLY=true; shift ;;
        -h|--help)
            usage ;;
        *)
            echo "Unknown option: $1" >&2
            usage ;;
    esac
done

# ─── Build (standalone layout only) ─────────────────────────────────────
if [[ ! -x "$BINARY" ]]; then
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    echo "[build] compiling odc-video-source..."
    cmake .. >/dev/null 2>&1 || { echo "cmake failed — check dependencies" >&2; exit 1; }
    make -j"$(nproc)" || { echo "make failed" >&2; exit 1; }
    BINARY="$BUILD_DIR/odc-video-source"
elif [[ "$BINARY" == "$SCRIPT_DIR/odc-video-source" ]]; then
    echo "[build] using monorepo binary: $BINARY"
else
    echo "[build] binary is up to date"
fi

if [[ "$BUILD_ONLY" == "true" ]]; then
    echo "[build] done. Binary: $BINARY"
    exit 0
fi

# ─── Validate video files ───────────────────────────────────────────────
resolve_video() {
    local v="$1"
    if [[ -f "$v" ]]; then
        echo "$v"
    elif [[ -f "$SCRIPT_DIR/$v" ]]; then
        echo "$SCRIPT_DIR/$v"
    elif [[ -f "$VID_SRC/$v" ]]; then
        echo "$VID_SRC/$v"
    else
        echo "Error: video file not found: $v" >&2
        return 1
    fi
}

# ─── Cleanup handler ────────────────────────────────────────────────────
PIDS=()
cleanup() {
    echo ""
    echo "[shutdown] stopping odc-video-source processes..."
    for pid in "${PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    echo "[shutdown] all streams stopped."
}
trap cleanup EXIT INT TERM

# ─── Launch ─────────────────────────────────────────────────────────────
echo "========================================"
echo "  odc-video-source RTSP Server"
echo "========================================"

if $SINGLE_MODE; then
    V="$(resolve_video "$VIDEO_OVERRIDE")" || exit 1
    RTSP_URI="rtsp://127.0.0.1:$BASE_PORT$PATH_STR"
    echo "  Mode:     single stream"
    echo "  Video:    $V"
    echo "  URI:      $RTSP_URI"
    echo "========================================"
    echo ""
    echo "Press Ctrl+C to stop."
    exec "$BINARY" --video "$V" --port "$BASE_PORT" --path "$PATH_STR" --loop "${EXTRA_ARGS[@]}"
else
    echo "  Mode:     $NUM_STREAMS stream(s)"
    echo "  Path:     $PATH_STR"
    echo ""

    for ((i=0; i<NUM_STREAMS; i++)); do
        V="$(resolve_video "${ALL_VIDEOS[$i]}")" || exit 1
        PORT=$((BASE_PORT + i))
        echo "  Stream $((i+1)): rtsp://127.0.0.1:$PORT$PATH_STR  ← $(basename "$V")"

        "$BINARY" \
            --video "$V" \
            --port "$PORT" \
            --path "$PATH_STR" \
            --loop \
            "${EXTRA_ARGS[@]}" &
        PIDS+=($!)
    done

    echo "========================================"
    echo ""
    echo "Press Ctrl+C to stop all streams."

    wait
fi
