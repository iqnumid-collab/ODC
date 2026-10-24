# Services per host

![Services Placement](diagrams/rendered/services-placement.svg)

## Matrix

| Service | `molt` (odc-host) | `hexa-five` (odc-server) | `rpi-4b` | `rpi-5` |
|---------|:-----------------:|:------------------------:|:--------:|:------:|
| Docker / Compose | required | optional | — | — |
| Cross-compile toolchains (Docker) | required | — | — | — |
| `odc-tunnel-manager` | local debug only | **required** | — | — |
| `odc-registry-manager` | local debug only | **required** | — | — |
| `odc-hook` | **required** | — | — | — |
| `odc-cli` (root) | required | required | — | — |
| `odc-onboard-v2` | optional native | — | **required** | **required** |
| Onboard graph CLI | optional | — | **required** | **required** |
| `odc-video-source` | optional | — | lab E2E | lab E2E |
| `odc-rtsp-monitor` | optional | — | lab E2E | lab E2E |
| GStreamer | if testing RTSP | — | **required** | **required** |
| WireGuard tools | for local VPN tests | **required** | **required** | **required** |
| OpenVPN | for local VPN tests | **required** | **required** | **required** |
| SQLite (runtime DB) | — | **required** | — | — |
| systemd units | `odc-hook` | tunnel + registry | onboard | onboard |

`odc-hook` runs only on the **odc-host** (`molt`). It is the lab’s mTLS WSS client against `hexa-five:9443`; the dev host monitors hook health and event throughput to assert registry/event-path correctness.

## Install notes by role

### odc-host (`molt`)

```bash
# Docker for deploy/Makefile cross builds
sudo apt install docker.io docker-compose-v2 cmake g++ pkg-config

# Build odc-root (includes odc-hook), then install hook locally:
cd odc-root && make -f deploy/Makefile linux
sudo cp deploy/export/linux/odc-hook /opt/odc/bin/
sudo cp deploy/export/linux/config/odc-hook.yaml /etc/odc-root/
sudo cp deploy/export/linux/config/odc-hook.service /etc/systemd/system/
# Point odc-hook.yaml at wss://192.168.10.16:9443 with lab client certs
sudo systemctl enable --now odc-hook
odc-cli hook status   # connected, events_received increasing

# aarch64 Pi packages: see BUILD_DEPLOY_PIPELINE.md
```

### odc-server (`hexa-five`)

Deploy `odc-root` Linux package from `deploy/export/linux/`:

```bash
scp -r deploy/export/linux/ hexa-five@192.168.10.16:/tmp/odc-linux
ssh hexa-five@192.168.10.16 'cd /tmp/odc-linux && sudo ./install.sh'
sudo systemctl enable --now odc-tunnel-manager odc-registry-manager
```

### Target boards (`rpi-4b`, `rpi-5`)

Deploy aarch64 `odc-onboard-v2` + tools + VPN configs:

```bash
# From molt after aarch64 build artifact is ready
scp -r dist/aarch64/odc-onboard/ odc-device-rpi-4b@192.168.10.11:/tmp/
ssh odc-device-rpi-4b@192.168.10.11 'cd /tmp/odc-onboard && sudo ./install.sh'
```

Same flow for `192.168.10.12` (`rpi-5`).
