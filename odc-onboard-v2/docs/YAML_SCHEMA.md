# odc-onboard-v2 YAML Schema Reference

This document describes the field-level schema for `/etc/odc-onboard/system.yaml`.
See also [`ARCHITECTURE.md`](ARCHITECTURE.md) for design rationale and runtime behavior.

## Top-Level Structure

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `version` | integer | yes | `2` | Schema version. Must be `2` for graph mode. |
| `general` | object | no | see below | Daemon-wide settings. |
| `components` | array | yes | `[]` | Component instances in the dataflow graph. |
| `links` | array | no | `[]` | Transport binding edges between component ports. |
| `triggers` | array | no | `[]` | Rules that activate or deactivate links on events. |

Example skeleton:

```yaml
version: 2

general:
  log_level: info
  socket: /run/odc-onboard/odc-onboard.sock

components: []
links: []
triggers: []
```

## `general`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `log_level` | string | `info` | Global log level: `trace`, `debug`, `info`, `warn`, or `error`. |
| `socket` | string | `/run/odc-onboard/odc-onboard.sock` | Unix domain socket path for JSON-RPC IPC. |

## `components[]`

Each entry declares one graph node.

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `id` | string | yes | — | Unique component identifier referenced by links and triggers. |
| `type` | string | yes | — | Component factory type (see catalog below). |
| `enabled` | boolean | no | `true` | Whether the component is loaded and started at runtime. |
| `config` | object | no | `{}` | Type-specific configuration payload. |

### Supported component types

| Type | Purpose |
|------|---------|
| `cellular.qmi` | QMI modem / WAN interface management |
| `vpn.wireguard` | WireGuard VPN tunnel |
| `vpn.openvpn` | OpenVPN tunnel |
| `mavlink.router` | Primary MAVLink router |
| `mavlink.extender` | VPN-bound MAVLink extender |
| `rtsp.compressor` | RTSP ingest/compress/republish bridge |
| `compression.capabilities` | Host GPU/encoder inventory (auto-injected at runtime if omitted) |

### Component port registry

| Component type | Output ports | Input ports |
|----------------|-------------|-------------|
| `cellular.qmi` | `wan`, `metrics` | — |
| `vpn.wireguard`, `vpn.openvpn` | `tunnel`, `state` | `wan` |
| `mavlink.router` | `router` | — |
| `mavlink.extender` | `status` | `transport`, `upstream` |
| `rtsp.compressor` | `status`, `metrics` | `transport` |
| `compression.capabilities` | `status`, `inventory` | — |

### `cellular.qmi` config

| Field | Type | Description |
|-------|------|-------------|
| `device` | string | QMI device path, e.g. `/dev/cdc-wdm0` |
| `iface` | string | Network interface name, e.g. `wwan0` |
| `auto_connect` | boolean | If true, start PDP on component start / modem detect; if false, arm watcher only until `graph.components.connect` |
| `allow_roaming` | boolean | If false, refuse WDS start when NAS reports roaming (default true) |
| `apn` | string | Carrier APN |
| `ip_type` | integer | IP type preference (`4`, `6`, or `46` dual-stack) |
| `auth_type` | string | Authentication type |
| `username` | string | PPP auth username |
| `password` | string | PPP auth password |
| `pin` | string | SIM PIN |
| `profile_index` | integer | If ≥0, `StartNetwork` uses this 3GPP profile |
| `ensure_profile` | boolean | Create/modify modem profile from APN before start |
| `modem_autoconnect` | boolean | Modem-side WDS autoconnect (distinct from app `auto_connect`) |
| `pdns` | array | Optional multi-PDN sessions (`id`, `apn`, `iface`, `ip_type`, `profile_index`, …). Omit for single top-level session |
| `hard_reset_after_soft_fails` | integer | Soft-fail count before USB/DMS hard reset (default 2; 0 = never auto) |
| `hard_reset_mode` | string | `dms`, `usb`, or `dms_then_usb` |
| `hard_reset_settle_ms` | integer | Delay after hard reset before reopen |
| `fcc_unlock` | object | `{ enabled: false, method: standard\|foxconn\|foxconn_v2 }` |
| `open_timeout_ms` | integer | Device open timeout (default 30000) |
| `init_timeout_ms` | integer | Client alloc / init timeout (default 30000) |
| `sim_timeout_ms` | integer | SIM auth timeout (default 30000) |
| `reg_timeout_ms` | integer | NAS registration timeout (default 120000) |
| `data_timeout_ms` | integer | WDS start-network timeout (default 30000) |
| `max_retries` | integer | Soft-reset retries after Error |
| `backoff_base_ms` / `backoff_max_ms` / `backoff_jitter_ms` | integer | Error backoff |
| `watchdog_interval_s` | integer | Connected watchdog interval |
| `ping_target` | string | Reachability probe host (via wwan iface) |
| `modem_info_interval_s` | integer | Telemetry poll interval |

