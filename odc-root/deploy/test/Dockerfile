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
# odc-common is a STATIC library, so there is no shared lib to copy.
# But the binaries link against CPM-built shared libs (spdlog, fmt) that are
# NOT part of any apt package version — copy them from the builder so the
# binaries resolve their shared objects at runtime.
COPY --from=builder /tmp/build/_deps/spdlog-build/libspdlog.so* /usr/lib/
COPY --from=builder /tmp/build/_deps/fmt-build/libfmt.so* /usr/lib/

RUN ldconfig

# The entrypoint reads component configs from /src/odc-root/... and cd's there
# to run the test suite. Provide the source tree in the runtime image.
COPY . /src/odc-root

COPY deploy/test/entrypoint.sh /entrypoint.sh
COPY deploy/test/test_suite.sh /test_suite.sh
COPY deploy/test/helpers.sh /helpers.sh
RUN chmod +x /entrypoint.sh /test_suite.sh /helpers.sh

ENTRYPOINT ["/entrypoint.sh"]
