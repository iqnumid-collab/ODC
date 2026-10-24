# Host attachments — real devices on `odc-device-*`

Physical and logical attachments required by the feature-isolated suites.  
Both boards use the same attachment contract unless noted.

![Host attachment map](diagrams/rendered/host-attachments.svg)

## Attachment contract (per DUT)

| Attachment | Interface / path | Used by suite | Mandatory? |
|------------|------------------|---------------|------------|
| Lab Ethernet | `eth0` → `192.168.10.11` / `.12` | SSH control plane for all suites | **Yes** |
| QMI cellular modem | `/dev/cdc-wdm0`, netdev `wwan0` (typical) | `01-cellular-wan`, WAN for `02-vpn` when testing over cellular | **Yes** for cellular suite; else SKIP |
| Flight controller | `/dev/ttyACM0` (default), baud `1500000` then `115200` | `03-mavlink` Phases A–D | **Yes** for MAVLink suite; else SKIP |
| Video source | USB camera **or** file via `odc-video-source` | `04-video-rtsp` | Lab file source is enough; camera optional |
| VPN peer configs | `/etc/odc-onboard/vpn/*.conf` / `*.ovpn` | `02-vpn`; `03-mavlink` Phases C–D | **Yes** for VPN suite; **Yes** for MAV extender phases (else SKIP C–D) |
| Onboard daemon | `odc-onboard-v2.service`, socket `/run/odc-onboard/odc-onboard.sock` | All suites | **Yes** |

## Recommended physical layout

```
                    lab LAN 192.168.10.0/24
  molt (.10) ──┬── hexa-five (.16)   [VPN server / registry]
               ├── odc-device-rpi-4b (.11)
               │     ├── USB QMI modem  → cdc-wdm0 / wwan0
               │     ├── USB FC         → ttyACM0
               │     └── (optional) USB cam → V4L2 / gst
               └── odc-device-rpi-5 (.12)
                     ├── USB QMI modem
                     ├── USB FC
                     └── (optional) USB cam
```

## Preflight probes (before any suite)

Run on each DUT over SSH. Missing mandatory attachment for a suite → **SKIP** that suite (record reason).

| Probe ID | Command / check | Pass criterion |
|----------|-----------------|----------------|
| ATT-ETH | `ip -br addr show eth0` | Address matches inventory |
| ATT-SVC | `systemctl is-active odc-onboard-v2` | `active` |
| ATT-SOCK | `test -S /run/odc-onboard/odc-onboard.sock` | Socket exists |
| ATT-QMI | `test -c /dev/cdc-wdm0` and/or `qmicli -d /dev/cdc-wdm0 --dms-get-manufacturer` | Device responds |
| ATT-WWAN | `ip -br link show wwan0` (after modem up) | Interface present when connected |
| ATT-FC | `test -c /dev/ttyACM0` | Character device present |
| ATT-VPNCFG | `ls /etc/odc-onboard/vpn/` | At least one WG or OVPN peer file |
| ATT-HEXA | From molt: `ping -c1 192.168.10.16` (or tunnel-manager health) | hexa-five reachable for VPN / MAV Phases C–D |
| ATT-GST | `gst-inspect-1.0 rtspserver` or compressor binary linked | GStreamer RTSP available |
| ATT-VTOOLS | `test -x /opt/odc/bin/odc-video-source && test -x /opt/odc/bin/odc-rtsp-monitor` | Lab tools installed |

## Fixture YAML policy

For feature isolation, each suite installs a **minimal** `system.yaml` (or temporary config path) that:

- Sets `version: 2`
- Enables **only** components for that feature (MAV Phases C–D intentionally add `vpn.*` + extender — see [`suites/03-mavlink.md`](suites/03-mavlink.md))
- Leaves VPN/MAV/RTSP/cellular out when not under test
- Avoids triggers that would auto-start unrelated services

Checked-in fixtures:

| Suite phase | Fixture |
|-------------|---------|
| MAV A–B | [`fixtures/mav-router-only.yaml`](fixtures/mav-router-only.yaml) |
| MAV C–D | [`fixtures/mav-router-vpn-extender.yaml`](fixtures/mav-router-vpn-extender.yaml) |

Persist path for lab: `/etc/odc-onboard/system.yaml` (backup before overwrite: `/etc/odc-onboard/system.yaml.bak.<suite>`).

## Environment knobs (design)

Align with [`tests/helpers/real_hardware.env.example`](../tests/helpers/real_hardware.env.example) when automation is added:

| Variable | Default | Meaning |
|----------|---------|---------|
| `ODC_QMI_DEVICE` | `/dev/cdc-wdm0` | QMI control device |
| `ODC_QMI_IFACE` | `wwan0` | Cellular netdev |
| `ODC_MAVLINK_DEVICE` | `/dev/ttyACM0` | FC UART |
| `ODC_MAVLINK_BAUD` | `1500000` | Primary baud |
| `ODC_MAVLINK_TCP_PORT` | `5760` | Primary TCP GCS (molt LAN probe) |
| `ODC_MAVLINK_UDP_PORT` | `14550` | Primary UDP GCS / extender peer port |
| `ODC_DUT_IPS` | `192.168.10.11,192.168.10.12` | Matrix targets for molt GCS sharing |
| `ODC_WG_CONFIG` | `/etc/odc-onboard/vpn/lab-wg.conf` | Per-host WG peer (MAV Phases C–D) |
| `ODC_OVPN_CONFIG` | `/etc/odc-onboard/vpn/lab-rpi.ovpn` | Per-host OVPN peer |
| `ODC_RTSP_INPUT_PORT` | `8554` | Video source / compressor in |
| `ODC_RTSP_OUTPUT_PORT` | `8555` | Compressor out / monitor |

**Dual-plane MAV verification (from molt):**

1. **LAN GCS** — TCP `DUT:5760` / UDP `DUT:14550` (`pymavlink`); record heartbeat `system_id`.  
2. **Extender plane** — after VPN `ip_ready`, assert `mav_ext_lab` `active` + rising `messages_in`/`messages_out` in `graph.status`; optional UDP to DUT tunnel IP `:14550` when molt has a route into the tunnel subnet.

Per-host peer files must differ between rpi-4b and rpi-5 (distinct keys/IPs) — see [`docs-topology/RUNTIME_TEST_GROUND.md`](../../docs-topology/RUNTIME_TEST_GROUND.md).

## Skip vs fail

| Condition | Outcome |
|-----------|---------|
| Modem absent during cellular suite | **SKIP** suite `01` |
| FC absent during MAVLink suite | **SKIP** suite `03` (that DUT) |
| VPN server unreachable | **SKIP** VPN connect cases; **SKIP** MAV Phases C–D; Phases A–B still **PASS/FAIL** |
| Daemon crash / wrong graph state with hardware present | **FAIL** |
| Wrong board IP / SSH failure | **FAIL** (infra) |
| Extender `active` without `ip_ready` | **FAIL** (MAV-C07) |