### `vpn.wireguard` / `vpn.openvpn` config

| Field | Type | Description |
|-------|------|-------------|
| `config_path` | string | Path to WireGuard `.conf` or OpenVPN `.ovpn` file |
| `creds_path` | string | Encrypted credentials file (OpenVPN only) |
| `wan` | string | Component id of upstream `cellular.qmi` WAN |
| `name` | string | Human-readable connection name |
| `auto_start` | boolean | Start tunnel automatically |
| `max_retries` | integer | Reconnect retry limit |
| `health_check` | object | Ping-based health check settings |

Health check object:

| Field | Type | Description |
|-------|------|-------------|
| `enabled` | boolean | Enable periodic health checks |
| `interval_sec` | integer | Check interval in seconds |
| `ping_host` | string | Host to ping through the tunnel |

### `mavlink.router` config

| Field | Type | Description |
|-------|------|-------------|
| `tcp_server_port` | integer | TCP server port for router clients |
| `dedup_period_sec` | integer | Duplicate-message suppression window |
| `sniffer_sysid` | integer | Sniffer system id |
| `report_stats` | boolean | Publish routing statistics |
| `endpoints` | array | UART/UDP/TCP endpoint definitions |
| `logging` | object | MAVLink log capture settings |

Endpoint entry:

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | `uart`, `udp`, or `tcp` |
| `name` | string | Endpoint label |
| `device` | string | Serial device path (`uart`) |
| `baud` | integer or array | Baud rate(s) (`uart`) |
| `address` | string | Bind/connect address (`udp`/`tcp`) |
| `port` | integer | Port number (`udp`/`tcp`) |
| `mode` | string | `server` or `client` (`udp`/`tcp`) |
| `flow_control` | boolean | Hardware flow control (`uart`) |

### `mavlink.extender` config

| Field | Type | Description |
|-------|------|-------------|
| `udp_port` | integer | Local UDP port for the extender |
| `auto` | boolean | Allow trigger-driven startup when transport binding is ready |

### `rtsp.compressor` config

| Field | Type | Description |
|-------|------|-------------|
| `output_codec` | string | Output codec, e.g. `h265`, `h264`, or `copy` |
| `output_bitrate_kbps` | integer | Target output bitrate |
| `output_width` | integer | Output frame width |
| `output_height` | integer | Output frame height |
| `output_framerate` | integer | Output frame rate |
| `input_port` | integer | RTSP ingest port on VPN target host |
| `input_path` | string | RTSP ingest path |
| `output_port` | integer | Local republish port |
| `output_path` | string | Local republish path |
| `input_use_tcp` | boolean | Use TCP for RTSP ingest |
| `hw_policy` | string | `auto` (default: prefer HW when inventory supports the codec), `force_hw`, or `force_sw` |
| `use_hw_codec` | boolean | Optional explicit override; when set, maps to `force_hw` / `force_sw` |

