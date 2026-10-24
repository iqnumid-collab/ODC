# Test suite overview — feature-isolated device testing

How the four core suites compose, how isolation is enforced, and what evidence to keep.

**Runners:** VPN → `scripts/lab-run-vpn-suite.sh`; video → `scripts/lab-run-video-suite.sh`.

![Suite isolation and order](diagrams/rendered/suite-isolation-order.svg)

## Suite catalog

| Suite ID | Doc | Feature under test | Allowed components | Forbidden while running |
|----------|-----|--------------------|--------------------|-------------------------|
| `ODC-DEV-CEL` | [01-cellular-wan.md](suites/01-cellular-wan.md) | Cellular WAN | `cellular.qmi` only | VPN, MAV extender, RTSP |
| `ODC-DEV-VPN` | [02-vpn.md](suites/02-vpn.md) · deep [vpn/](vpn/) | VPN tunnels | `vpn.wireguard` and/or `vpn.openvpn` (+ optional `cellular.qmi` as WAN only) | MAV extender, RTSP compressor |
| `ODC-DEV-MAV` | [03-mavlink.md](suites/03-mavlink.md) | MAVLink router + host sharing + VPN extender (phased) | Phases A–B: `mavlink.router` only; Phases C–D: + `vpn.*` + `mavlink.extender` | Phases A–B: live VPN/RTSP/cellular; Phases C–D: RTSP only |
| `ODC-DEV-VID` | [04-video-rtsp.md](suites/04-video-rtsp.md) · deep [compressor/](compressor/) | RTSP compressor data plane | `rtsp.compressor` (+ lab video-source/monitor) | Live VPN, MAV, cellular |

Full multi-feature E2E remains documented in [`../docs/REAL_HARDWARE_TEST_SUITE.md`](../docs/REAL_HARDWARE_TEST_SUITE.md) and is **out of scope** for this isolated suite set.

## Isolation rules

1. **Fixture replace** — before a suite, install that suite’s minimal YAML; restore previous config after.
2. **One suite process** — do not start suite B while suite A still owns the daemon config.
3. **No cross-triggers** — fixtures must not declare `vpn.connected → mav/rtsp` links when testing cellular/VPN alone.
4. **Port / iface hygiene** — tear down `wwan0` sessions, TUN/WG ifaces, and RTSP ports after each suite.
5. **Matrix independence** — rpi-4b and rpi-5 may run the same suite ID in parallel if each has its own modem/FC/peer.

## Recommended run order (per DUT)

```
ATT preflight → ODC-DEV-CEL → ODC-DEV-VPN → ODC-DEV-MAV → ODC-DEV-VID → (optional) restore full system.yaml
```

Rationale: cellular proves WAN; VPN reuses WAN; MAV Phases A–B are UART/LAN-only; **MAV Phases C–D require VPN** (run after `ODC-DEV-VPN` or with nested WG fixture). Video stays isolated from MAV/VPN.

MAV fixtures: [`fixtures/mav-router-only.yaml`](fixtures/mav-router-only.yaml), [`fixtures/mav-router-vpn-extender.yaml`](fixtures/mav-router-vpn-extender.yaml).  
VPN fixtures: [`fixtures/vpn-wg-only.yaml`](fixtures/vpn-wg-only.yaml), [`fixtures/vpn-ovpn-only.yaml`](fixtures/vpn-ovpn-only.yaml).  
Diagram: [`diagrams/rendered/suite-mavlink-full.svg`](diagrams/rendered/suite-mavlink-full.svg).  
Runners: [`../../../scripts/lab-run-mav-suite.sh`](../../../scripts/lab-run-mav-suite.sh), [`../../../scripts/lab-run-vpn-suite.sh`](../../../scripts/lab-run-vpn-suite.sh).

## Result model

| Result | Meaning |
|--------|---------|
| **PASS** | All mandatory assertions for the case succeeded |
| **FAIL** | Hardware/prereqs present but behavior incorrect |
| **SKIP** | Declared attachment or server missing |

Aggregate report (design):

```
host=odc-device-rpi-5 suite=ODC-DEV-CEL
  CEL-01 PASS
  CEL-02 PASS
  CEL-03 SKIP reason=no_apn_in_lab
summary: 2 PASS, 0 FAIL, 1 SKIP
```

## Evidence to archive on `molt`

Per suite run, under `logs/<date>/<host>/<suite-id>/`:

| Artifact | Source |
|----------|--------|
| `graph.status.json` | CLI / IPC dump at start and end |
| `system.yaml.fixture` | YAML used for the suite |
| `journal-odc-onboard-v2.txt` | `journalctl -u odc-onboard-v2` |
| `ip-addr.txt` / `ip-link.txt` | Interface state |
| Feature-specific | See each suite (qmicli, mav stats, rtsp bandwidth) |

## Driver model (future automation — not implemented here)

| Role | Responsibility |
|------|----------------|
| Driver host | `molt` SSHes to DUT, pushes fixture, collects artifacts |
| DUT | Runs daemon + local probes |
| Server | `hexa-five` only for VPN suite peer listing |

Pseudo-flow:

```
for host in rpi-4b rpi-5:
  for suite in CEL VPN MAV VID:
    backup config → apply fixture → restart service → run cases → collect → restore
```

## Relationship to systemd service

Suites assume the installed unit:

- `ExecStart=/opt/odc/bin/odc-onboard-v2 --daemon --config /etc/odc-onboard/system.yaml`
- Socket: `/run/odc-onboard/odc-onboard.sock`

Temporary sockets/configs are allowed for automation later; design assertions stay the same.
