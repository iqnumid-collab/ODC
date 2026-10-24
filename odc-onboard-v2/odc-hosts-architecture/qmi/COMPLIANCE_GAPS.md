# QMI compliance gaps

Separate register of **standardized / expected** cellular behaviors that `odc-onboard-v2` does **not** fully implement yet.  
These are **not** FAIL criteria for [SUITE_QMI_OPERATIONS.md](SUITE_QMI_OPERATIONS.md). Each row suggests a future test ID once the feature lands.

## Open gaps (Phase 2)

None — all Phase 2 gaps are closed in tree (see Closed below).

## How to use this register

1. **Suite authors** — do not mark GAP rows as suite FAIL.  
2. **Implementers** — when closing a gap, add the future test ID to `SUITE_QMI_OPERATIONS.md` (or a `SUITE_QMI_GAPS_CLOSED.md`) and move the row to a “Closed” section with PR/commit.  
3. **Lab** — PIN and roaming-policy fixtures still needed for full hardware proof of closed Phase 1 items. Locked-SKU fixture needed for FCC case.

## Closed gaps

| Gap ID | Closed in | Notes | Suite case |
|--------|-----------|-------|------------|
| GAP-TO-01 | Phase 1 (tree) | `open/init/sim/reg/data_timeout_ms` parsed in `parse_qmi_config` + legacy `app_lifecycle` JSON | `QMI-CFG-02` |
| GAP-AC-01 | Phase 1 (tree) | `auto_connect=false` arms watcher only; `request_connect()` / `graph.components.connect` starts PDP | `QMI-CFG-01` |
| GAP-ROAM-01 | Phase 1 (tree) | Real NAS roaming indicator; `allow_roaming` gate before WDS | `QMI-NAS-POL-01` |
| GAP-PIN-01 | Phase 1 (tree) | `pin1_retries` surfaced; verify refused when retries left &lt; 2 | `QMI-UIM-SAFE-01` |
| GAP-MET-01 | Phase 1 (tree) | `CellularMetrics` on DSPatch `metrics` port + `status_json.metrics` | `QMI-GRAPH-01` |
| GAP-EVT-01 | Phase 1 (tree) | Stable `cellular.wan_ready` via `GraphTriggerFiredEvent`, `last_events`, trigger log | `QMI-GRAPH-02` |
| GAP-RST-01 | Phase 2 (tree) | Soft reset reopens device; hard reset via DMS `RESET` and/or USB cycle; `graph.components.reset`; escalate after `hard_reset_after_soft_fails` | `QMI-SM-HR-01` |
| GAP-PROF-01 | Phase 2 (tree) | WDS 3GPP create/modify/delete; `profile_index` / `ensure_profile` / `modem_autoconnect` | `QMI-WDS-PROF-01` |
| GAP-MPDN-01 | Phase 2 (tree) | Dual-stack IPv4/IPv6 on `IpSettings`/`QmiNetIface`; concurrent PDNs via `pdns[]` + extra WDS CIDs | `QMI-WDS-MPDN-01` |
| GAP-FCC-01 | Phase 2 (tree) | Gated `fcc_unlock.enabled` with `standard` / `foxconn` / `foxconn_v2` before SIM path | `QMI-DMS-FCC-01` |

## Related reading

- Baseline ops: [QMI_STANDARD_OPERATIONS.md](QMI_STANDARD_OPERATIONS.md)  
- Implemented cases: [SUITE_QMI_OPERATIONS.md](SUITE_QMI_OPERATIONS.md)  
- Architecture: [`../../docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md) § cellular.qmi  
