# QMI-only cellular test suite

**Status:** design only (markdown + diagrams; no automation executed from this folder).  
**Scope:** deep, feature-isolated QMI / `cellular.qmi` compliance testing on `odc-device-*`.

![QMI case map](diagrams/rendered/qmi-case-map.svg)

## Purpose

Define the **standardized cellular operations** that `odc-onboard-v2` must implement, the **use cases** it must handle on real QMI modems, and the **test cases** that prove implemented behavior — separately from a **compliance gap register** for operations not yet implemented.

| Layer | Document | Role |
|-------|----------|------|
| Shallow smoke | [`../suites/01-cellular-wan.md`](../suites/01-cellular-wan.md) | Board-level CEL-01… smoke (WAN up / wan_ready) |
| **This folder** | Deep QMI suite | Service-by-service ops, state machine, fixtures, gaps |

Run the shallow suite for quick board health; use this folder when validating QMI stack conformance.

## DUT / modem matrix

Suite is **modem-model agnostic**: always run [MODEM_DISCOVERY.md](MODEM_DISCOVERY.md) first and fill the capability record. Snapshot from lab discovery (2026-07-19):

| Host | IP | USB ID | Manufacturer | Model | Control | Netdev | Driver | SIM (at discovery) |
|------|-----|--------|--------------|-------|---------|--------|--------|--------------------|
| `odc-device-rpi-4b` | `192.168.10.11` | `1508:1001` | Fibocom Wireless Inc. | NL668-EAU-10-01 (LTE) | `/dev/cdc-wdm0` | `wwan0` | `qmi_wwan` | Slot1 `error: no-atr-received` |
| `odc-device-rpi-5` | `192.168.10.12` | `2cb7:0104` | Fibocom Wireless Inc. | FM160-EAU-00 5G Module | `/dev/cdc-wdm0` | `wwan0` | `qmi_wwan` | Slot1 `absent` |

Both boards have `qmicli` and an online DMS operating mode. Until a usable SIM is inserted, **WDS connect / NAS register cases SKIP** (not FAIL).

## Lab fixtures (agreed)

| Fixture | Available | Used by |
|---------|-----------|---------|
| Plain SIM (no PIN), APN `TBD` | Planned | WDS happy path |
| Wrong / invalid APN | Yes (config) | WDS negative |
| No SIM / SIM removed | Yes (current hardware state) | UIM `SIM_ERROR` |
| RF attenuation / antenna remove | Yes | NAS / BEARER_LOST |
| PIN-locked SIM | **No** | UIM PIN cases stay SKIP |
| One modem per Pi | Yes | Parallel matrix |

## Document map

| File | Contents |
|------|----------|
| [QMI_STANDARD_OPERATIONS.md](QMI_STANDARD_OPERATIONS.md) | Compliance baseline: standard ops mapped to code |
| [MODEM_DISCOVERY.md](MODEM_DISCOVERY.md) | Generic discovery procedure + capability record |
| [SUITE_QMI_OPERATIONS.md](SUITE_QMI_OPERATIONS.md) | Test cases (implemented behavior only) |
| [COMPLIANCE_GAPS.md](COMPLIANCE_GAPS.md) | Separate gap register for unimplemented std ops |
| [diagrams/](diagrams/) | Mermaid sources + rendered SVGs |

## Isolation policy

Inherited from [`../TEST_SUITE_OVERVIEW.md`](../TEST_SUITE_OVERVIEW.md):

- Fixture YAML contains **only** `cellular.qmi` (no VPN / MAV / RTSP).
- Missing modem → **SKIP**; missing SIM → **SKIP** connect cases; wrong behavior with hardware present → **FAIL**.
- APN / credentials are placeholders (`TBD`); never bake production secrets into docs.

## Related code

| Area | Path |
|------|------|
| Services (DMS/NAS/WDS/UIM/WDA) | [`../../src/qmi/qmi_services.cpp`](../../src/qmi/qmi_services.cpp) |
| State machine + transitions | [`../../src/qmi/qmi_modem_controller.cpp`](../../src/qmi/qmi_modem_controller.cpp) |
| Config | [`../../src/qmi/qmi_config.hpp`](../../src/qmi/qmi_config.hpp) |
| Hotplug | [`../../src/qmi/qmi_watcher.cpp`](../../src/qmi/qmi_watcher.cpp) |
| Graph wrapper | [`../../src/graph/components/cellular_wan_component.cpp`](../../src/graph/components/cellular_wan_component.cpp) |

## External references

- [libqmi / qmicli](https://www.freedesktop.org/wiki/Software/libqmi/)
- [Sixfab: QMI data connection with libqmi](https://docs.sixfab.com/page/setting-up-a-data-connection-over-qmi-interface-using-libqmi)
- [Techship: qmicli + qmi_wwan step-by-step](https://techship.com/support/faq/how-to-step-by-step-set-up-a-data-connection-over-qmi-interface-using-qmicli-and-in-kernel-driver-qmi-wwan-in-linux/)
- [libqmi-devel: CID / `--client-no-release-cid` semantics](https://lists.freedesktop.org/archives/libqmi-devel/2017-September/002425.html)