At graph start the daemon probes GStreamer encoder plugins / GPU paths, autosaves the inventory to `/var/lib/odc-onboard/compression-capabilities.json`, and applies `hw_policy` via that inventory before the bridge starts. Unsupported codecs are rejected.

### `compression.capabilities` config

| Field | Type | Description |
|-------|------|-------------|
| `auto_refresh_on_start` | boolean | If true (default), ensure inventory is loaded/detected when the component starts |

If this component is omitted from YAML, the runtime auto-injects `compression-capabilities` so inventory remains queryable via `graph.status` / `graph.capabilities`.

## `links[]`

Links bind a source component port to a target component port and declare which transport fields are copied into the binding.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | yes | Unique link identifier referenced by triggers. |
| `from` | object | yes | Source endpoint (`component`, `port`). |
| `to` | object | yes | Target endpoint (`component`, `port`). |
| `bind` | object | no | Field binding specification. |

Endpoint object:

| Field | Type | Description |
|-------|------|-------------|
| `component` | string | Source or target component id |
| `port` | string | Port name from the component port registry |

`bind.fields` is a list of transport field names propagated across the link, commonly:

- `connection_id`
- `interface_name`
- `target_ip`
- `router_ref`

## `triggers[]`

Triggers activate or deactivate links in response to graph events.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | yes | Unique trigger identifier. |
| `on` | string | yes | Event name (see supported events). |
| `source` | string | yes | Component id that emits the event. |
| `activate` | array | no | Link ids to activate when the event fires. |
| `deactivate` | array | no | Link ids to deactivate when the event fires. |

### Supported trigger events

| Event | Emitted by | Meaning |
|-------|------------|---------|
| `vpn.connected` | `VpnTunnelComponent` | VPN state reached Connected |
| `vpn.disconnected` | `VpnTunnelComponent` | VPN disconnected or error |
| `vpn.ip_ready` | `VpnTunnelComponent` | Connected and IPv4 assigned |
| `cellular.wan_ready` | `CellularWanComponent` | Modem connected, WAN interface up. Emitted as `GraphTriggerFiredEvent.on_event`, recorded in component `last_events`, and used to unblock VPN `wan` binding. |
| `component.manual_start` | IPC `graph.components.start` | Operator override |

## Validation rules

`GraphValidator` enforces:

- Every `link.from.component` and `link.to.component` references an existing component id
- Port names exist for the given component type
- `vpn.*.config.wan` references a `cellular.*` component when present
- No duplicate component ids
- Trigger `source` references a VPN component
- Trigger `activate` / `deactivate` lists reference existing link ids
- RTSP `input_port` ≠ `output_port` when both bind to the same host

## Persistence

Runtime mutations through `graph.*` IPC methods persist back to the config file using atomic write:

1. Write to `<config>.tmp`
2. Copy existing file to `<config>.bak` if present
3. Rename temp file to final path

Default config path: `/etc/odc-onboard/system.yaml` (override with `--config`).

## Migration from v1 JSON

Use `tools/migrate_v1_config.py` for best-effort conversion:

```bash
python3 tools/migrate_v1_config.py /etc/odc-onboard/config.json -o /etc/odc-onboard/system.yaml
```

| v1 JSON section | v2 YAML mapping |
|-----------------|-----------------|
| `general` | `general` |
| `vpn.wireguard.connections[]` | `components[]` type `vpn.wireguard` |
| `vpn.openvpn.connections[]` | `components[]` type `vpn.openvpn` |
| `mavlink` | `components[]` type `mavlink.router` |
| `mavlink.extender_auto` | `components[]` type `mavlink.extender` + `links[]` + `triggers[]` |
| `rtsp` | `components[]` type `rtsp.compressor` + links/triggers when `auto_start_on_vpn` |
| `qmi` | `components[]` type `cellular.qmi` |

## Example

See [`config/system.example.yaml`](../config/system.example.yaml) for a full multi-VPN reference configuration.
