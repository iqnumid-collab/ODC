# ── odc-root Integration Test Docker Image ──────────────────────────────────
# Builds all services + CLI, runs integration tests inside container.
# Usage:
#   docker build -t odc-root-test -f deploy/test/Dockerfile .
#   docker run --rm --cap-add=NET_ADMIN --device=/dev/net/tun \
#       -v $(pwd)/deploy/export/linux:/output odc-root-test

FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git ca-certificates \
    g++-12 gcc-12 libssl-dev libsqlite3-dev libcap-dev libsodium-dev \
    && rm -rf /var/lib/apt/lists/*

ENV CC=gcc-12 CXX=g++-12

COPY . /src/odc-root
WORKDIR /src/odc-root

RUN cmake -B /tmp/build -DCMAKE_BUILD_TYPE=Release -GNinja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_TUNNEL_MANAGER=ON \
    -DBUILD_REGISTRY_MANAGER=ON \
    -DBUILD_ODC_CLI=ON \
    -DBUILD_ODC_HOOK=ON \
    -DBUILD_TESTING=ON \
    && cmake --build /tmp/build -j$(nproc) \
    && cmake --install /tmp/build --prefix /usr

# ── Runtime stage ──────────────────────────────────────────────────────────
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    openvpn wireguard-tools iproute2 net-tools curl jq python3 \
    libssl3 libsqlite3-0 libsodium23 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/bin/odc-* /usr/bin/
# Note: odc-common is a STATIC library, so there is no shared lib to copy.
# (A COPY instruction cannot use shell redirection like `2>/dev/null`.)

RUN ldconfig

COPY deploy/test/entrypoint.sh /entrypoint.sh
COPY deploy/test/test_suite.sh /test_suite.sh
COPY deploy/test/helpers.sh /helpers.sh
RUN chmod +x /entrypoint.sh /test_suite.sh /helpers.sh

ENTRYPOINT ["/entrypoint.sh"]
