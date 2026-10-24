# ── macOS Cross-Compiler Builder (osxcross) ───────────────────────────────────
# Produces: odc-hook-darwin-x86_64
#
# IMPORTANT LEGAL NOTICE:
#   This Dockerfile requires the macOS SDK from Apple to be provided at build
#   time. The SDK is Apple intellectual property and CANNOT be redistributed.
#   You must obtain it yourself under Apple's license terms.
#
#   See macos/SDK_README.md for detailed instructions on extracting the SDK.
#
# Build command (SDK tarball must be in the docker build context):
#   docker build \
#     --build-arg SDK_TARBALL=MacOSX14.0.sdk.tar.xz \
#     --build-arg MACOS_SDK_VERSION=14.0 \
#     -t odc-hook-macos -f deploy/macos/Dockerfile .
#
# Or via Makefile (after placing SDK tarball in deploy/macos/):
#   make -f deploy/Makefile macos SDK_TARBALL=deploy/macos/MacOSX14.0.sdk.tar.xz

# ── Stage 1: Build osxcross toolchain ───────────────────────────────────────
FROM ubuntu:22.04 AS osxcross-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    clang \
    curl \
    patch \
    python3 \
    libssl-dev \
    lzma-dev \
    libxml2-dev \
    xz-utils \
    bzip2 \
    cpio \
    libbz2-dev \
    zlib1g-dev \
    bash \
    uuid-dev \
    grep \
    && rm -rf /var/lib/apt/lists/*

ARG MACOS_SDK_VERSION=14.0
ARG MACOS_DEPLOYMENT_TARGET=10.13
ARG SDK_TARBALL

RUN mkdir -p /tmp/osxcross/tarballs
COPY ${SDK_TARBALL} /tmp/osxcross/tarballs/
RUN ls -la /tmp/osxcross/tarballs/

RUN git clone --depth 1 https://github.com/tpoechtrager/osxcross /tmp/osxcross-src \
    && cd /tmp/osxcross-src \
    && SDK_VERSION="${MACOS_SDK_VERSION}" \
       OSX_VERSION_MIN="${MACOS_DEPLOYMENT_TARGET}" \
       ENABLE_ARCHS="x86_64" \
       UNATTENDED=1 \
       ./build.sh \
    && mv target /opt/osxcross \
    && rm -rf /tmp/osxcross-src /tmp/osxcross

RUN /opt/osxcross/bin/x86_64-apple-darwin*-clang --version

# ── Stage 2: Cross-compile OpenSSL for macOS ───────────────────────────────
FROM osxcross-builder AS deps-builder

ARG MACOS_SDK_VERSION=14.0
ENV OSXCROSS_TARGET=darwin${MACOS_SDK_VERSION}

ENV PATH=/opt/osxcross/bin:$PATH
ENV CC=x86_64-apple-${OSXCROSS_TARGET}-clang
ENV CXX=x86_64-apple-${OSXCROSS_TARGET}-clang++

RUN mkdir -p /opt/cross/macos-x86_64

RUN cd /tmp && \
    git clone --depth 1 --branch openssl-3.2.1 https://github.com/openssl/openssl.git && \
    cd openssl && \
    ./Configure darwin64-x86_64-cc no-shared no-tests no-docs \
        --prefix=/opt/cross/macos-x86_64 \
        --cross-compile-prefix=x86_64-apple-${OSXCROSS_TARGET}- \
        -mmacosx-version-min=10.13 \
        CFLAGS="-O2" && \
    make -j"$(nproc)" && \
    make install_sw && \
    rm -rf /tmp/openssl

# ── Cross-compile libsodium for macOS ───────────────────────────────────────
RUN cd /tmp && \
    curl -sSL https://download.libsodium.org/libsodium/releases/libsodium-1.0.20.tar.gz | tar xz && \
    cd libsodium-stable && \
    ./configure \
        --host=x86_64-apple-darwin14.0 \
        --prefix=/opt/cross/macos-x86_64 \
        --disable-shared \
        --enable-static \
        CC=x86_64-apple-${OSXCROSS_TARGET}-clang \
        && make -j$(nproc) && make install && \
    rm -rf /tmp/libsodium-stable

# ── Stage 3: Build odc-hook for macOS + header audit ───────────────────────
FROM osxcross-builder AS builder

ARG MACOS_SDK_VERSION=14.0
ENV OSXCROSS_TARGET=darwin${MACOS_SDK_VERSION}

ENV PATH=/opt/osxcross/bin:$PATH
ENV CC=x86_64-apple-${OSXCROSS_TARGET}-clang
ENV CXX=x86_64-apple-${OSXCROSS_TARGET}-clang++

COPY --from=deps-builder /opt/cross/macos-x86_64 /opt/cross/macos-x86_64

ENV OPENSSL_ROOT_DIR=/opt/cross/macos-x86_64
ENV CMAKE_PREFIX_PATH=/opt/cross/macos-x86_64

COPY . /build/odc-root
WORKDIR /build/odc-root

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -GNinja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_TOOLCHAIN_FILE=deploy/macos/toolchain.cmake \
    -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR}" \
    -DCMAKE_FIND_ROOT_PATH="${OPENSSL_ROOT_DIR};/opt/osxcross/SDK/MacOSX${MACOS_SDK_VERSION}.sdk" \
    -DBUILD_TUNNEL_MANAGER=OFF \
    -DBUILD_REGISTRY_MANAGER=OFF \
    -DBUILD_ODC_CLI=OFF \
    -DBUILD_ODC_HOOK=ON \
    -DBUILD_ODC_HOOK_VPN=ON \
    -DBUILD_TESTING=ON \
    && cmake --build build --target odc-hook test_event_bus test_thread_pool test_ws_client -j$(nproc) \
    && x86_64-apple-${OSXCROSS_TARGET}-strip build/odc-hook/odc-hook

# ── Cross-platform header audit ──────────────────────────────────────────────
RUN echo "=== Cross-platform header audit ===" \
    && (! grep -rn --include='*.hpp' --include='*.cpp' \
        -E '<sys/socket\.h>|<sys/un\.h>|<sys/capability\.h>|<linux/' \
        odc-hook/ common/include/ common/src/ \
        | grep -v 'platform_linux' \
        | grep -v 'tun_device.cpp') \
    || (echo "ERROR: POSIX-only headers found in cross-platform code" && false)

RUN echo "=== Cross-platform audit PASSED ==="

# ── Package binaries + configs + test artifacts ──────────────────────────
RUN mkdir -p /output/tests /output/config \
    && cp build/odc-hook/odc-hook /output/ \
    && cp build/tools/odc-cli/odc-cli /output/ 2>/dev/null || true \
    && cp odc-hook/config/odc-hook.yaml /output/config/ \
    && cp deploy/macos/com.odc.hook.plist /output/config/ 2>/dev/null || true \
    && cp deploy/macos/install.sh /output/ 2>/dev/null || true \
    && cp build/tests/test_event_bus /output/tests/ 2>/dev/null || true \
    && cp build/tests/test_thread_pool /output/tests/ 2>/dev/null || true \
    && cp build/tests/test_ws_client /output/tests/ 2>/dev/null || true

FROM scratch
COPY --from=builder /output /output
