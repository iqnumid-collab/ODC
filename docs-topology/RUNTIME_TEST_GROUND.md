# Runtime test ground design

How to lay out OpenDroneCore services for **lab runtime testing** across `molt`, `hexa-five`, and the Pi targets.

![Runtime Test Ground](diagrams/rendered/runtime-test-ground.svg)

## Goals

1. Exercise **odc-root** server paths with real networking (not only Docker).
2. Exercise **odc-onboard-v2** on real aarch64 boards with real VPN peers.
3. Keep **build/orchestrate/`odc-hook` monitor** on `molt` so boards stay thin and the event path is asserted from the control plane.
4. Support board matrix: same tests on rpi-4b and rpi-5 in parallel.

## Ground layout

| Zone | Host | Responsibility |
|------|------|----------------|
| Control | `molt` | Build, package, deploy, run **`odc-hook`**, monitor hook + remote tests, collect artifacts |
| Server | `hexa-five` | Stable VPN + registry ground truth (no hook) |
| Device A | `odc-device-rpi-4b` | Onboard DUT |
| Device B | `odc-device-rpi-5` | Onboard DUT (matrix) |

Optional attachments on a Pi:

- Flight controller on `/dev/ttyACM0`
- USB camera or file-backed `odc-video-source`

## Test layers

| Layer | Where | What |
|-------|-------|------|
| L0 Unit | `molt` CI / native | CTest, component tests |
| L1 Container integration | `molt` Docker (`odc-root/deploy/test`) | mTLS, events, peers in compose |
| L2 Lab server | `hexa-five` | Real WG/OVPN listen, WSS :9443 |
| L2b Hook probe | `molt` | Live `odc-hook` mTLS + event counters |
| L3 Lab onboard | Pi boards | Graph boot, VPN connect, MAV/RTSP |
| L4 Matrix / soak | both Pis + hook | Parallel deploy, long-run stability + hook soak |

L1 remains useful for PR CI; L2–L4 are this topology’s purpose.

## Recommended fixtures

On **hexa-five**:

- Dedicated lab CA + server certs under `/etc/odc-root/certs/`
- WG subnet pool reserved for lab (document in tunnel-manager YAML)
- Known peer slots for `rpi-4b` and `rpi-5` (stable keys/IPs)

On **each Pi**:

- Persisted `/etc/odc-onboard/system.yaml` (do not wipe between runs unless `CLEANUP_CONFIG=1`)
- VPN configs in `/etc/odc-onboard/vpn/` matching server peers
- `tests/helpers/real_hardware.env` values pointed at lab server IPs

On **molt**:

- `host-configs/*.env` for inventory
- Artifact dirs: `dist/amd64/odc-root/`, `dist/aarch64/odc-onboard/`
- Log sink: `logs/<date>/<host>/`
- Running `odc-hook` with lab client certs; monitoring via `odc-cli hook status` / systemd

## Runtime scenarios (minimum set)

1. **Server only** — tunnel + registry healthy; CLI status green.
2. **Hook connected** — `odc-hook` on `molt` mTLS-connected; status green before device tests.
3. **Single-device VPN** — rpi-4b connects; peer listed; **hook event counters increase**.
4. **Graph trigger** — VPN Connected → extender + compressor up.
5. **RTSP E2E** — video-source → compressor → monitor.
6. **Dual-device** — both Pis connected with distinct peers; no cross-talk; hook sees both peer events.
7. **Restart durability** — kill onboard; restart; graph + tunnels recover; hook stays connected.
8. **Config push** — molt updates YAML; remote reload/restart; assertions pass.

## Isolation rules

- Lab subnet only (`192.168.10.0/24`); do not point production credentials at these hosts.
- One WG interface naming scheme per device (`wg-lab-rpi4`, `wg-lab-rpi5`).
- Prefer immutable versioned packages (`odc-onboard-v2_<gitsha>_arm64.tar.gz`) over ad-hoc `scp` of loose `.o` rebuilds.

## Success criteria (lab green)

- All [PER_HOST_CHECKS](PER_HOST_CHECKS.md) pass (including **hook status on `molt`**).
- Scenarios 1–5 pass on at least one Pi.
- Scenario 6 passes when both boards are online.
- Artifacts (logs + `odc-cli hook status` snapshots) archived on `molt`.
