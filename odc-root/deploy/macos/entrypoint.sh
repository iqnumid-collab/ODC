#!/bin/bash
set -euo pipefail

echo "=== Building odc-hook for macOS (x86_64) via osxcross ==="

OSXCROSS_TARGET="${OSXCROSS_TARGET:-darwin23}"
export PATH="/opt/osxcross/bin:$PATH"
export CC="x86_64-apple-${OSXCROSS_TARGET}-clang"
export CXX="x86_64-apple-${OSXCROSS_TARGET}-clang++"

BUILD_DIR="/build/odc-root/build-macos"
mkdir -p "$BUILD_DIR"

cmake -B "$BUILD_DIR" -S /build/odc-root \
    -DCMAKE_BUILD_TYPE=Release \
    -GNinja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_TOOLCHAIN_FILE=deploy/macos/toolchain.cmake \
    -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR}" \
    -DCMAKE_FIND_ROOT_PATH="${OPENSSL_ROOT_DIR};/opt/osxcross/SDK" \
    -DBUILD_TUNNEL_MANAGER=OFF \
    -DBUILD_REGISTRY_MANAGER=OFF \
    -DBUILD_ODC_CLI=OFF \
    -DBUILD_ODC_HOOK=ON \
    -DBUILD_TESTING=ON

cmake --build "$BUILD_DIR" --target odc-hook test_event_bus test_thread_pool test_ws_client -j"$(nproc)"
x86_64-apple-"${OSXCROSS_TARGET}"-strip "$BUILD_DIR/odc-hook/odc-hook"

echo "=== Cross-platform header audit ==="
if grep -rn --include='*.hpp' --include='*.cpp' \
    -E '<sys/socket\.h>|<sys/un\.h>|<sys/capability\.h>|<linux/' \
    /build/odc-root/odc-hook/ /build/odc-root/common/include/ /build/odc-root/common/src/ \
    | grep -v 'platform_linux'; then
    echo "ERROR: POSIX-only headers found in cross-platform code"
    exit 1
fi
echo "=== Header audit PASSED ==="

mkdir -p /output/tests
cp "$BUILD_DIR/odc-hook/odc-hook" /output/
cp "$BUILD_DIR/tests/test_event_bus" /output/tests/ 2>/dev/null || true
cp "$BUILD_DIR/tests/test_thread_pool" /output/tests/ 2>/dev/null || true
cp "$BUILD_DIR/tests/test_ws_client" /output/tests/ 2>/dev/null || true

echo "=== Cross-compile complete ==="
file /output/odc-hook
