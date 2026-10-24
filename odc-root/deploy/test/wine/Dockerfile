# ── Windows Wine Smoke Test Docker Image ────────────────────────────────────
# Cross-compiles odc-hook + odc-cli for Windows, then runs smoke tests via Wine.

FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64 mingw-w64-tools \
    cmake ninja-build git ca-certificates perl make \
    && rm -rf /var/lib/apt/lists/*

ENV CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++

RUN cd /tmp && git clone --depth 1 --branch openssl-3.2.1 https://github.com/openssl/openssl.git && \
    cd openssl && \
    ./Configure mingw64 no-shared no-tests no-docs no-apps \
        --prefix=/opt/cross/x86_64-w64-mingw32 \
        --cross-compile-prefix=x86_64-w64-mingw32- CFLAGS="-O2" && \
    make -j"$(nproc)" && make install_sw && rm -rf /tmp/openssl

ENV OPENSSL_ROOT_DIR=/opt/cross/x86_64-w64-mingw32
ENV CMAKE_PREFIX_PATH=/opt/cross/x86_64-w64-mingw32

COPY . /src/odc-root
WORKDIR /src/odc-root

RUN cmake -B /tmp/build-win -DCMAKE_BUILD_TYPE=Release -GNinja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_TOOLCHAIN_FILE=deploy/windows/toolchain.cmake \
    -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR}" \
    -DCMAKE_FIND_ROOT_PATH="/opt/cross/x86_64-w64-mingw32;/usr/x86_64-w64-mingw32" \
    -DBUILD_TUNNEL_MANAGER=OFF -DBUILD_REGISTRY_MANAGER=OFF \
    -DBUILD_ODC_CLI=ON -DBUILD_ODC_HOOK=ON -DBUILD_TESTING=ON \
    && cmake --build /tmp/build-win --target odc-hook odc-cli -j$(nproc) \
    && x86_64-w64-mingw32-strip /tmp/build-win/odc-hook/odc-hook.exe \
    && x86_64-w64-mingw32-strip /tmp/build-win/tools/odc-cli/odc-cli.exe

# ── Runtime stage with Wine ──────────────────────────────────────────────
FROM ubuntu:22.04

RUN dpkg --add-architecture i386 && apt-get update && DEBIAN_FRONTEND=noninteractive \
    apt-get install -y --no-install-recommends \
    wine64 wine32 wine libwine libwine:i386 \
    curl jq grep \
    && rm -rf /var/lib/apt/lists/*

ENV WINEDEBUG=-all WINEPREFIX=/root/.wine

# Initialize Wine
RUN wine wineboot --init 2>/dev/null || true

COPY --from=builder /tmp/build-win/odc-hook/odc-hook.exe /win/odc-hook.exe
COPY --from=builder /tmp/build-win/tools/odc-cli/odc-cli.exe /win/odc-cli.exe
COPY deploy/test/wine/smoke_test.sh /smoke_test.sh
RUN chmod +x /smoke_test.sh

ENTRYPOINT ["/smoke_test.sh"]
