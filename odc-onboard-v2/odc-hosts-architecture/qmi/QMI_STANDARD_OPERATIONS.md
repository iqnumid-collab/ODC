# QMI standard operations — compliance baseline

Operations a Linux QMI WAN stack (libqmi + `qmi_wwan`) is expected to perform, mapped to what `odc-onboard-v2` implements today.

![Standard QMI workflow](diagrams/rendered/qmi-standard-workflow.svg)

## Canonical connect workflow (industry reference)

Manual `qmicli` sequence used by Sixfab / Techship / libqmi community docs:

1. Open device (prefer `-p` / qmi-proxy when sharing the port)
2. `dms-get-operating-mode` → `dms-set-operating-mode=online` if needed
3. Bring `wwan*` down → set sysfs `qmi/raw_ip=Y` → bring iface up
4. Optional: `wda-get-data-format` confirms raw-IP
5. `wds-start-network` with APN / auth / ip-type, **retain WDS CID** while connected
6. `wds-get-current-settings` (or DHCP) → configure IP/GW/DNS/MTU
7. Data plane: ping / route via `wwan*`
8. Teardown: `wds-stop-network`, release CID, deconfigure iface

`odc-onboard-v2` performs the same sequence inside `QmiServices` + `QmiNetIface` + the modem controller state machine (not via shelling out to `qmicli`).

---

## 1. Device open and client IDs

| Standard expectation | odc-onboard-v2 | Code |
|----------------------|----------------|------|
| Open QMI control device | `qmi_device_new` + `qmi_device_open` | `qmi_services.cpp` `open_device` |
| Net flags: raw-IP, no QoS header | `PROXY \| NET_RAW_IP \| NET_NO_QOS_HEADER` | same |
| Allocate CIDs for DMS, NAS, WDS, UIM, WDA | `qmi_device_allocate_client` ×5 | `allocate_clients` |
| Keep WDS CID for the life of the PDP session | Clients held on `QmiServices` until `close` / stop | `start_network` / `stop_network` / `close` |
| Close / release cleanly | `qmi_device_close_async` + client release | `close` |

**Use cases that must be handled**

- Modem already present at daemon start
- Modem plugged after start (`DEVICE_DETECTED`)
- Open timeout → Error + backoff

---

## 2. DMS — Device Management Service

| Operation | Standard | Implementation |
|-----------|----------|----------------|
| Get operating mode | Required before data | `set_modem_online` reads mode, sets ONLINE if needed |
| Set operating mode ONLINE | Required | DMS `SetOperatingMode(ONLINE)` |
| Manufacturer / model / revision / HW revision / IDs | Inventory | `QmiHwInfo` collectors |
| IMEI / related IDs | Inventory | DMS `GetIds` (+ UIM IMSI/ICCID via DMS-UIM helpers) |

**Use cases**

- Cold start with modem in low-power / offline mode → force online
- Collect HW identity once per session (`HW_INFO_DONE`)
- Persist identity for `graph.status` / telemetry consumers

---

## 3. UIM — SIM / card status and PIN

| Operation | Standard | Implementation |
|-----------|----------|----------------|
| Get card status | Detect absent / error / PIN / ready | `get_sim_status` → UIM `GetCardStatus` |
| Verify PIN1 | When PIN locked and PIN configured | `verify_pin` → UIM `VerifyPin` slot 1 |
| Fail closed on bad SIM | No silent “connected” without card | `SIM_ERROR` → Error state |

**Card states the stack must distinguish**

| Observed state | Expected SM event | Lab note |
|----------------|-------------------|----------|
| Card ready, no PIN | `SIM_READY` | Plain SIM fixture |
| PIN1 required | `SIM_PIN_NEEDED` | No lab PIN SIM → cases SKIP |
| Card absent | `SIM_ERROR` | Current rpi-5 |
| Card error (e.g. no-ATR) | `SIM_ERROR` | Current rpi-4b |

**PIN safety rule (compliance expectation)**

- Never auto-retry PIN verify when remaining attempts &lt; 2 (avoid permanent lock).  
- Current code: verifies when `cfg.pin` non-empty and `pin_retries_left >= 2` (or unknown); refuses otherwise.

---

## 4. NAS — Network Access Service

| Operation | Standard | Implementation |
|-----------|----------|----------------|
| Register for serving-system indications | Prefer indications over pure poll | `register_indications` |
| Poll serving system if needed | Fallback | `GetServingSystem` |
| Emit registered → start data | Required for connect path | `REGISTERED` → `DataConnecting` |
| Signal strength | Telemetry | NAS `GetSignalStrength` (RSSI, LTE RSRP, LTE SNR) |
| Roaming indicator | Telemetry / policy input | Read in `QmiModemInfo`; indication callback currently hardcodes `roaming=false` |

**Use cases**

- Register on home PLMN with plain SIM + RF
- Registration timeout / failure → Error + backoff
- RF loss while registered / connected → `BEARER_LOST` or reg failure path
- Reacquire after RF restored (via Error backoff or reconnect)

---

## 5. WDS — Wireless Data Service

