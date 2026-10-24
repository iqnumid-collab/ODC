# ── Linux Builder ─────────────────────────────────────────────────────────────
# Produces: odc-hook-linux-x86_64, odc-registry-manager, test binaries
#
# Usage:
#   docker build -t odc-hook-linux -f deploy/linux/Dockerfile .
#   docker run --rm -v $(pwd)/deploy/output/linux:/output odc-hook-linux
#
# Or via Makefile:
#   make -f deploy/Makefile linux

FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    g++-12 \
    gcc-12 \
    libssl-dev \
    libsqlite3-dev \
    libcap-dev \
    libsodium-dev \
    && rm -rf /var/lib/apt/lists/*

ENV CC=gcc-12
ENV CXX=g++-12

COPY . /build/odc-root
WORKDIR /build/odc-root

RUN cmake -B /tmp/build -DCMAKE_BUILD_TYPE=Release -GNinja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_TUNNEL_MANAGER=OFF \
    -DBUILD_REGISTRY_MANAGER=ON \
    -DBUILD_ODC_CLI=OFF \
    -DBUILD_ODC_HOOK=ON \
    -DBUILD_ODC_HOOK_VPN=ON \
    -DBUILD_TESTING=ON \
    && cmake --build /tmp/build -j$(nproc)

RUN cd /tmp/build && ctest --output-on-failure -j$(nproc)

# ── Package binaries + configs + test artifacts ───────────────────────────
RUN mkdir -p /output/tests /output/config \
    && cp /tmp/build/odc-hook/odc-hook /output/ \
    && cp /tmp/build/registry-manager/odc-registry-manager /output/ \
    && cp /tmp/build/tools/odc-cli/odc-cli /output/ 2>/dev/null || true \
    && cp odc-hook/config/odc-hook.yaml /output/config/ \
    && cp registry-manager/config/registry-manager.yaml /output/config/ 2>/dev/null || true \
    && cp tunnel-manager/config/tunnel-manager.yaml /output/config/ 2>/dev/null || true \
    && cp configs/systemd/odc-hook.service /output/config/ 2>/dev/null || true \
    && cp configs/systemd/odc-registry-manager.service /output/config/ 2>/dev/null || true \
    && cp configs/systemd/odc-tunnel-manager.service /output/config/ 2>/dev/null || true \
    && cp deploy/linux/install.sh /output/ 2>/dev/null || true \
    && find /tmp/build/tests -type f -executable -exec cp {} /output/tests/ \;

FROM scratch
COPY --from=builder /output /output
