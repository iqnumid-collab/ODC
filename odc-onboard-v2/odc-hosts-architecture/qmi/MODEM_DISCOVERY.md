# Modem discovery (generic)

Read-only procedure to identify any QMI modem on an `odc-device-*` board before running [SUITE_QMI_OPERATIONS.md](SUITE_QMI_OPERATIONS.md).  
Do **not** start/stop network or change operating mode during discovery (except optional read-only `qmicli` gets).

## When to run

- First bring-up of a board  
- After USB modem swap  
- Before any QMI suite matrix run  
- After kernel / firmware update  

Record results in the **capability record** template below (one table row per host).

---

## Procedure

Run over SSH as a user with sudo for `qmicli` on `/dev/cdc-wdm*`.

### DSC-1 — USB topology

```bash
lsusb
lsusb -t
```

**Pass criteria**

- At least one vendor cellular device present  
- An interface bound to **`qmi_wwan`** (or documented equivalent). `option` on adjacent interfaces is normal for AT/diag ports.

### DSC-2 — Control and net nodes

```bash
ls -la /dev/cdc-wdm* 2>/dev/null
ip -br link | grep -Ei 'wwan|wwp' || true
```

**Pass criteria**

- Character device `/dev/cdc-wdmN` exists  
- Sibling netdev (typically `wwan0`) exists or can be resolved after modem init  

Note the chosen pair: `ODC_QMI_DEVICE` + `ODC_QMI_IFACE`.

### DSC-3 — Driver confirmation

```bash
lsusb -t | grep -Ei 'qmi_wwan|cdc_wdm|option'
readlink -f /sys/class/net/wwan0/device/driver 2>/dev/null || true
```

**Fail** if no `qmi_wwan` (or `cdc_wdm`) binding — suite cannot proceed (SKIP all QMI cases).

### DSC-4 — Tooling

```bash
command -v qmicli
dpkg -l libqmi-utils libqmi-glib5 2>/dev/null | awk '/^ii/{print}'
systemctl is-active odc-onboard-v2 || true
test -S /run/odc-onboard/odc-onboard.sock && echo sock_ok
```

### DSC-5 — Identity (read-only)

```bash
DEV=/dev/cdc-wdm0   # adjust
sudo qmicli -p -d "$DEV" --dms-get-manufacturer
sudo qmicli -p -d "$DEV" --dms-get-model
sudo qmicli -p -d "$DEV" --dms-get-revision
sudo qmicli -p -d "$DEV" --dms-get-ids
sudo qmicli -p -d "$DEV" --dms-get-operating-mode
```

Use `-p` so qmi-proxy can coexist with the daemon when it holds the port. If the daemon owns the device exclusively and `-p` still fails, stop the service **only for discovery**, then restart — document that exception in the run log.

### DSC-6 — SIM slot report (read-only)

```bash
sudo qmicli -p -d "$DEV" --uim-get-card-status
```

Classify into: `ready` | `pin-needed` | `absent` | `error` (e.g. no-ATR) | `unknown`.

### DSC-7 — Optional WDA / raw-ip peek

```bash
sudo qmicli -p -d "$DEV" --wda-get-data-format 2>/dev/null || true
cat /sys/class/net/wwan0/qmi/raw_ip 2>/dev/null || true
```

Do **not** write raw_ip during discovery.

### DSC-8 — Fill capability record

Copy a row into the table below (or append a dated section).

---

## Capability record template

| Field | Example | Host value |
|-------|---------|------------|
| Date | ISO date | |
| Host | `odc-device-rpi-5` | |
| Lab IP | `192.168.10.12` | |
| USB VID:PID | `2cb7:0104` | |
| Manufacturer | Fibocom… | |
| Model | FM160-… | |
| Revision | | |
| IMEI (last 4 only in shared logs) | | |
| Control device | `/dev/cdc-wdm0` | |
| Netdev | `wwan0` | |
| Kernel driver | `qmi_wwan` | |
| Operating mode | online | |
| SIM slot1 class | absent / ready / … | |
| qmicli version / path | `/usr/bin/qmicli` | |
| Notes | 5G vs LTE, quirks | |

---

## Lab snapshot — 2026-07-19

### odc-device-rpi-4b (`192.168.10.11`)

| Field | Value |
|-------|-------|
| USB | `1508:1001` Fibocom Modem |
| Manufacturer | Fibocom Wireless Inc. |
| Model | NL668-EAU-10-01 |
| Control / net | `/dev/cdc-wdm0` / `wwan0` (DOWN at snapshot) |
| Driver | `qmi_wwan` on data IF; `option` on AT IFs |
| Operating mode | online |
| SIM | Slot[1] `error: no-atr-received (3)` |
| Suite impact | UIM no-SIM/error cases **runnable**; WDS/NAS connect **SKIP** until usable SIM |

### odc-device-rpi-5 (`192.168.10.12`)

| Field | Value |
|-------|-------|
| USB | `2cb7:0104` Fibocom FM160 Modem |
| Manufacturer | Fibocom Wireless Inc. |
| Model | FM160-EAU-00 5G Module |
| Control / net | `/dev/cdc-wdm0` / `wwan0` (DOWN at snapshot) |
| Driver | `qmi_wwan` + `option` |
| Operating mode | online |
| SIM | Slot[1] `absent` |
| Suite impact | Same as 4b: connect cases SKIP until SIM inserted |

---

## Discovery → suite gating

| Discovery result | Suite action |
|------------------|--------------|
| No `cdc-wdm*` / no `qmi_wwan` | **SKIP** entire QMI suite |
| Modem OK, SIM absent/error | Run DSC/DMS/UIM-error/hotplug; **SKIP** NAS register + WDS connect |
| Modem OK, SIM ready, APN still `TBD` | Run through NAS if RF; **SKIP** WDS until APN filled in env |
| Modem OK, SIM ready, APN set | Full suite eligible |
| PIN needed, no PIN in config | **SKIP** PIN cases; treat as blocked SimAuth if daemon cannot proceed |

Re-run discovery after inserting a SIM and update this file’s snapshot section (or a dated appendix).
