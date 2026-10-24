#!/usr/bin/env bash
# Cross-build odc-onboard-v2 for aarch64 via Docker (amd64→arm64 toolchain).
# Uses `docker run` (not buildx) to avoid apt "delayed item" hangs under BuildKit.
# See docs-topology/BUILD_DEPLOY_PIPELINE.md
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="$REPO/odc-onboard-v2"
OUT="$REPO/dist/aarch64/odc-onboard"
IMG_BASE="debian:trixie"

mkdir -p "$OUT"
rm -rf "${OUT:?}/"*

echo "==> docker pull $IMG_BASE (linux/amd64)"
docker pull --platform linux/amd64 "$IMG_BASE"

echo "==> docker run cross-build → $OUT"
mkdir -p "$SRC/.cache/CPM"
docker run --rm \
  --platform linux/amd64 \
  --network=host \
  -e SRC=/src \
  -e OUT=/out \
  -e HOME=/root \
  -e CPM_SOURCE_CACHE=/root/.cache/CPM \
  -e HOST_UID="$(id -u)" \
  -e HOST_GID="$(id -g)" \
  -v "$SRC:/src:ro" \
  -v "$OUT:/out" \
  -v "$SRC/.cache/CPM:/root/.cache/CPM" \
  -v "$SRC/deploy/aarch64/build-in-container.sh:/build.sh:ro" \
  "$IMG_BASE" \
  bash /build.sh

chmod +x "$OUT/install.sh" "$OUT/odc-onboard-v2" 2>/dev/null || true
chmod +x "$OUT/tools/"* 2>/dev/null || true
file "$OUT/odc-onboard-v2"
ls -la "$OUT"
echo "OK: package at $OUT"
