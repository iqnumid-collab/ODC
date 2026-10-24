#!/bin/bash
# ── Common dependency preparation for cross-compilation ───────────────────────
# Called by platform-specific entrypoint.sh scripts.
# Sets up common build environment variables.
set -euo pipefail

echo "=== Preparing build environment ==="

BUILD_DIR="${BUILD_DIR:-/build/odc-root/build}"
mkdir -p "$BUILD_DIR" /output

# Common CMake flags for odc-hook cross-compilation
COMMON_CMAKE_FLAGS=(
    -DCMAKE_BUILD_TYPE=Release
    -GNinja
    -DBUILD_TUNNEL_MANAGER=OFF
    -DBUILD_REGISTRY_MANAGER=OFF
    -DBUILD_ODC_CLI=OFF
    -DBUILD_ODC_HOOK=ON
)

# OpenSSL cross-compile helper
build_openssl_cross() {
    local target="$1"       # e.g., mingw64, darwin64-x86_64-cc
    local prefix="$2"       # e.g., /opt/cross/mingw-w64
    local cross_prefix="$3" # e.g., x86_64-w64-mingw32-
    local extra_cflags="${4:-}"

    if [ -f "${prefix}/lib/libssl.a" ]; then
        echo "OpenSSL already built at ${prefix}"
        return 0
    fi

    echo "--- Cross-compiling OpenSSL for ${target} ---"
    mkdir -p /tmp/openssl-build
    cd /tmp/openssl-build

    if [ ! -f openssl.tar.gz ]; then
        curl -sSL -o openssl.tar.gz \
            https://github.com/openssl/openssl/releases/download/openssl-3.2.1/openssl-3.2.1.tar.gz
        tar xzf openssl.tar.gz --strip-components=1
    fi

    ./Configure "${target}" no-shared no-tests no-docs no-apps \
        --prefix="${prefix}" \
        --cross-compile-prefix="${cross_prefix}" \
        CFLAGS="-O2 ${extra_cflags}"

    make -j"$(nproc)"
    make install_sw
    rm -rf /tmp/openssl-build
    echo "--- OpenSSL build complete ---"
}

# SQLite3 amalgamation fetcher (if not using CPM)
fetch_sqlite_amalgamation() {
    local version="${1:-3450200}"
    local dest="${2:-/tmp/sqlite}"

    if [ -f "${dest}/sqlite3.c" ]; then
        echo "SQLite3 source already at ${dest}"
        return 0
    fi

    mkdir -p "${dest}"
    curl -sSL -o "${dest}/sqlite3.zip" \
        "https://www.sqlite.org/2024/sqlite-amalgamation-${version}.zip"
    unzip -qo "${dest}/sqlite3.zip" -d "${dest}"
    echo "SQLite3 amalgamation fetched to ${dest}"
}

echo "=== Build environment ready ==="
