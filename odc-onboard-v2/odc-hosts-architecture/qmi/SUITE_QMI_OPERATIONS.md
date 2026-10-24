# Suite QMI operations — test cases (implemented behavior)

**Suite ID:** `ODC-DEV-QMI`  
**DUTs:** `odc-device-rpi-4b`, `odc-device-rpi-5` (identical case IDs)  
**Behavior under test:** only what `odc-onboard-v2` implements today  
**Gaps:** [COMPLIANCE_GAPS.md](COMPLIANCE_GAPS.md) (not asserted here as PASS/FAIL)

![Case map](diagrams/rendered/qmi-case-map.svg)

## Isolation fixture

Cellular-only `system.yaml` (no VPN / MAV / RTSP):

```yaml
version: 2
general:
  log_level: info
  socket: /run/odc-onboard/odc-onboard.sock

components:
  - id: cellular0
    type: cellular.qmi
    enabled: true
    config:
      device: /dev/cdc-wdm0          # from discovery
      iface: wwan0
      auto_connect: true             # parsed; see gaps
      apn: TBD                       # replace before WDS cases
      ip_type: 4
      auth_type: none
      # username / password / pin: lab secrets via env, not committed
      max_retries: 5
      backoff_base_ms: 1000
      backoff_max_ms: 60000
      ping_target: "8.8.8.8"

links: []
triggers: []
```

Backup previous config before overwrite: `/etc/odc-onboard/system.yaml.bak.qmi.<timestamp>`.

## Environment knobs

| Variable | Default | Meaning |
|----------|---------|---------|
| `ODC_QMI_DEVICE` | `/dev/cdc-wdm0` | Control device |
| `ODC_QMI_IFACE` | `wwan0` | Netdev |
| `ODC_APN` | `TBD` | Operator APN (required for WDS PASS) |
| `ODC_APN_BAD` | `odc.invalid.apn` | Wrong-APN negative |
| `ODC_QMI_USER` / `ODC_QMI_PASS` | empty | Optional auth |
| `ODC_QMI_PIN` | empty | PIN (no fixture → SKIP PIN cases) |
| `ODC_PING_TARGET` | `8.8.8.8` | Reachability |
| `ODC_SOCKET` | `/run/odc-onboard/odc-onboard.sock` | Graph IPC |

## Result rules

| Result | When |
|--------|------|
| **PASS** | Assertions hold with required fixture present |
| **FAIL** | Fixture present but behavior wrong / daemon crash |
| **SKIP** | Discovery gate or fixture missing (document reason) |

Evidence root (design): `logs/<date>/<host>/ODC-DEV-QMI/` — `graph.status.json`, journal, `qmicli` dumps, `ip addr`, discovery record.

---

## QMI-DSC — Discovery and preflight

Prerequisite: [MODEM_DISCOVERY.md](MODEM_DISCOVERY.md).

### QMI-DSC-01 — Discovery record complete

| | |
|--|--|
| **Steps** | Run DSC-1…DSC-8; attach capability row for this host. |
| **Expect** | Control + netdev + driver + model filled. |
| **Skip** | Never — infra failure → **FAIL** if board unreachable. |

### QMI-DSC-02 — Daemon + socket with cellular fixture

| | |
|--|--|
| **Steps** | Install fixture; `systemctl restart odc-onboard-v2`; check active + socket. |
| **Expect** | `active`; socket exists; `graph.status` shows `cellular0` / `cellular.qmi`. |
| **Evidence** | `systemctl status`, `graph.status.json` |

### QMI-DSC-03 — Isolation held

| | |
|--|--|
| **Steps** | Inspect `graph.status` component list. |
| **Expect** | Only `cellular0` (no vpn / mavlink / rtsp). |

---

## QMI-DMS — Device management

### QMI-DMS-01 — Identity visible

| | |
|--|--|
| **Steps** | After Initializing/HW info, read identity via journal / telemetry / optional `qmicli` when safe. |
| **Expect** | Manufacturer + model non-empty; matches discovery record. |
| **Skip** | If daemon holds port and no IPC telemetry yet — use pre-start `qmicli` from discovery. |

### QMI-DMS-02 — Operating mode online

| | |
|--|--|
| **Steps** | Observe transition through Initializing → `MODEM_ONLINE`. |
| **Expect** | Mode online (or equivalent); SimAuth entered when SIM path continues. |
| **Evidence** | Journal state transitions |

