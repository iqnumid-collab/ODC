#!/bin/bash
set -euo pipefail

echo "=== Building odc-hook for Linux (x86_64) ==="

BUILD_DIR="/build/odc-root/build-linux"
mkdir -p "$BUILD_DIR"

cmake -B "$BUILD_DIR" -S /build/odc-root \
    -DCMAKE_BUILD_TYPE=Release \
    -GNinja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_TUNNEL_MANAGER=OFF \
    -DBUILD_REGISTRY_MANAGER=ON \
    -DBUILD_ODC_CLI=OFF \
    -DBUILD_ODC_HOOK=ON \
    -DBUILD_TESTING=ON

cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "=== Running tests ==="
cd "$BUILD_DIR"
ctest --output-on-failure -j"$(nproc)"
echo "=== All tests passed ==="

mkdir -p /output/tests
cp "$BUILD_DIR/odc-hook/odc-hook" /output/
cp "$BUILD_DIR/registry-manager/odc-registry-manager" /output/
find "$BUILD_DIR/tests" -type f -executable -exec cp {} /output/tests/ \;

echo "=== Build complete: /output/odc-hook ==="
ls -lh /output/
