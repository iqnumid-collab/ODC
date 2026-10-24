# OpenDroneCore Lab Topology

Lab network and host roles for **odc-root** (server) and **odc-onboard-v2** (device) runtime testing.

![Lab Topology](diagrams/rendered/lab-topology.svg)

## Host inventory

| Role | Board / machine | Hostname | IP | SSH user | Password |
|------|-----------------|----------|----|----------|----------|
| Target | rpi-4b | `odc-device-rpi-4b` | `192.168.10.11` | `odc-device-rpi-4b` | `odc-device-rpi-4b` |
| Target | rpi-5 | `odc-device-rpi-5` | `192.168.10.12` | `odc-device-rpi-5` | `odc-device-rpi-5` |
| odc-server | x86_64 | `hexa-five` | `192.168.10.16` | `hexa-five` | `hexa` |
| odc-host (this host) | x86_64 | `molt` | `192.168.10.10` | `molt` | see `host-configs/molt.env` |

Subnet: `192.168.10.0/24`, gateway `192.168.10.1`.

**`odc-hook` runs on `molt` only** — the odc-host installs and monitors it (`odc-cli hook status`) to assert mTLS event-path correctness against `hexa-five`.

## Documents

| Doc | Contents |
|-----|----------|
| [hosts/](hosts/) | Per-host purpose, install list, deploy paths |
| [SERVICES.md](SERVICES.md) | What to install on each host |
| [COMMUNICATION.md](COMMUNICATION.md) | How hosts and services talk |
| [EVENT_PIPELINES.md](EVENT_PIPELINES.md) | Event / control / data pipelines |
| [PER_HOST_CHECKS.md](PER_HOST_CHECKS.md) | Health and readiness checks |
| [RUNTIME_TEST_GROUND.md](RUNTIME_TEST_GROUND.md) | Designing the runtime test ground |
| [BUILD_DEPLOY_PIPELINE.md](BUILD_DEPLOY_PIPELINE.md) | Cross-compile → build → config → deploy |

## Machine-readable configs

Synced with [`host-configs/`](../host-configs/):

- `rpi-4b.yaml` / `.env`
- `rpi-5.yaml` / `.env`
- `hexa-five.yaml` / `.env`
- `molt.yaml` / `.env`

## Quick SSH aliases (suggested `~/.ssh/config`)

```
Host odc-device-rpi-4b
  HostName 192.168.10.11
  User odc-device-rpi-4b

Host odc-device-rpi-5
  HostName 192.168.10.12
  User odc-device-rpi-5

Host hexa-five
  HostName 192.168.10.16
  User hexa-five

Host molt
  HostName 192.168.10.10
  User molt
```