### QMI-DMS-03 — Mode persistence across soft reset

| | |
|--|--|
| **Steps** | Force Error path (e.g. no SIM already) until backoff soft reset; confirm DMS online still attempted. |
| **Expect** | Soft reset re-opens device without requiring manual `qmicli` set-online. |

---

## QMI-UIM — SIM / card

### QMI-UIM-01 — No SIM / card error → SIM_ERROR (runnable today)

| | |
|--|--|
| **Steps** | With current lab state (absent / no-ATR), start cellular0; watch SM. |
| **Expect** | Reaches SimAuth then **Error** (or stays Error after SIM_ERROR); **never** Connected; no crash. |
| **Evidence** | Journal `SIM_ERROR` / card status |

### QMI-UIM-02 — Card ready → SIM_READY

| | |
|--|--|
| **Steps** | Insert plain SIM (no PIN); restart component/daemon; observe SimAuth → Registering. |
| **Expect** | `SIM_READY`; leaves SimAuth without PIN verify. |
| **Skip** | No plain SIM |

### QMI-UIM-03 — PIN needed + PIN configured

| | |
|--|--|
| **Steps** | PIN-locked SIM + `pin` in config. |
| **Expect** | `SIM_PIN_NEEDED` → verify → `SIM_READY` (or clear Error if PIN wrong once). |
| **Skip** | **No PIN fixture in lab** |

### QMI-UIM-04 — Wrong PIN once (negative)

| | |
|--|--|
| **Steps** | PIN-locked SIM + incorrect `pin`. |
| **Expect** | Verify fails; Error; daemon up; does not loop PIN forever without backoff. |
| **Skip** | No PIN fixture |

---

## QMI-NAS — Registration and RF

### QMI-NAS-01 — Register to serving system

| | |
|--|--|
| **Steps** | Plain SIM + RF + APN not required yet; wait for `REGISTERED`. |
| **Expect** | Registering → DataConnecting (or Connected path continues). |
| **Skip** | No SIM / no RF |

### QMI-NAS-02 — Signal telemetry sanity

| | |
|--|--|
| **Steps** | While registered/connected, sample telemetry (RSSI / RSRP / SNR). |
| **Expect** | Values in plausible ranges (not all-zero forever if RF present); published on interval. |
| **Skip** | Not registered |

### QMI-NAS-03 — RF attenuation → loss

| | |
|--|--|
| **Steps** | From Connected (or Registering), attenuate / remove antenna; wait. |
| **Expect** | `BEARER_LOST` and/or registration failure → Error; backoff begins; daemon stays up. |
| **Skip** | Cannot reach Connected without SIM |

### QMI-NAS-04 — RF restore → recovery

| | |
|--|--|
| **Steps** | Restore RF after QMI-NAS-03; wait through backoff. |
| **Expect** | Soft reset path re-enters Registering / Connected without USB replug. |
| **Skip** | Same as NAS-03 |

---

## QMI-WDS — Data session

Requires: SIM ready, RF, `ODC_APN` ≠ `TBD`.

### QMI-WDS-01 — Start network happy path

| | |
|--|--|
| **Steps** | Valid APN; wait `DATA_CONNECTED` / Connected. |
| **Expect** | `QmiConnectionEvent` with IPv4; `wwan0` has address; WAN ready for graph. |
| **Skip** | APN TBD or no SIM |

### QMI-WDS-02 — Current settings applied

| | |
|--|--|
| **Steps** | Compare WDS settings (IP/GW/DNS/MTU) to `ip addr` / routes / resolv handling. |
| **Expect** | Iface configured consistently with settings (prefix/MTU). |
| **Skip** | WDS-01 skipped |

### QMI-WDS-03 — Stop network clean teardown

| | |
|--|--|
| **Steps** | From Connected, stop cellular component / disconnect request. |
| **Expect** | `DataDisconnecting` → packet stop; iface deconfigured; no orphan PDP from OS view. |
| **Skip** | WDS-01 skipped |

### QMI-WDS-04 — Wrong APN negative

| | |
|--|--|
| **Steps** | Set `apn: odc.invalid.apn` (or `ODC_APN_BAD`); start. |
| **Expect** | `DATA_FAILED` → Error → backoff; **not** Connected; daemon active. |
| **Skip** | No SIM (cannot reach DataConnecting meaningfully) — still attempt if Registering reached |

### QMI-WDS-05 — Connect / disconnect cycle hygiene

