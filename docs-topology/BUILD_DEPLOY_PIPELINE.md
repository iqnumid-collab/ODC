# Build & deploy pipeline

Pipeline stages on `molt` (odc-host): **cross-compile (Docker) → build → install package → fetch/update config → deploy to targets**.

![Build Deploy Pipeline](diagrams/rendered/build-deploy-pipeline.svg)

## Stage overview

| Stage | Input | Output | Host |
|-------|-------|--------|------|
| 1. Cross-compile / build | source trees | platform binaries | `molt` (+ Docker) |
| 2. Package | binaries + units + scripts | tarball / `export/` tree | `molt` |
| 3. Install (local staging) | package | verified tree under `dist/` | `molt` |
| 4. Fetch / update config | `host-configs/`, templates | per-host YAML + VPN + certs | `molt` |
| 5. Deploy | package + config | running services on targets | → `hexa-five`, Pis |
| 6. Verify | SSH checks | pass/fail report | `molt` |

## 1. Cross-compile via Docker

### odc-root (server / hook) — existing

```bash
cd odc-root
make -f deploy/Makefile linux      # → deploy/export/linux/
# optional: windows / macos targets for hook clients
```

Uses `deploy/linux/Dockerfile` (and siblings). See [`odc-root/deploy/export/README.md`](../odc-root/deploy/export/README.md).

### odc-onboard-v2 (Pi aarch64) — target design

Build inside an aarch64 (or cross) Docker image on `molt`:

```bash
# Illustrative — wire to a Dockerfile under odc-onboard-v2/deploy/ when added
docker build -f odc-onboard-v2/deploy/aarch64/Dockerfile \
  -t odc-onboard-aarch64 .
docker create --name odc-pi-build odc-onboard-aarch64
docker cp odc-pi-build:/output ./dist/aarch64/odc-onboard/
docker rm odc-pi-build
```

Requirements in the image:

- CMake, g++ for `aarch64-linux-gnu` (or native arm64 container)
- GStreamer + QMI + WireGuard headers as needed by onboard
- Produce: `odc-onboard-v2`, graph CLI, `odc-video-source`, `odc-rtsp-monitor`, `install.sh`, systemd unit

Native Pi builds are allowed for bring-up; CI/lab default should be Docker from `molt` for reproducibility.

## 2–3. Build package layout

### Server package → `hexa-five`

```
dist/amd64/odc-root/
  odc-tunnel-manager
  odc-registry-manager
  odc-cli
  install.sh
  config/*.yaml
  config/*.service
```

### Hook package → `molt` (odc-host, required)

```
dist/amd64/odc-hook/
  odc-hook
  odc-cli                     # for hook status monitoring
  install-hook.sh
  config/odc-hook.yaml        # wss://192.168.10.16:9443 + client certs
  config/odc-hook.service
```

(Alternatively split from the same `deploy/export/linux/` tree: install tunnel/registry on hexa-five, install **only** `odc-hook` + unit on `molt`.)

### Onboard package → Pis

```
dist/aarch64/odc-onboard/
  odc-onboard-v2
  odc-cli                     # graph CLI
  tools/odc-video-source
  tools/odc-rtsp-monitor
  install.sh
  config/system.yaml.template
  config/odc-onboard-v2.service
```

## 4. Fetch / update config

| Source | Consumed by | Action |
|--------|-------------|--------|
| `host-configs/<host>.yaml` | deploy scripts | inventory (IP, user, role) |
| `host-configs/<host>.env` | shell orchestration | `source` before SSH |
| Server YAML templates | `hexa-five` | tunnel listen, WG pool, WSS cert paths |
| `system.yaml` (+ VPN files) | Pis | graph topology + tunnel client configs |
| Certs from `odc-cli cert` | registry on server; **hook client certs on `molt`** | issue/rotate; keep hook certs local to odc-host |

Suggested helper flow:

```bash
# Render configs for a target
./scripts/render-config.sh --host rpi-4b --out /tmp/cfg-rpi4
# Merge lab VPN peer files from a secrets store or local vault path
./scripts/fetch-vpn-configs.sh --host rpi-4b --out /tmp/cfg-rpi4/vpn
```

Never bake production secrets into Docker images; inject at deploy time from `host-configs` or a private vault.

## 5. Deploy

### Install odc-hook on odc-host (`molt`) — required

```bash
# From the same linux export / dist tree
sudo install -D deploy/export/linux/odc-hook /opt/odc/bin/odc-hook
sudo install -D deploy/export/linux/config/odc-hook.yaml /etc/odc-root/odc-hook.yaml
sudo install -D deploy/export/linux/config/odc-hook.service /etc/systemd/system/odc-hook.service
# Ensure yaml points at wss://192.168.10.16:9443 and lab client certs
sudo systemctl daemon-reload
sudo systemctl enable --now odc-hook
odc-cli hook status   # must show connected=true before device E2E
```

### To odc-server

```bash
rsync -av dist/amd64/odc-root/ hexa-five@192.168.10.16:/tmp/odc-root/
ssh hexa-five@192.168.10.16 'cd /tmp/odc-root && sudo ./install.sh'
ssh hexa-five@192.168.10.16 'sudo systemctl restart odc-tunnel-manager odc-registry-manager'
```

### To target boards

```bash
HOST=odc-device-rpi-4b IP=192.168.10.11
rsync -av dist/aarch64/odc-onboard/ ${HOST}@${IP}:/tmp/odc-onboard/
rsync -av /tmp/cfg-rpi4/ ${HOST}@${IP}:/tmp/odc-onboard/config/
ssh ${HOST}@${IP} 'cd /tmp/odc-onboard && sudo ./install.sh && sudo systemctl restart odc-onboard-v2'
```

Repeat for `rpi-5` (`192.168.10.12`).

## 6. Verify

Run [PER_HOST_CHECKS.md](PER_HOST_CHECKS.md) (hook checks on `molt` first), then a thin E2E:

```bash
# on molt — event-path correctness
systemctl is-active odc-hook && odc-cli hook status

# on Pi
sudo REAL_HARDWARE=1 /opt/odc/share/tests/run_all.sh
# or remote driver from molt
ssh odc-device-rpi-4b@192.168.10.11 'sudo REAL_HARDWARE=1 /opt/odc/share/tests/run_all.sh'

# after VPN churn, re-check hook counters on molt
odc-cli hook status
```

## Pipeline as Makefile targets (recommended)

```make
.PHONY: build-server build-onboard package config install-hook deploy-server deploy-pi4 deploy-pi5 verify

build-server:
	$(MAKE) -C odc-root -f deploy/Makefile linux

build-onboard:
	./scripts/docker-build-onboard-aarch64.sh

package: build-server build-onboard
	./scripts/package-dist.sh

config:
	./scripts/render-all-configs.sh

install-hook: package config
	./scripts/install-hook-local.sh   # molt only

deploy-server: package config
	./scripts/deploy.sh hexa-five

deploy-pi4: package config
	./scripts/deploy.sh rpi-4b

deploy-pi5: package config
	./scripts/deploy.sh rpi-5

verify:
	./scripts/verify-lab.sh           # includes odc-cli hook status
```

## CI vs lab

| Context | Build | Deploy |
|---------|-------|--------|
| GitHub Actions | `odc-root` Docker linux (+ wine smoke) | artifacts only |
| Lab (`molt`) | amd64 server + aarch64 onboard Docker | SSH to `192.168.10.0/24` |

PR CI should not require the physical Pis; lab pipelines on `molt` own hardware deploy.
