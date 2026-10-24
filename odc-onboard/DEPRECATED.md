# odc-onboard (v1) — Deprecated

> **Status: Deprecated.** This is the previous-generation onboard agent
> (`project(odc-onboard VERSION 0.1.0)`). It is **not built, tested, or shipped**
> by the CI/release pipeline — only [`odc-onboard-v2`](../odc-onboard-v2) is.
> It remains in the tree for historical reference and migration only.

## What v1 is

A single daemon (`odc-onboard`) that managed drone/edge connectivity with a
**fixed, hard-wired pipeline**. The subsystems were composed directly in
`src/main.cpp`:

- `qmi/` — QMI cellular modem control (modem controller, state machine,
  services, net interface, modem/hw info, watcher)
- `mavlink/` — MAVLink router
- `vpn/` — OpenVPN / WireGuard tunnels (actor-driven)
- `actors/` — long-lived actors: `vpn_manager_actor`, `vpn_monitor_actor`,
  `system_monitor_actor`, `log_actor`
- `rtsp/`, `core/`, `ipc/`, `cli/`

There is **no graph layer** in v1. The set of active features and the way they
are wired is decided at compile time in `main.cpp`; changing the topology means
changing and rebuilding the code.

## Why it was deprecated

v1's fixed-pipeline design did not scale with the project's needs:

1. **No runtime composition.** Features were wired statically in `main.cpp`.
   Enabling/disabling a link (e.g. cellular vs. Ethernet WAN, OpenVPN vs.
   WireGuard, video on/off) required a code change and a rebuild rather than
   configuration.
2. **No declarative config.** There was no schema-validated description of the
   desired system; operational intent lived in code and ad-hoc flags.
3. **Tight coupling.** Subsystems referenced each other directly, making it hard
   to add a new capability (e.g. RTSP compression, MAVLink extension,
   compression-capability detection) without touching unrelated code.
4. **Limited supervision/observability.** Lifecycle, validation, and status were
   handled per-subsystem rather than by a single runtime with a uniform control
   surface.

## The change flow: v1 → v2

`odc-onboard-v2` (`VERSION 0.2.0`) keeps the proven low-level subsystems but
replaces the hard-wired pipeline with a **graph-based runtime**.

| Concern | v1 (`odc-onboard`) | v2 (`odc-onboard-v2`) |
|---------|--------------------|------------------------|
| Topology | Hard-wired in `main.cpp` | Declarative YAML → processing **graph** |
| Composition | Compile-time | Runtime (build/validate/supervise) |
| Features | Fixed subsystems | Pluggable **components** |
| Config | Flags / in-code | Schema-validated YAML, persisted |
| Control | Per-subsystem | Uniform IPC graph interface |
| Shipped by CI | No | **Yes** (aarch64 release artifact) |

**Carried forward from v1 (largely unchanged):** the QMI modem stack
(`qmi_modem_controller`, `qmi_state_machine`, `qmi_services`, `qmi_net_iface`,
`qmi_modem_info`, `qmi_hw_info`, `qmi_watcher`), the MAVLink router, and the
VPN actors.

**Added in v2:** a `graph/` layer —
`graph_schema`, `graph_validator`, `link_engine`, `trigger_evaluator`,
`circuit_registry`, `graph_builder`, `graph_runtime`, `persistence_manager` —
plus discrete graph **components**:

- `cellular_wan_component`
- `vpn_tunnel_component`
- `mav_router_component`
- `mav_extender_component`
- `rtsp_compressor_component`
- `compression_capabilities_component`

and supporting pieces (`ipc/graph_handlers`, `rtsp/compression_capabilities`,
`qmi/qmi_usb_reset`).

In short: **v1 was a fixed connectivity daemon; v2 generalises the same
capabilities into a configurable graph runtime.** New work targets v2.

## Migration

Use the helper to convert a v1 configuration to the v2 YAML schema:

```bash
odc-onboard-v2/tools/migrate_v1_config.py
```

See [`odc-onboard-v2`](../odc-onboard-v2) for the current agent, its YAML
schema, and deployment.
