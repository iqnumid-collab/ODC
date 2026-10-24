# Kept for documentation / optional image builds.
# Preferred path: scripts/docker-build-onboard-aarch64.sh → deploy/aarch64/build-in-container.sh
# (`docker run` avoids BuildKit apt delayed-item hangs in this lab).
FROM --platform=linux/amd64 debian:trixie
COPY deploy/aarch64/build-in-container.sh /build.sh
WORKDIR /src
CMD ["bash", "/build.sh"]
