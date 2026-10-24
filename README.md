# OpenDroneCore

OpenDroneCore (ODC) is a modular connectivity and media platform for drone/edge
deployments. It manages cellular WAN links, VPN tunnels, MAVLink routing, and
RTSP video compression through a graph-based runtime, split across an onboard
agent and a server-side control plane.

## Components

| Component | Description |
|-----------|-------------|
| `odc-onboard-v2/` | Onboard agent (current). Graph runtime, QMI cellular modem control, VPN tunnels (OpenVPN/WireGuard), MAVLink router, RTSP compression. |
| `odc-onboard/` | Previous-generation onboard agent. |
| `odc-root/` | Server-side control plane: registry-manager, tunnel-manager, odc-hook, CLI tooling, and the desktop app (`odc-app`). |
| `scripts/` | Build helper scripts. |

## Architecture

The onboard agent builds a **processing graph** from a YAML configuration. Each
node is a component (cellular WAN, VPN tunnel, MAVLink router, RTSP compressor,
etc.) wired together by a link engine and driven by an event bus. The runtime
validates, builds, and supervises the graph, exposing status and control over
an IPC interface consumed by the CLI and control plane.

The server side (`odc-root`) provides device registry, VPN tunnel provisioning
(OpenVPN and WireGuard servers with certificate management), and a hook service
for client-side VPN bring-up.

## Building

Each component uses CMake. From a component directory:

```bash
cmake -S . -B build
cmake --build build -j
```

> External dependencies (libqmi, CAF, CLI11, and others) are staged under
> `build-deps/` and `thirdparty/`. CMake-fetched packages are cached locally and
> are not committed.

## Repository notes

- Build artifacts, caches, logs, sample media, VPN credentials, host-specific
  configs, and documentation folders are excluded via `.gitignore`.
- Do **not** commit secrets (VPN keys, certificates, `.env` files) or
  host-specific configuration. These are intentionally ignored.

## License

Proprietary — all rights reserved unless stated otherwise.
