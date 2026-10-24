# ── Windows Cross-Compiler Builder (MinGW-w64) ───────────────────────────────
# Produces: odc-hook-windows-x86_64.exe
#
# Compile-level validation: builds test targets to verify cross-platform linking,
# then runs zero-POSIX-header audit on odc-hook and common library.

FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    g++-mingw-w64-x86-64 \
    gcc-mingw-w64-x86-64 \
    mingw-w64-tools \
    perl \
    make \
    grep \
    unzip \
    curl \
    && rm -rf /var/lib/apt/lists/*

ENV CC=x86_64-w64-mingw32-gcc
ENV CXX=x86_64-w64-mingw32-g++

# ── Cross-compile OpenSSL for Windows ────────────────────────────────────────
RUN mkdir -p /opt/cross && \
    cd /tmp && \
    git clone --depth 1 --branch openssl-3.2.1 https://github.com/openssl/openssl.git && \
    cd openssl && \
    ./Configure mingw64 no-shared no-tests no-docs \
        --prefix=/opt/cross/x86_64-w64-mingw32 \
        --cross-compile-prefix=x86_64-w64-mingw32- \
        CFLAGS="-O2" && \
    make -j"$(nproc)" && \
    make install_sw && \
    rm -rf /tmp/openssl

# ── Cross-compile libsodium for Windows ──────────────────────────────────────
RUN cd /tmp && \
    curl -sSL https://download.libsodium.org/libsodium/releases/libsodium-1.0.20.tar.gz | tar xz && \
    cd libsodium-stable && \
    ./configure \
        --host=x86_64-w64-mingw32 \
        --prefix=/opt/cross/x86_64-w64-mingw32 \
        --disable-shared \
        --enable-static \
        CC=x86_64-w64-mingw32-gcc \
        && make -j$(nproc) && make install && \
    rm -rf /tmp/libsodium-stable

# ── Download wintun.dll for Windows TUN support ─────────────────────────────
RUN curl -sSL https://www.wintun.net/builds/wintun-0.14.1.zip -o /tmp/wintun.zip && \
    unzip -j /tmp/wintun.zip "wintun/bin/amd64/wintun.dll" -d /opt/cross/x86_64-w64-mingw32/bin/ && \
    rm /tmp/wintun.zip

ENV OPENSSL_ROOT_DIR=/opt/cross/x86_64-w64-mingw32
ENV CMAKE_PREFIX_PATH=/opt/cross/x86_64-w64-mingw32

COPY . /build/odc-root
WORKDIR /build/odc-root

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -GNinja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_TOOLCHAIN_FILE=deploy/windows/toolchain.cmake \
    -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR}" \
    -DCMAKE_FIND_ROOT_PATH="/opt/cross/x86_64-w64-mingw32;/usr/x86_64-w64-mingw32" \
    -DBUILD_TUNNEL_MANAGER=OFF \
    -DBUILD_REGISTRY_MANAGER=OFF \
    -DBUILD_ODC_CLI=OFF \
    -DBUILD_ODC_HOOK=ON \
    -DBUILD_ODC_HOOK_VPN=ON \
    -DBUILD_TESTING=ON \
    && cmake --build build --target odc-hook test_event_bus test_thread_pool test_ws_client -j$(nproc) \
    && x86_64-w64-mingw32-strip build/odc-hook/odc-hook.exe

# ── Cross-platform header audit ──────────────────────────────────────────────
RUN echo "=== Cross-platform header audit ===" \
    && (! grep -rn --include='*.hpp' --include='*.cpp' \
        -E '<sys/socket\.h>|<sys/un\.h>|<sys/capability\.h>|<linux/' \
        odc-hook/ common/include/ common/src/ \
        | grep -v 'platform_linux' \
        | grep -v 'tun_device.cpp') \
    || (echo "ERROR: POSIX-only headers found in cross-platform code" && false)

RUN echo "=== Cross-platform audit PASSED ==="

# ── Package binaries + wintun.dll + configs + test artifacts ────────────
RUN mkdir -p /output/tests /output/config \
    && cp build/odc-hook/odc-hook.exe /output/ \
    && cp build/tools/odc-cli/odc-cli.exe /output/ 2>/dev/null || true \
    && cp /opt/cross/x86_64-w64-mingw32/bin/wintun.dll /output/ \
    && cp odc-hook/config/odc-hook.yaml /output/config/ \
    && cp deploy/windows/install.ps1 /output/ 2>/dev/null || true \
    && cp build/tests/test_event_bus.exe /output/tests/ 2>/dev/null || true \
    && cp build/tests/test_thread_pool.exe /output/tests/ 2>/dev/null || true \
    && cp build/tests/test_ws_client.exe /output/tests/ 2>/dev/null || true

FROM scratch
COPY --from=builder /output /output