| | |
|--|--|
| **Steps** | Repeat WDS-01 ↔ WDS-03 three times. |
| **Expect** | All cycles succeed; no progressive failure suggesting CID exhaustion; logs clean. |
| **Skip** | WDS-01 skipped |

---

## QMI-SM — State machine and recovery

![State machine](diagrams/rendered/qmi-state-machine.svg)

### QMI-SM-01 — Happy-path order

| | |
|--|--|
| **Steps** | With SIM+APN+RF, record state sequence from journal. |
| **Expect** | Order compatible with: DeviceOpening → Initializing → SimAuth → Registering → DataConnecting → Connected. |
| **Skip** | Missing SIM/APN/RF |

### QMI-SM-02 — Error backoff and max retries

| | |
|--|--|
| **Steps** | Hold no-SIM (or wrong APN) so Error persists; observe backoff timing and retry count. |
| **Expect** | Exponential backoff within configured base/max + jitter; after `max_retries`, settles Idle or stops soft-reset storm; daemon alive. |
| **Evidence** | Journal timestamps |

### QMI-SM-03 — DEVICE_REMOVED / hotplug

| | |
|--|--|
| **Steps** | While running, unplug USB modem; wait Idle; replug; wait detection. |
| **Expect** | Remove → Idle (iface cleaned); add → DeviceOpening and progress per SIM state. |
| **Caution** | Physical USB only; do not force unbind in production images without recovery plan. |

### QMI-SM-04 — Daemon restart durability

| | |
|--|--|
| **Steps** | With fixture on disk, `systemctl restart odc-onboard-v2`; wait. |
| **Expect** | Service active; SM restarts from Idle/open; reaches same terminal state as cold start for current fixture (Error if no SIM; Connected if SIM+APN). |

### QMI-SM-05 — BEARER_LOST from Connected

| | |
|--|--|
| **Steps** | Same RF cut as NAS-03 while Connected. |
| **Expect** | Transition Connected → Error via `BEARER_LOST` (or documented equivalent). |
| **Skip** | Cannot reach Connected |

---

## QMI-NET — Interface and reachability

### QMI-NET-01 — raw_ip enabled for session

| | |
|--|--|
| **Steps** | During/after connect attempt, read `/sys/class/net/$IFACE/qmi/raw_ip`. |
| **Expect** | `Y` when stack configures for QMI raw-IP. |
| **Skip** | Sysfs node absent on this driver build (document). |

### QMI-NET-02 — Addressing on Connected

| | |
|--|--|
| **Steps** | `ip -br addr show $IFACE`. |
| **Expect** | IPv4 present when Connected. |
| **Skip** | Not Connected |

### QMI-NET-03 — Ping via cellular iface

| | |
|--|--|
| **Steps** | `ping -c 3 -W 3 -I $IFACE $ODC_PING_TARGET`. |
| **Expect** | At least one reply **or** telemetry `internet_reachable=true` with consistent latency field. |
| **Skip** | Not Connected / lab firewall blocks ICMP |

### QMI-NET-04 — Deconfigure on Error / disconnect

| | |
|--|--|
| **Steps** | After disconnect or Error from Connected, check iface addresses. |
| **Expect** | Session addresses removed or iface down per design; no stale default route via wwan hijacking lab eth unexpectedly (lab policy). |

---

## Phase 1 closed-gap cases (CFG / GRAPH / policy)

These assert behaviors closed in [COMPLIANCE_GAPS.md](COMPLIANCE_GAPS.md) Closed section. Hardware fixtures still apply for SKIP.

### QMI-CFG-01 — `auto_connect: false` dormancy

| | |
|--|--|
| **Steps** | Fixture with `auto_connect: false`; start component; wait; confirm `qmi_state` stays Idle (modem may be detected). Call `graph components connect cellular0`. |
| **Expect** | No PDP until connect; after connect, SM progresses per SIM/RF. |
| **Evidence** | `status_json.auto_connect`, journal, `graph.components.connect` |

### QMI-CFG-02 — YAML timeouts parsed

| | |
|--|--|
| **Steps** | Set `reg_timeout_ms: 5000` (short) with no RF/SIM so Registering times out quickly **or** inspect status/config round-trip of timeout keys. |
| **Expect** | Keys present in effective config; timeout fires near configured value when exercised. |

### QMI-NAS-POL-01 — `allow_roaming: false`