| Operation | Standard | Implementation |
|-----------|----------|----------------|
| Start network (APN, IP family, auth) | Core | `start_network` — APN, ip-type, PAP/CHAP user/pass |
| Get current settings | IP / GW / DNS / MTU / prefix | `get_ip_settings` |
| Stop network | Clean teardown | `stop_network` with packet handle |
| Packet service status | Diagnostics | Telemetry `GetPacketServiceStatus` |
| Profile list (3GPP) | Optional inventory | Telemetry `GetProfileList` |
| Bearer / packet-service down indication | Required | WDS `packet-service-status` → `BEARER_LOST` path |

**Use cases**

- Happy path: correct APN → IPv4 on `wwan0` → `QmiConnectionEvent` / WAN ready
- Wrong APN → `DATA_FAILED` → Error → backoff (no crash)
- Disconnect request → `DataDisconnecting` → cleanup → back toward Registering
- Repeated connect/disconnect without CID exhaustion

---

## 6. WDA — Wireless Data Administrative

| Operation | Standard | Implementation |
|-----------|----------|----------------|
| Get data format | Confirm raw-IP vs 802.3 | Telemetry / HW path `GetDataFormat` |
| Align with kernel raw_ip | Required for most modern modules | `QmiNetIface::set_raw_ip` writes `Y` to sysfs |

---

## 7. Netiface / OS integration (non-QMI but mandatory)

| Operation | Standard | Implementation |
|-----------|----------|----------------|
| Set raw_ip sysfs | Before data | `qmi_net_iface.cpp` |
| Bring iface up/down | Required | `ip link` helpers |
| Apply IP/GW/DNS/MTU from WDS settings | Preferred over DHCP-only | `configure` / `deconfigure` |
| Optional reachability probe | Health | `ping -I <iface> <ping_target>` |

**Use cases**

- IP from WDS settings applied without requiring `udhcpc`
- Clean deconfigure on disconnect / error / device remove
- Ping via cellular iface (not eth0 default route only)

---

## 8. Hotplug / lifecycle

| Operation | Standard | Implementation |
|-----------|----------|----------------|
| Detect `cdc-wdm*` add/bind | Required | `qmi_watcher` on `usbmisc` (+ `net` for sibling iface) |
| Detect remove/unbind | Required | → `DEVICE_REMOVED` → Idle |
| Soft recovery | Soft reopen after Error | `act_soft_reset` → `act_open_device` + backoff |
| Hard modem reset / FCC unlock | Vendor / region specific | Hard reset: DMS `RESET` + USB cycle (`graph.components.reset`); FCC: gated `fcc_unlock` |

---

## 9. State machine contract

![QMI state machine](diagrams/rendered/qmi-state-machine.svg)

States (`qmi_events.hpp`):

`Idle` → `DeviceOpening` → `Initializing` → `SimAuth` → `Registering` → `DataConnecting` → `Connected` ⇄ `DataDisconnecting` | `Error`

Mandatory observable outcomes for operators / tests:

| Outcome | Observable |
|---------|------------|
| Connected | `QmiState::Connected` + non-empty IPv4 in `QmiConnectionEvent` |
| WAN ready for VPN binding | `CellularWanComponent` `WanReady` / graph WAN port |
| Error with retries | Error → backoff → soft reset until `max_retries` |
| Removed modem | Idle; no crash loop |

---

## 10. Graph / config surface (cellular.qmi)

Keys accepted by `CellularWanComponent` / `QmiConfig` (see also YAML schema):

| Key | Role |
|-----|------|
| `device` / `device_path` | `/dev/cdc-wdmN` |
| `iface` / `iface_name` | `wwan0` |
| `apn`, `username`, `password`, `auth_type`, `ip_type` | WDS |
| `pin` | UIM |
| `auto_connect` | App-level: when false, arm watcher only until `graph.components.connect` |
| `max_retries`, backoff_* | Error recovery |
| `hard_reset_after_soft_fails`, `hard_reset_mode`, `hard_reset_settle_ms` | Soft→hard escalation / operator reset |
| `profile_index`, `ensure_profile`, `modem_autoconnect` | WDS 3GPP profile path |
| `pdns[]` | Multi-PDN sessions (omit → single top-level session) |
| `fcc_unlock` | Gated FCC unlock before SIM (`enabled` default false) |
| `watchdog_interval_s`, `ping_target`, `modem_info_interval_s` | Telemetry / health |
| `*_timeout_ms` | Parsed from YAML into `QmiConfig` |

---

## Summary: must-handle use cases

1. Modem present at boot → full connect to Connected  
2. Hotplug modem after boot → same  
3. No SIM / bad ATR → SimAuth error, no false Connected  
4. PIN SIM (when fixture exists) → verify then register  
5. Correct APN → IP + WAN ready  
6. Wrong APN → DATA_FAILED + backoff, daemon stays up  
7. RF cut while Connected → BEARER_LOST / Error recovery  
8. Operator disconnect → clean stop_network + iface down  
9. USB unplug → Idle; replug → reconnect  
10. Daemon restart with fixture → recover toward Connected  

Cases for unimplemented expectations live only in [COMPLIANCE_GAPS.md](COMPLIANCE_GAPS.md).
All Phase 2 gaps (hard reset, profiles, multi-PDN, FCC unlock) are closed in tree; see Closed section.
