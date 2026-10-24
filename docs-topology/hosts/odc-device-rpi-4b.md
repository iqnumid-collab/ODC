# Target board: rpi-4b

| Field | Value |
|-------|-------|
| Role | Onboard device under test (`odc-onboard-v2`) |
| Hostname | `odc-device-rpi-4b` |
| IP | `192.168.10.11` |
| SSH user | `odc-device-rpi-4b` |
| Password | `odc-device-rpi-4b` |
| Arch | `aarch64` (arm64) |
| OS image | `2026-06-18-raspios-trixie-arm64` |
| Config | [`host-configs/rpi-4b.yaml`](../../host-configs/rpi-4b.yaml) |

## Purpose

Primary Raspberry Pi 4B lab target for **onboard runtime testing**: VPN client tunnels, MAVLink router/extenders, RTSP compressors, and graph IPC.

## Services to install

| Package / binary | Why |
|------------------|-----|
| `odc-onboard-v2` | Onboard connectivity daemon (graph runtime) |
| `odc-cli` (onboard graph CLI) | Local graph mutations and health probes |
| `odc-video-source` | Lab RTSP source for compressor E2E |
| `odc-rtsp-monitor` | Lab RTSP sink / frame monitor |
| GStreamer runtime | RTSP compressor pipelines |
| WireGuard tools (`wireguard-tools`) | Kernel WG client support |
| OpenVPN | OpenVPN client support |
| `libqmi` / modem tools (optional) | Cellular WAN when modem attached |
| systemd units | `odc-onboard-v2.service` auto-start |

## Deploy path

```
/opt/odc/bin/odc-onboard-v2
/etc/odc-onboard/system.yaml
/etc/odc-onboard/vpn/
/var/run/odc-onboard.sock
```

## Per-host checks

See [PER_HOST_CHECKS.md](../PER_HOST_CHECKS.md#rpi-4b--odc-device-rpi-4b).