| | |
|--|--|
| **Steps** | With roaming network (or injected roaming indication in future unit test), `allow_roaming: false`. |
| **Expect** | No WDS start; Error / REGISTRATION_FAILED; clear journal reason. |
| **Skip** | Cannot force roaming in lab |

### QMI-UIM-SAFE-01 — PIN retries gate

| | |
|--|--|
| **Steps** | PIN-locked SIM with `pin_retries_left < 2` (or mock); configure any PIN; start. |
| **Expect** | No `verify_pin` QMI call; Error; `pin_retries_left` in status. |
| **Skip** | No PIN fixture |

### QMI-GRAPH-01 — Metrics port / status

| | |
|--|--|
| **Steps** | Reach Connected (or telemetry interval while registered); `graph.status` → `metrics` object; optional DSPatch consumer later. |
| **Expect** | Non-empty metrics fields after telemetry tick. |
| **Skip** | Not Connected / no telemetry yet |

### QMI-GRAPH-02 — `cellular.wan_ready` naming

| | |
|--|--|
| **Steps** | Reach Connected with IPv4; inspect `last_events` and journal / `GraphTriggerFiredEvent`. |
| **Expect** | Event string exactly `cellular.wan_ready`. |
| **Skip** | Not Connected |

---

## Phase 2 closed-gap cases (reset / profiles / multi-PDN / FCC)

### QMI-SM-HR-01 — Soft reopen + hard reset

| | |
|--|--|
| **Steps** | 1. Force Error (no SIM or wrong APN). 2. Observe soft reset reopen (`act_soft_reset` → open). 3. Call `graph components reset cellular0`. 4. Confirm `reset_stage` progresses and SM re-enters DeviceOpening. |
| **Expect** | Soft path reopens without USB; hard reset uses DMS/USB per `hard_reset_mode`; daemon stays up. |
| **Evidence** | Journal, `graph.status` `reset_stage` / `soft_fail_count` |

### QMI-WDS-PROF-01 — Ensure profile + start by index

| | |
|--|--|
| **Steps** | Fixture with `ensure_profile: true`, valid APN; connect; note `active_profile_index`; restart daemon with `profile_index` set to that index. |
| **Expect** | Profile created/modified; start by profile succeeds; status shows `active_profile_index`. |
| **Skip** | No SIM / APN TBD |

### QMI-WDS-MPDN-01 — Dual-stack / multi-PDN

| | |
|--|--|
| **Steps** | A) `ip_type: 46` single PDN — expect v4 and/or v6 in status when carrier assigns. B) Optional `pdns:` with two entries — second session starts without tearing down primary. |
| **Expect** | Primary `wan_ready` true; secondary PDN logged connected; stopping one session leaves the other (when mux capable). |
| **Skip** | No dual-stack from carrier; hardware cannot mux → document SKIP for part B |

### QMI-DMS-FCC-01 — FCC unlock gated

| | |
|--|--|
| **Steps** | With `fcc_unlock.enabled: true` on a locked SKU, start cellular; observe unlock before SimAuth. |
| **Expect** | Unlock attempted; failure → Error with clear reason (no silent Connected). Default EU modules use `enabled: false` → **SKIP**. |
| **Skip** | No locked SKU fixture |

---

## Suggested run order (per DUT)

```
MODEM_DISCOVERY
  → QMI-DSC-*
  → QMI-DMS-*
  → QMI-UIM-01 (always with current hardware)
  → QMI-UIM-02+ (when SIM available)
  → QMI-NAS-* / QMI-WDS-* / QMI-NET-* (when SIM+APN+RF)
  → QMI-SM-* interleaved with fixtures above
  → QMI-CFG-* / QMI-GRAPH-* / QMI-NAS-POL / QMI-UIM-SAFE (Phase 1 closed gaps)
  → QMI-SM-HR-01 / QMI-WDS-PROF-01 / QMI-WDS-MPDN-01 / QMI-DMS-FCC-01 (Phase 2)
  → restore system.yaml backup
```

## Matrix

| Host | Parallel? |
|------|-----------|
| rpi-4b + rpi-5 | Yes — one modem each; same case IDs; separate evidence dirs |

## Explicit non-goals (this suite)

- VPN over cellular (see `../suites/02-vpn.md`)  
- Asserting still-open Phase 2 gap items as failures (see COMPLIANCE_GAPS)  
- ModemManager / NetworkManager coexistence certification  
