# odc-hosts-architecture — device test suite design

**Status:** design only (no automation executed from this folder yet).  
**Scope:** prove each core `odc-onboard-v2` feature on real `odc-device-*` boards with real attachments.

![Lab device matrix](diagrams/rendered/lab-device-matrix.svg)

## Purpose

Define a **host-attached, feature-isolated** hardware test suite for:

| Feature | Graph type(s) | Real attachment |
|---------|---------------|-----------------|
| Cellular WAN | `cellular.qmi` | USB QMI modem (`/dev/cdc-wdm0`, `wwan0`) |
| VPN | `vpn.wireguard`, `vpn.openvpn` | Lab peers on `hexa-five` + cellular (or eth) WAN |
| MAVLink | `mavlink.router`, `mavlink.extender` | Flight controller on `/dev/ttyACM0` (or documented UART) |
| Video | `rtsp.compressor` | Camera or file-backed `odc-video-source` + `odc-rtsp-monitor` |

Each core feature is tested **on its own** (other components disabled or absent from the fixture YAML) before any full-graph E2E.

## DUT hosts

| Host | IP | Inventory | Topology notes |
|------|-----|-----------|----------------|
| `odc-device-rpi-4b` | `192.168.10.11` | [`host-configs/rpi-4b.yaml`](../../host-configs/rpi-4b.yaml) | [`docs-topology/hosts/odc-device-rpi-4b.md`](../../docs-topology/hosts/odc-device-rpi-4b.md) |
| `odc-device-rpi-5` | `192.168.10.12` | [`host-configs/rpi-5.yaml`](../../host-configs/rpi-5.yaml) | [`docs-topology/hosts/odc-device-rpi-5.md`](../../docs-topology/hosts/odc-device-rpi-5.md) |

Control / server roles used by VPN and observability (not DUTs for these feature suites):

- `molt` — SSH driver, artifact collection, optional `odc-hook` counters  
- `hexa-five` — VPN / registry ground truth for VPN suite  

See also: [`docs-topology/RUNTIME_TEST_GROUND.md`](../../docs-topology/RUNTIME_TEST_GROUND.md).

## Document map

| Document | Contents |
|----------|----------|
| [HOST_ATTACHMENTS.md](HOST_ATTACHMENTS.md) | Required physical devices, ports, skip rules |
| [TEST_SUITE_OVERVIEW.md](TEST_SUITE_OVERVIEW.md) | Isolation rules, run order, PASS/FAIL/SKIP, evidence |
| [qmi/](qmi/) | Deep QMI-only suite: standard ops, discovery, cases, compliance gaps |
| [vpn/](vpn/) | Deep VPN suite: topologies, mechanism, architecture, steps, logic, cases |
| [suites/01-cellular-wan.md](suites/01-cellular-wan.md) | Cellular-only smoke cases |
| [suites/02-vpn.md](suites/02-vpn.md) | VPN-only smoke cases (WG + OpenVPN) |
| [suites/03-mavlink.md](suites/03-mavlink.md) | MAVLink full suite (FC + molt GCS sharing + VPN extender) |
| [suites/04-video-rtsp.md](suites/04-video-rtsp.md) | Video/RTSP-only cases |
| [fixtures/](fixtures/) | Checked-in minimal `system.yaml` fixtures (MAV router-only / VPN+extender) |
| [diagrams/](diagrams/) | Mermaid sources + rendered SVGs |

## Design principles

1. **One feature per suite** — fixture YAML contains only the components needed for that feature (MAV Phases C–D are the documented exception: VPN + extender after suite VPN).
2. **Real hardware first** — QMI modem and FC are first-class; missing hardware → **SKIP** (not FAIL).
3. **Same cases on both Pis** — matrix: rpi-4b and rpi-5 run identical case IDs.
4. **Observable via graph API** — assertions use `graph.status` / CLI + OS checks (`ip`, `mmcli`/`qmicli`, `journalctl`).
5. **Host sharing from molt** — MAV suite verifies LAN GCS to both DUTs and extender ops toward hexa-five ([`suites/03-mavlink.md`](suites/03-mavlink.md)).
6. **No production credentials** — lab VPN peers and APNs only (`192.168.10.0/24` lab LAN).

## Related existing docs

- Graph architecture: [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)  
- Existing combined HW E2E: [`../docs/REAL_HARDWARE_TEST_SUITE.md`](../docs/REAL_HARDWARE_TEST_SUITE.md)  
- Script catalog: [`../docs/TEST_SUITE_REFERENCE.md`](../docs/TEST_SUITE_REFERENCE.md)  

This folder **refines** the lab design into per-feature, per-device suites; it does not replace the graph unit/integration scripts under `tests/` until automation is added later.
