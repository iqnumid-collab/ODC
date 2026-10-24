# odc-host — molt (this host)

| Field | Value |
|-------|-------|
| Role | Build / orchestrator / test runner / hook monitor (`odc-host`) |
| Hostname | `molt` |
| IP | `192.168.10.10` |
| SSH user | `molt` |
| Password | *(see `host-configs/molt.env` — do not commit to public remotes)* |
| Arch | `x86_64` (amd64) |
| Config | [`host-configs/molt.yaml`](../../host-configs/molt.yaml) |

## Purpose

Developer workstation and **lab control plane**:

- Cross-compile via Docker (`odc-root/deploy`, aarch64 toolchains for Pi)
- Native build of `odc-root` / `odc-onboard-v2`
- Fetch or generate configs, push deploy packages over SSH
- Orchestrate runtime tests against `hexa-five`, `rpi-4b`, and `rpi-5`
- Run and **monitor `odc-hook`** as the system event-path probe (mTLS WSS client → registry)

## Services to install

| Package / binary | Why |
|------------------|-----|
| Docker + Compose | Cross-compile and integration test containers |
| Build deps (CMake, gcc/g++, pkg-config, …) | Native amd64 builds |
| `odc-hook` | **Required** — long-running mTLS client; proves registry event delivery |
| `odc-cli` (local) | `hook status`, remote IPC via SSH, WSS events watch |
| `qemu-user-static` / binfmt (optional) | aarch64 smoke under emulation |
| SSH client + `sshpass`/`expect` (lab only) | Non-interactive deploy |
| Git + CI runners (optional) | Automate pipelines |
| systemd | `odc-hook.service` always on during lab sessions |

This host does **not** need long-running `odc-tunnel-manager` / onboard daemon unless debugging locally. Server daemons stay on `hexa-five`; Pis run onboard only.

## `odc-hook` on the dev host

| Item | Value |
|------|-------|
| Binary | `/opt/odc/bin/odc-hook` |
| Config | `/etc/odc-root/odc-hook.yaml` → `wss://192.168.10.16:9443` |
| IPC | `/var/run/odc/hook.sock` |
| Unit | `odc-hook.service` |
| Monitor | `odc-cli hook status` + journal / event counters |

Correctness criteria (dev host owns these assertions):

1. Unit active and process alive
2. mTLS session to registry `connected=true`
3. After a Pi VPN connect/disconnect, `events_received` / `events_processed` increase
4. Thread pool not wedged (`thread_pool_active` returns to idle)

## Workspace layout (expected)

```
~/Documents/GitHub/opendronecore/
  odc-root/          # server + hook + deploy Dockerfiles
  odc-onboard-v2/    # Pi target daemon
  host-configs/      # per-host YAML/ENV
  docs-topology/     # this documentation
```

## Per-host checks

See [PER_HOST_CHECKS.md](../PER_HOST_CHECKS.md#odc-host--molt).
