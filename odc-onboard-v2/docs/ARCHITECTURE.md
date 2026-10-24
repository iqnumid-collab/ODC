# odc-onboard-v2 — Graph Architecture & Implementation Plan

**Version:** 0.1 (draft)  
**Date:** 2026-07-01  
**Status:** Design specification — pending open-question resolution  
**Framework:** [DSPatch](https://github.com/cross-platform/dspatch) (`cross-platform/dspatch`)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Goals, Scope, and Constraints](#2-goals-scope-and-constraints)
3. [Current State (v1) — Findings](#3-current-state-v1--findings)
4. [Design Decisions](#4-design-decisions)
5. [Target Architecture](#5-target-architecture)
6. [DSPatch Integration Model](#6-dspatch-integration-model)
7. [YAML System Schema](#7-yaml-system-schema)
8. [Component Catalog](#8-component-catalog)
9. [Link Semantics and Transport Binding](#9-link-semantics-and-transport-binding)
10. [Trigger Model](#10-trigger-model)
11. [Runtime Interaction Flows](#11-runtime-interaction-flows)
12. [Graph-First IPC and CLI API](#12-graph-first-ipc-and-cli-api)
13. [Persistence Model](#13-persistence-model)
14. [Threading and Concurrency](#14-threading-and-concurrency)
15. [Directory Layout](#15-directory-layout)
16. [Phased Implementation Roadmap](#16-phased-implementation-roadmap)
17. [Testing Strategy](#17-testing-strategy)
18. [Migration from v1](#18-migration-from-v1)
19. [Risks and Mitigations](#19-risks-and-mitigations)
20. [Open Questions](#20-open-questions)
21. [References](#21-references)

---

## 1. Executive Summary

odc-onboard-v2 is a **greenfield rewrite** of the onboard connectivity daemon. It replaces hardcoded subsystem wiring (VPN auto-spawn, MAVLink extenders, RTSP compressors, cellular coupling) with a **single global DSPatch dataflow circuit** whose topology is declared in **YAML** and can be **mutated at runtime** through a **graph-first IPC/CLI API**.

Key properties:

- **One circuit, many services:** All VPN tunnels, cellular WAN, MAVLink router/extenders, and RTSP compressors live in one DSPatch `Circuit`.
- **Links configure transport, not bytes:** Graph links do not route MAVLink frames or RTP media through DSPatch buses. Links **bind** service components to VPN tunnel interface names and IP addresses so traffic is destined for the correct transport.
- **Runtime-first:** Components, links, and triggers can be added, removed, and reconfigured live via IPC. Changes **persist back to YAML** on disk.
- **Cellular as primary WAN:** The QMI modem component provides the upstream network; VPN tunnels run over cellular.
- **No dummy overrides:** Source is copied from `odc-onboard/` into `odc-onboard-v2/` and refactored in place. v1 managers are not wrapped with shims.

---

## 2. Goals, Scope, and Constraints

### 2.1 Goals

| Goal | Description |
|------|-------------|
| **Declarative topology** | Users define components and links in YAML instead of implicit auto-spawn flags scattered across config sections. |
| **Runtime graph control** | Operators add/remove VPNs, MAVLink extenders, and RTSP compressors without restarting the daemon. |
| **Correct transport binding** | When a VPN connects, linked MAVLink and RTSP instances automatically target the tunnel interface and IP. |
| **Configurable RTSP** | Input/output ports, paths, and codec settings are schema-driven (fixes v1 hardcoded `:8554/stream`). |
| **Multi-VPN independence** | Multiple VPN connections operate simultaneously, each with its own linked service subgraph. |

### 2.2 In Scope (v2.0)

- VPN: WireGuard and OpenVPN as graph components
- Cellular: QMI modem as primary WAN component
- MAVLink: Primary router + per-VPN extender components
- RTSP: One compressor instance per linked VPN
- Graph runtime: DSPatch circuit builder, link engine, trigger evaluator
- YAML schema loader, validator, and persistence
- Graph-first JSON-RPC IPC over Unix domain socket
- Test suite ported from v1 RTSP/MAVLink/VPN tests

### 2.3 Out of Scope (v2.0)

- DSPatcher GUI authoring tool integration
- Moving raw MAVLink/RTP bytes through DSPatch buses (data plane stays in existing socket/GStreamer pipelines)
- Bonded/aggregated multi-VPN traffic splitting
- Legacy `vpn.*` / `mavlink.*` / `rtsp.*` IPC as primary API (graph-first only)
- Non-Linux platforms

### 2.4 Constraints

- Linux onboard target with `CAP_NET_ADMIN` for TUN/netlink
- GStreamer + GLib required for RTSP compressor (inherited from v1 `RtspBridge`)
- Header-only DSPatch dependency (no additional shared library for the graph engine)
- Single-binary deployment model preserved

---

## 3. Current State (v1) — Findings

These findings from v1 source and documentation directly motivate the v2 design.

| ID | Finding | Impact on v2 |
|----|---------|--------------|
| F1 | Subsystems are separate managers (`VpnManager`, `MavExtenderManager`, `RtspCompressorManager`) wired through a pub/sub `EventBus`, not a unified topology. | Replace implicit wiring with explicit YAML links. |
| F2 | Auto-spawn listens to `VpnInterfaceEvent::AddressAdded`. | Change trigger to `VpnState::Connected` per design decision; optionally sub-stage for IP-ready. |
| F3 | `VpnInterfaceMonitor` only emits events for interfaces registered to a VPN `connection_id` via `iface_to_connection_` map. Raw TUN devices created in tests never trigger auto-spawn. | Graph components emit structured tunnel outputs directly from VPN connection state, not from orphan netlink events. |
| F4 | RTSP `input_uri` and `output_port` are hardcoded to port 8554 and path `/stream` in `rtsp_compressor_manager.hpp`. | All RTSP transport parameters become YAML-configurable. |
| F5 | Config is monolithic JSON with sections (`vpn`, `mavlink`, `rtsp`, `qmi`) and no link concept. | Replace with YAML `components[]`, `links[]`, `triggers[]`. |
| F6 | Each RTSP compressor runs in a dedicated GLib thread with its own `GMainLoop`. | Preserve thread model inside `RtspCompressorComponent`; binding updates via thread-safe queue. |
| F7 | Architecture docs omit RTSP from the IPC table. | v2 docs cover all graph component types and IPC methods. |

### 3.1 v1 Architecture (reference)

```
┌─────────────┐     EventBus      ┌──────────────────────┐
│ VpnManager  │ ───────────────▶  │ MavExtenderManager   │
│             │                   │ (auto-spawn on IP)   │
└─────────────┘                   └──────────────────────┘
       │                                    │
       │ EventBus                           │ UDP over tunnel
       ▼                                    ▼
┌──────────────────────┐           ┌──────────────┐
│ RtspCompressorManager│           │  MavRouter   │
│ (auto-spawn on IP)   │           │  (primary)   │
└──────────────────────┘           └──────────────┘
```

Problem: coupling is implicit, trigger conditions differ per manager, and transport parameters are hardcoded.

---

## 4. Design Decisions

Decisions captured during architecture review:

| Topic | Decision |
|-------|----------|
| Graph framework | **DSPatch** (`cross-platform/dspatch`) |
| Link semantics | Auto-configure MAVLink/RTSP components to use VPN interface name and IP as transport destination |
| Config format | **YAML** with `components[]`, `links[]`, `triggers[]` |
| Graph topology | **One global circuit** for all services |
| VPN activation trigger | **`VpnState::Connected`** (not `AddressAdded`) |
| Cellular role | **Primary WAN** — VPN tunnels run over cellular |
| MAVLink model | **Both** primary router + per-VPN extender components |
| RTSP model | **One compressor per VPN link** |
| Multi-VPN | **Independent** simultaneous tunnels, each with own linked services |
| IPC strategy | **Graph-first** — `graph.*` methods are the primary API |
| Runtime changes | Fully dynamic via IPC; **persist to YAML** on disk |
| Migration | **Greenfield v2** — copy `odc-onboard/` sources, refactor; no dummy overrides |

---

## 5. Target Architecture

### 5.1 High-Level Block Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         odc-onboard-v2 daemon                           │
│                                                                         │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────────────────┐ │
│  │ YAML Schema  │───▶│ GraphBuilder │───▶│ CircuitRegistry          │ │
│  │ (on disk)    │    │              │    │ (single global Circuit)  │ │
│  └──────────────┘    └──────────────┘    └────────────┬─────────────┘ │
│         ▲                                               │               │
│         │ persist                                       ▼               │
│  ┌──────────────┐                              ┌──────────────────────┐ │
│  │ Persistence  │◀─────────────────────────────│ GraphRuntime         │ │
│  │ Manager      │                              │ - trigger evaluator  │ │
│  └──────────────┘                              │ - link engine        │ │
│         ▲                                       │ - apply/patch        │ │
│         │                                       └──────────┬───────────┘ │
│  ┌──────────────┐                                          │             │
│  │ IPC Server   │──────────────────────────────────────────┘             │
│  │ graph.* API  │                                                        │
│  └──────────────┘                                                        │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────────┐ │
│  │                    DSPatch Circuit (single)                       │ │
│  │                                                                     │ │
│  │  [cellular0] ──wan──▶ [vpn_cloud] ──tunnel──┬──▶ [mav_ext_cloud]  │ │
│  │                          [vpn_site] ──tunnel─┼──▶ [mav_ext_site]   │ │
│  │                                              └──▶ [rtsp_cloud]     │ │
│  │  [mav_primary] ──router──▶ [mav_ext_cloud]                        │ │
│  │                              └──▶ [mav_ext_site]                  │ │
│  └─────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5.2 Control Plane vs Data Plane

| Plane | Mechanism | What flows |
|-------|-----------|------------|
| **Control / binding** | DSPatch circuit + YAML links + triggers | VPN state, interface/IP, component lifecycle, `TransportBinding` structs |
| **Data** | Existing sockets and GStreamer pipelines | MAVLink frames (UDP/TCP), RTSP/RTP media bytes |

DSPatch component output buses emit **binding descriptors**, not media:

```cpp
struct TransportBinding {
    std::string connection_id;
    std::string interface_name;
    std::string target_ip;       // IPv4 on tunnel
    int         prefix_len{24};
    VpnState    vpn_state;
    bool        ip_ready{false}; // true once IPv4 assigned
};
```

Service components (MAVLink extender, RTSP compressor) consume `TransportBinding` and configure their data-plane endpoints accordingly.

---

## 6. DSPatch Integration Model

### 6.1 Why DSPatch

[DSPatch](https://github.com/cross-platform/dspatch) is a header-only C++ dataflow framework providing:

- `Component` — processing node with named input/output buses
- `Circuit` — container wiring components together
- `Process()` — synchronous tick executing the graph

It maps naturally to odc-onboard-v2's needs: declarative topology, parallel component threads, and runtime rewiring.

### 6.2 Mapping to odc-onboard

| DSPatch concept | odc-onboard-v2 usage |
|-----------------|----------------------|
| `Component` | One instance per YAML component (`vpn_cloud`, `mav_ext_cloud`, etc.) |
| Input bus | Receives `TransportBinding`, WAN readiness, router handles |
| Output bus | Emits `TransportBinding`, status, metrics |
| `Circuit` | Single global circuit built from YAML |
| `Process()` | Called on orchestrator tick to propagate binding updates |

### 6.3 Component Base Class

All v2 graph components inherit from a common base:

```cpp
namespace odc::graph {

class OdcComponent : public DSPatch::Component {
public:
    virtual std::string type() const = 0;
    virtual std::string id() const = 0;
    virtual nlohmann::json status() const = 0;
    virtual void configure(const nlohmann::json& config) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

} // namespace odc::graph
```

### 6.4 Circuit Build Sequence

1. Parse YAML → `GraphSchema` struct
2. For each `components[]` entry, factory-create `OdcComponent` subclass
3. Register components in `CircuitRegistry`
4. For each `links[]` entry, wire DSPatch buses (or register link in `LinkEngine`)
5. Load `triggers[]` into `TriggerEvaluator`
6. Call `circuit->Tick()` on state changes

---

## 7. YAML System Schema

### 7.1 Top-Level Structure

```yaml
version: 2

general:
  log_level: info
  socket: /run/odc-onboard/odc-onboard.sock

components: []   # component instances
links: []        # transport binding edges
triggers: []     # activation rules
```

### 7.2 Component Entry

```yaml
components:
  - id: cellular0
    type: cellular.qmi
    enabled: true
    config:
      device: /dev/cdc-wdm0
      iface: wwan0
      auto_connect: true
      apn: internet

  - id: vpn_cloud
    type: vpn.wireguard
    enabled: true
    config:
      config_path: /etc/odc-onboard/vpn/cloud.conf
      wan: cellular0          # references another component id

  - id: vpn_site
    type: vpn.openvpn
    enabled: true
    config:
      config_path: /etc/odc-onboard/vpn/site.ovpn
      creds_path: /etc/odc-onboard/vpn/site.creds
      wan: cellular0

  - id: mav_primary
    type: mavlink.router
    enabled: true
    config:
      tcp_server_port: 5760
      dedup_period_sec: 5
      endpoints:
        - type: uart
          name: flight_controller
          device: /dev/ttyACM0
          baud: [1500000, 115200]
        - type: udp
          name: gcs
          address: 0.0.0.0
          port: 14550
          mode: server

  - id: mav_ext_cloud
    type: mavlink.extender
    enabled: true
    config:
      udp_port: 14550
      auto: true

  - id: mav_ext_site
    type: mavlink.extender
    enabled: true
    config:
      udp_port: 14551
      auto: true

  - id: rtsp_cloud
    type: rtsp.compressor
    enabled: true
    config:
      output_codec: h265
      output_bitrate_kbps: 800
      output_width: 1280
      output_height: 720
      output_framerate: 30
      input_port: 8554
      input_path: /stream
      output_port: 8555
      output_path: /stream
      input_use_tcp: true
```

### 7.3 Link Entry

Links declare **transport binding** from a source component port to a target component port:

```yaml
links:
  - id: cloud_mavlink
    from:
      component: vpn_cloud
      port: tunnel
    to:
      component: mav_ext_cloud
      port: transport
    bind:
      fields: [connection_id, interface_name, target_ip]

  - id: cloud_rtsp
    from:
      component: vpn_cloud
      port: tunnel
    to:
      component: rtsp_cloud
      port: transport
    bind:
      fields: [connection_id, interface_name, target_ip]

  - id: site_mavlink
    from:
      component: vpn_site
      port: tunnel
    to:
      component: mav_ext_site
      port: transport
    bind:
      fields: [connection_id, interface_name, target_ip]

  - id: router_to_ext_cloud
    from:
      component: mav_primary
      port: router
    to:
      component: mav_ext_cloud
      port: upstream
    bind:
      fields: [router_ref]
```

### 7.4 Trigger Entry

Triggers define when links activate or deactivate:

```yaml
triggers:
  - id: cloud_services_on_connect
    on: vpn.connected
    source: vpn_cloud
    activate: [cloud_mavlink, cloud_rtsp]

  - id: cloud_services_on_disconnect
    on: vpn.disconnected
    source: vpn_cloud
    deactivate: [cloud_mavlink, cloud_rtsp]

  - id: site_mavlink_on_connect
    on: vpn.connected
    source: vpn_site
    activate: [site_mavlink]

  - id: site_mavlink_on_disconnect
    on: vpn.disconnected
    source: vpn_site
    deactivate: [site_mavlink]
```

### 7.5 Supported Trigger Events

| Event | Emitted by | Meaning |
|-------|------------|---------|
| `vpn.connected` | `VpnTunnelComponent` | VPN state reached Connected |
| `vpn.disconnected` | `VpnTunnelComponent` | VPN disconnected or error |
| `vpn.ip_ready` | `VpnTunnelComponent` | Connected AND IPv4 assigned (optional sub-trigger) |
| `cellular.wan_ready` | `CellularWanComponent` | Modem connected, WAN interface up |
| `component.manual_start` | IPC `graph.components.start` | Operator override |

### 7.6 Component Port Registry

| Component type | Output ports | Input ports |
|----------------|-------------|-------------|
| `cellular.qmi` | `wan`, `metrics` | — |
| `vpn.wireguard`, `vpn.openvpn` | `tunnel`, `state` | `wan` |
| `mavlink.router` | `router` | — |
| `mavlink.extender` | `status` | `transport`, `upstream` |
| `rtsp.compressor` | `status`, `metrics` | `transport` |
| `compression.capabilities` | `status`, `inventory` | — |

### 7.7 Validation Rules

The `GraphValidator` enforces:

- Every `link.from.component` and `link.to.component` references an existing component id
- Port names exist for the given component type
- `vpn.*.config.wan` references a `cellular.*` component
- No duplicate component ids
- Trigger `source` references a VPN component
- Trigger `activate`/`deactivate` lists reference existing link ids
- RTSP `input_port` ≠ `output_port` when both bind to the same host (warn/error)

---

## 8. Component Catalog

### 8.1 `cellular.qmi`

**Source (v1):** `src/qmi/qmi_modem_controller.hpp`

| Property | Value |
|----------|-------|
| Purpose | Manage 4G/5G modem; provide primary WAN |
| Outputs | `wan_ready` event with interface name and IP |
| Thread | QMI event thread + orchestrator callbacks |
| Config keys | `device`, `iface`, `apn`, `username`, `password`, `pin`, `auto_connect` |

**Behavior:**

1. Start modem, connect to network
2. Emit `wan_ready` on `wwan0` (or configured iface) up
3. Connected VPN components consume `wan` input to know upstream is available

---

### 8.2 `vpn.wireguard` / `vpn.openvpn`

**Source (v1):** `src/vpn/vpn_manager.hpp`, `wireguard_connection.hpp`, `openvpn_connection.hpp`

| Property | Value |
|----------|-------|
| Purpose | Manage one VPN tunnel |
| Inputs | `wan` — optional reference to cellular component |
| Outputs | `tunnel` — `TransportBinding`; `state` — VPN state changes |
| Trigger | Emits `vpn.connected` / `vpn.disconnected` on state transitions |
| Config keys | `config_path`, `creds_path` (OpenVPN), `wan`, `enabled`, health check settings |

**Behavior:**

1. Wait for WAN ready (if `wan` specified)
2. Start VPN connection
3. On `Connected`: emit `TransportBinding` with interface name; update with IP when assigned
4. On disconnect/error: emit disconnected, tear down linked services via trigger

---

### 8.3 `mavlink.router`

**Source (v1):** `src/mavlink/mav_router.hpp`

| Property | Value |
|----------|-------|
| Purpose | Primary MAVLink router bridging FC ↔ GCS |
| Outputs | `router` — opaque handle/reference for extenders |
| Config keys | `tcp_server_port`, `endpoints[]`, `dedup_period_sec`, logging settings |
| Always on | Yes, when `enabled: true` |

---

### 8.4 `mavlink.extender`

**Source (v1):** `src/mavlink/mav_extender_manager.hpp` (refactored to single-instance component)

| Property | Value |
|----------|-------|
| Purpose | Tunnel MAVLink over VPN to remote endpoint |
| Inputs | `transport` — `TransportBinding` from VPN; `upstream` — router reference |
| Outputs | `status` — running/stopped, packet stats |
| Config keys | `udp_port`, `auto` (armed by trigger) |
| Thread | One thread per extender instance |

**Binding application:**

When `transport` receives a binding with `ip_ready: true`:

```
extender.bind(binding):
  local_addr = binding.target_ip
  interface  = binding.interface_name
  start UDP client → primary router via tunnel
```

---

### 8.5 `rtsp.compressor`

**Source (v1):** `src/rtsp/rtsp_compressor_manager.hpp`, `thirdparty/minimal-cmpr/RtspBridge`

| Property | Value |
|----------|-------|
| Purpose | Ingest RTSP, transcode (e.g. H.265 720p), serve compressed stream |
| Inputs | `transport` — `TransportBinding` from VPN |
| Outputs | `status`, `metrics` (bandwidth, frame count) |
| Thread | Dedicated GLib thread + `GMainLoop` per instance |
| Config keys | `input_port`, `input_path`, `output_port`, `output_path`, codec, bitrate, resolution, `hw_policy` |

At graph start, `CompressionCapabilities` probes GPU/encoder plugins, autosaves
`/var/lib/odc-onboard/compression-capabilities.json`, and exposes inventory via
`compression.capabilities` / IPC `graph.capabilities`. Each compressor applies
`hw_policy` (`auto` / `force_hw` / `force_sw`) against that inventory before
`bridge->start()`.

**Binding application:**

```cpp
BridgeConfig cfg;
cfg.input_uri   = "rtsp://" + binding.target_ip
                + ":" + std::to_string(config.input_port)
                + config.input_path;
cfg.output_port = config.output_port;
cfg.output_path = config.output_path;
// codec, bitrate, resolution from component config
compression_caps->apply_rules(cfg, hw_policy);
bridge->start(cfg);
```

This replaces v1 hardcoded:

```cpp
cfg.input_uri   = "rtsp://" + entry->target_ip + ":8554/stream";
cfg.output_port = entry->output_port;  // always 8554
```

---

## 9. Link Semantics and Transport Binding

### 9.1 Definition

A **link** is a declarative edge that tells the runtime:

> When the source component's port emits a binding, copy the specified fields to the target component's port, which configures that service's data-plane transport.

Links do **not**:

- Route packets through DSPatch
- Replace VPN routing tables
- Modify iptables/netfilter rules (unless explicitly added later)

Links **do**:

- Propagate `connection_id`, `interface_name`, `target_ip` to MAVLink/RTSP components
- Activate/deactivate target components via triggers
- Enable runtime rewiring without code changes

### 9.2 Binding Propagation Example

```
vpn_cloud.tunnel emits:
  { connection_id: "vpn_cloud", interface_name: "wg-cloud", target_ip: "10.77.5.2", ip_ready: true }

link cloud_mavlink (bind: [connection_id, interface_name, target_ip])
  → mav_ext_cloud.transport receives binding
  → mav_ext_cloud starts UDP extender on 10.77.5.2 via wg-cloud

link cloud_rtsp (bind: [connection_id, interface_name, target_ip])
  → rtsp_cloud.transport receives binding
  → rtsp_cloud starts RtspBridge with input_uri rtsp://10.77.5.2:8554/stream
```

### 9.3 Multiple Independent VPNs

Each VPN component emits its own `tunnel` output. Links are scoped per VPN:

```
vpn_cloud  ──▶ mav_ext_cloud, rtsp_cloud
vpn_site   ──▶ mav_ext_site
```

No cross-coupling unless explicitly linked in YAML.

---

## 10. Trigger Model

### 10.1 Evaluation

The `TriggerEvaluator` subscribes to component state events:

```
on vpn.connected from vpn_cloud:
  activate links: [cloud_mavlink, cloud_rtsp]
    → LinkEngine.propagate(vpn_cloud.tunnel → targets)
    → mav_ext_cloud.start()
    → rtsp_cloud.start()

on vpn.disconnected from vpn_cloud:
  deactivate links: [cloud_mavlink, cloud_rtsp]
    → mav_ext_cloud.stop()
    → rtsp_cloud.stop()
```

### 10.2 Connected vs IP-Ready

Design decision: primary trigger is `vpn.connected`.

**Recommended sub-staging** (pending final confirmation):

| Stage | Event | Action |
|-------|-------|--------|
| 1 | `vpn.connected` | Link activated; components enter "armed" state |
| 2 | `vpn.ip_ready` | Components receive full binding with IP; data plane starts |

This avoids starting RTSP ingest before an IP exists on the tunnel.

---

## 11. Runtime Interaction Flows

### 11.1 Startup

```
1. Parse CLI: odc-onboard-v2 --config /etc/odc-onboard/system.yaml --daemon
2. Load YAML → GraphSchema
3. GraphValidator.validate()
4. GraphBuilder.build() → single Circuit
5. Start cellular0 → wait for wan_ready
6. Start mav_primary (always-on router)
7. Start enabled VPN components
8. IPC server listen on socket
9. Enter main loop: orchestrator tick + DSPatch Process()
```

### 11.2 VPN Connect → Services

```
cellular0          vpn_cloud           GraphRuntime         mav_ext_cloud       rtsp_cloud
    │                   │                    │                     │                  │
    │── wan_ready ─────▶│                    │                     │                  │
    │                   │── Connected ──────▶│                     │                  │
    │                   │                    │── evaluate triggers │                  │
    │                   │                    │── propagate links ──▶│── start extender │
    │                   │                    │── propagate links ────────────────────▶│── start bridge
    │                   │                    │                     │                  │
```

### 11.3 Runtime Link Addition (IPC)

```
Client ── graph.links.add ──▶ GraphRuntime
                                  │
                                  ├── GraphValidator
                                  ├── LinkEngine.register(link)
                                  ├── PersistenceManager.write(yaml)
                                  └── if VPN already connected: propagate immediately
```

### 11.4 Graceful Shutdown

Teardown order (reverse dependency):

```
1. Stop all RTSP compressors (GLib threads)
2. Stop all MAVLink extenders
3. Stop MAVLink router
4. Disconnect all VPNs
5. Stop cellular modem
6. Destroy circuit
7. Remove socket file
```

---

## 12. Graph-First IPC and CLI API

### 12.1 Design Principle

The `graph.*` namespace is the **primary** management API. Legacy v1 methods (`vpn.start`, `rtsp.start`, etc.) are not exposed in v2.0.

Transport: JSON-RPC 2.0 over Unix domain socket (same as v1).

### 12.2 Method Reference

#### `graph.status`

Returns full graph state.

**Response:**

```json
{
  "version": 2,
  "components": [
    {
      "id": "vpn_cloud",
      "type": "vpn.wireguard",
      "enabled": true,
      "state": "connected",
      "binding": {
        "interface_name": "wg-cloud",
        "target_ip": "10.77.5.2",
        "ip_ready": true
      }
    }
  ],
  "links": [
    { "id": "cloud_mavlink", "active": true }
  ],
  "triggers": [
    { "id": "cloud_services_on_connect", "on": "vpn.connected", "source": "vpn_cloud" }
  ]
}
```

#### `graph.components.list`

List all component instances with summary status.

#### `graph.components.add`

Add a new component at runtime.

**Params:**

```json
{
  "id": "vpn_new",
  "type": "vpn.wireguard",
  "enabled": true,
  "config": { "config_path": "/etc/odc-onboard/vpn/new.conf", "wan": "cellular0" }
}
```

Persists to YAML, builds new circuit node.

#### `graph.components.remove`

**Params:** `{ "id": "vpn_new" }`

Stops component, removes links referencing it, persists.

#### `graph.components.configure`

**Params:**

```json
{
  "id": "rtsp_cloud",
  "config": { "output_port": 8556 }
}
```

Patches config, restarts affected component if running.

#### `graph.components.start` / `graph.components.stop`

Manual lifecycle override (bypasses triggers).

#### `graph.components.connect`

**Params:** `{ "id": "cellular0" }`

Explicit PDP connect for `cellular.qmi` when `auto_connect: false` (arms the modem watcher on `start`, then `connect` begins the QMI state machine). No-op / error for non-cellular types.

#### `graph.links.add`

**Params:**

```json
{
  "id": "new_link",
  "from": { "component": "vpn_new", "port": "tunnel" },
  "to": { "component": "mav_ext_new", "port": "transport" },
  "bind": { "fields": ["connection_id", "interface_name", "target_ip"] }
}
```

#### `graph.links.remove`

**Params:** `{ "id": "new_link" }`

#### `graph.triggers.set`

Create or replace a trigger rule. Persists to YAML.

#### `graph.apply`

Validate and rebuild affected circuit nodes after batch changes.

**Params (optional):** `{ "component_ids": ["rtsp_cloud"] }`

#### `graph.validate`

Dry-run validation; returns errors without applying.

### 12.3 CLI REPL

The interactive CLI mirrors all `graph.*` methods:

```
odc> graph status
odc> graph components add vpn_new vpn.wireguard config_path=/etc/odc-onboard/vpn/new.conf wan=cellular0
odc> graph links add cloud_rtsp vpn_cloud:tunnel rtsp_cloud:transport
odc> graph apply
```

---

## 13. Persistence Model

### 13.1 Write Path

Every mutating IPC call follows:

```
IPC mutation
  → GraphValidator.validate(diff)
  → GraphRuntime.apply(diff)
  → PersistenceManager.write_atomic(path, updated_yaml)
```

### 13.2 Atomic Write

```
1. Write to /etc/odc-onboard/system.yaml.tmp
2. fsync
3. rename() → /etc/odc-onboard/system.yaml
4. Keep /etc/odc-onboard/system.yaml.bak (previous version)
```

### 13.3 Default Config Path

```
/etc/odc-onboard/system.yaml
```

Override via `--config` CLI flag.

---

## 14. Threading and Concurrency

### 14.1 Thread Ownership

| Component | Thread |
|-----------|--------|
| GraphRuntime | Orchestrator main + thread pool |
| DSPatch Process() | Orchestrator tick (single-threaded per tick) |
| VPN connections | One thread per connection (from v1) |
| MAVLink router | Router thread pool (from v1) |
| MAVLink extender | One thread per extender (from v1) |
| RTSP compressor | One GLib thread per instance (from v1) |
| IPC server | Accept thread + per-request orchestrator tasks |

### 14.2 Graph Mutation Lock

All runtime graph mutations acquire `GraphMutex`:

```cpp
{
    std::unique_lock lock(graph_mutex_);
    link_engine_.add(link);
    persistence_.write(schema_);
    circuit_.patch(affected_ids);
}
```

DSPatch `Process()` is called outside the lock to avoid blocking IPC.

### 14.3 Binding Updates to GLib Threads

RTSP and other GLib-owned components receive binding updates via `g_idle_add()` or a thread-safe queue consumed on the GLib main loop.

---

## 15. Directory Layout

```
odc-onboard-v2/
├── CMakeLists.txt
├── docs/
│   └── ARCHITECTURE.md              ← this document
├── config/
│   └── system.example.yaml
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── app_lifecycle.hpp/cpp    # trimmed from v1
│   │   ├── event_bus.hpp            # retained for domain events
│   │   ├── log_control.hpp
│   │   └── privilege_check.hpp
│   ├── graph/
│   │   ├── graph_schema.hpp/cpp     # YAML ↔ typed model
│   │   ├── graph_validator.hpp/cpp
│   │   ├── graph_builder.hpp/cpp    # schema → DSPatch Circuit
│   │   ├── graph_runtime.hpp/cpp    # lifecycle, triggers, apply
│   │   ├── link_engine.hpp/cpp      # binding propagation
│   │   ├── trigger_evaluator.hpp/cpp
│   │   ├── circuit_registry.hpp/cpp
│   │   ├── persistence_manager.hpp/cpp
│   │   ├── transport_binding.hpp
│   │   └── components/
│   │       ├── odc_component.hpp
│   │       ├── cellular_wan_component.hpp/cpp
│   │       ├── vpn_tunnel_component.hpp/cpp
│   │       ├── mav_router_component.hpp/cpp
│   │       ├── mav_extender_component.hpp/cpp
│   │       └── rtsp_compressor_component.hpp/cpp
│   ├── vpn/                         # copied from v1, adapted
│   ├── mavlink/                     # copied from v1, adapted
│   ├── rtsp/                        # copied from v1, ports configurable
│   ├── qmi/                         # copied from v1
│   ├── ipc/
│   │   ├── ipc_server.hpp
│   │   ├── method_registry.hpp
│   │   └── graph_handlers.hpp/cpp   # graph.* methods
│   └── cli/
│       ├── cli_repl.hpp/cpp
│       └── graph_commands.hpp/cpp
├── thirdparty/
│   ├── dspatch/                     # header-only, submodule or copy
│   ├── minimal-cmpr/                # from v1
│   └── thread_orchestrator/         # from v1
└── tests/
    ├── helpers/
    │   └── test_helpers.sh
    ├── test_graph_schema.sh
    ├── test_graph_vpn_binding.sh
    ├── test_graph_mavlink_extender.sh
    ├── test_graph_rtsp_e2e.sh
    └── run_all.sh
```

---

## 16. Phased Implementation Roadmap

### Phase 0 — Foundation (Week 1–2)

| Task | Description | Exit Criteria |
|------|-------------|---------------|
| 0.1 | Copy `odc-onboard/` → `odc-onboard-v2/` | Directory exists, builds (initially same as v1) |
| 0.2 | Add DSPatch header-only dependency | `#include <DSPatch.h>` compiles |
| 0.3 | Create `src/graph/` module skeleton | Empty graph module links |
| 0.4 | Integrate YAML parser (yaml-cpp) | Parse example YAML to struct |
| 0.5 | Implement `GraphValidator` (structural rules) | Invalid YAML rejected with errors |
| 0.6 | Minimal daemon: load YAML, expose `graph.status` | IPC returns empty graph JSON |

---

### Phase 1 — Cellular + VPN Components (Week 3–4)

| Task | Description | Exit Criteria |
|------|-------------|---------------|
| 1.1 | `CellularWanComponent` wrapping QMI controller | `wan_ready` output on modem connect |
| 1.2 | `VpnTunnelComponent` wrapping WireGuard/OpenVPN | `tunnel` output on Connected |
| 1.3 | Wire `config.wan` reference to cellular component | VPN waits for WAN before connect |
| 1.4 | Multiple independent VPN components | Two VPNs connect simultaneously |
| 1.5 | IPC: `graph.components.add/remove/configure` | Runtime CRUD persists to YAML |
| 1.6 | Emit `vpn.connected` / `vpn.disconnected` events | TriggerEvaluator receives events |

---

### Phase 2 — Link Engine + Triggers (Week 5)

| Task | Description | Exit Criteria |
|------|-------------|---------------|
| 2.1 | Implement `LinkEngine` binding propagation | Binding copied per link field spec |
| 2.2 | Implement `TriggerEvaluator` | Links activate/deactivate on VPN state |
| 2.3 | IPC: `graph.links.add/remove`, `graph.triggers.set` | Runtime link CRUD persists |
| 2.4 | IPC: `graph.apply`, `graph.validate` | Batch apply with validation |
| 2.5 | Tests: `test_graph_vpn_binding.sh` | Link/unlink via IPC, YAML round-trip |

---

### Phase 3 — MAVLink Graph Integration (Week 6–7)

| Task | Description | Exit Criteria |
|------|-------------|---------------|
| 3.1 | `MavRouterComponent` (always-on primary router) | Router starts from YAML config |
| 3.2 | `MavExtenderComponent` (one per YAML entry) | Extender starts on binding |
| 3.3 | Link router → extender upstream | Extender uses primary router |
| 3.4 | Link VPN tunnel → extender transport | Auto-start on `vpn.connected` trigger |
| 3.5 | Remove v1 `MavExtenderManager` auto-spawn path | Only graph triggers spawn extenders |
| 3.6 | Tests: `test_graph_mavlink_extender.sh` | E2E MAVLink over VPN tunnel |

---

### Phase 4 — RTSP Graph Integration (Week 8–9)

| Task | Description | Exit Criteria |
|------|-------------|---------------|
| 4.1 | `RtspCompressorComponent` wrapping RtspBridge | GLib thread lifecycle managed |
| 4.2 | Configurable input/output ports and paths | No hardcoded 8554 |
| 4.3 | Link VPN tunnel → RTSP transport | One compressor per VPN |
| 4.4 | Build `input_uri` from binding + config | Correct ingest URL per tunnel |
| 4.5 | Metrics output → `graph.status` | Bandwidth visible in status |
| 4.6 | Tests: `test_graph_rtsp_e2e.sh` | Port v1 57-test user E2E suite |

---

### Phase 5 — Hardening + Documentation (Week 10–11)

| Task | Description | Exit Criteria |
|------|-------------|---------------|
| 5.1 | Incremental `graph.apply` (no full restart) | Patch single component without global stop |
| 5.2 | Graceful shutdown ordering | Clean teardown, no orphaned threads |
| 5.3 | CLI REPL graph commands | All IPC methods available interactively |
| 5.4 | `config/system.example.yaml` | Complete working example |
| 5.5 | Full `run_all.sh` test suite green | All graph tests pass |
| 5.6 | YAML schema reference doc | Field-level documentation |

---

### Phase 6 — Migration + Cutover (Week 12+)

| Task | Description | Exit Criteria |
|------|-------------|---------------|
| 6.1 | v1 `config.json` → v2 YAML migration tool | Best-effort conversion script |
| 6.2 | Field deployment on target hardware | Production validation |
| 6.3 | v1 deprecation notice | README updated in both directories |

---

## 17. Testing Strategy

### 17.1 Test Layers

| Layer | Tests | Privilege |
|-------|-------|-----------|
| Schema | `test_graph_schema.sh` — YAML load, validate, reject invalid | No |
| VPN binding | `test_graph_vpn_binding.sh` — connect, link, IPC CRUD, persist | Root |
| MAVLink | `test_graph_mavlink_extender.sh` — extender over TUN/VPN | Root |
| RTSP E2E | `test_graph_rtsp_e2e.sh` — video source → compressor → monitor | Mixed |
| Integration | `run_all.sh` — all suites sequential | Mixed |

### 17.2 Key Test Scenarios

1. **YAML round-trip:** IPC add component → verify YAML on disk → restart daemon → state restored
2. **Multi-VPN:** Two VPNs connected; independent extenders/compressors per link
3. **Trigger activation:** VPN connect activates links; disconnect deactivates
4. **Runtime link add:** VPN already connected; add new link via IPC; service starts immediately
5. **RTSP port config:** Different input/output ports per compressor; no port collision
6. **Graceful shutdown:** No segfault, no orphaned `odc-onboard-v2` or compressor processes

### 17.3 Test Tools (from v1)

| Tool | Purpose |
|------|---------|
| `tools/odc-video-source` | RTSP test video source |
| `tools/odc-rtsp-monitor` | Bandwidth verification |
| `tests/helpers/test_helpers.sh` | Assertion library |

---

## 18. Migration from v1

### 18.1 Source Copy Strategy

```bash
cp -a odc-onboard/ odc-onboard-v2/
# Then refactor in odc-onboard-v2/ only:
# - Add src/graph/
# - Remove MavExtenderManager auto-spawn
# - Remove RtspCompressorManager auto-spawn
# - Replace JSON config loader with YAML graph loader
# - Replace IPC handlers with graph.*
```

### 18.2 Config Migration (best-effort)

| v1 JSON section | v2 YAML mapping |
|-----------------|-----------------|
| `general` | `general` |
| `vpn.wireguard.connections[]` | `components[]` type `vpn.wireguard` |
| `vpn.openvpn.connections[]` | `components[]` type `vpn.openvpn` |
| `mavlink` | `components[]` type `mavlink.router` |
| `mavlink.extender_auto` | `triggers[]` + `links[]` (manual conversion) |
| `rtsp` | `components[]` type `rtsp.compressor` + links |
| `qmi` | `components[]` type `cellular.qmi` |

Auto-spawn flags (`auto_start_on_vpn`, `extender_auto.enabled`) become explicit `triggers[]` entries.

### 18.3 Parallel Operation

v1 (`odc-onboard`) and v2 (`odc-onboard-v2`) run as separate binaries during transition. Only one should bind the IPC socket at a time.

---

## 19. Risks and Mitigations

| Risk | Severity | Mitigation |
|------|----------|------------|
| DSPatch circuit mutation during `Process()` | High | `GraphMutex`; queue mutations; apply between ticks |
| GLib thread binding race | High | Binding updates via `g_idle_add` / lock-free queue |
| YAML corruption on crash mid-write | Medium | Atomic write (tmp + rename) + `.bak` |
| `vpn.connected` before IP assigned | Medium | Sub-stage `vpn.ip_ready`; components wait for IP |
| Runtime API abuse (adding broken links) | Medium | `graph.validate` dry-run; strict validator |
| v1 test parity gaps | Low | Port all v1 RTSP/MAVLink tests to graph IPC |
| DSPatch learning curve | Low | Start with thin `OdcComponent` wrapper; few nodes initially |

---

## 20. Open Questions

These items require stakeholder confirmation before locking the v2 spec:

| # | Question | Options | Impact |
|---|----------|---------|--------|
| 1 | **Connected vs IP-ready:** Should MAVLink/RTSP start on `vpn.connected` or wait for `vpn.ip_ready`? | Immediate / Wait for IP / Two-stage arm+start | Trigger implementation |
| 2 | **YAML file layout:** Single file or split? | `system.yaml` only / split by domain | PersistenceManager |
| 3 | **IPC security:** Trust Unix socket peer or require capability/auth? | Peer trust / cap-based / token | IPC server |
| 4 | **RTSP camera addressing:** Is ingest always at `{target_ip}:{input_port}` on the VPN subnet? | Yes / separate `camera.host` field | RtspCompressorComponent |
| 5 | **CAF actors:** Keep CAF from v1 or replace with orchestrator + graph only? | Keep / Remove | app_lifecycle complexity |
| 6 | **v1 parallel maintenance:** How long to maintain v1 during v2 rollout? | Until v2 field-validated / hard cutover | Release planning |
| 7 | **DSPatcher GUI:** Plan export/import compatibility for future GUI authoring? | Yes later / No | YAML schema stability |

---

## 21. References

### Internal (v1 source)

| Ref | Path | Description |
|-----|------|-------------|
| R1 | `odc-onboard/src/core/vpn_monitor.hpp` | VPN interface event gating via `find_connection_id_by_iface` |
| R2 | `odc-onboard/src/core/app_lifecycle.hpp` | Application startup, subsystem initialization |
| R3 | `odc-onboard/src/mavlink/mav_extender_manager.hpp` | v1 MAVLink auto-spawn on AddressAdded |
| R4 | `odc-onboard/src/rtsp/rtsp_compressor_manager.hpp` | v1 RTSP hardcoded ports, GLib thread model |
| R5 | `odc-onboard/tests/helpers/test_config.json` | v1 config structure |
| R6 | `docs-architecture/README.md` | v1 architecture overview |
| R7 | `docs-architecture/RTSP_USER_E2E_TEST_SUITE.md` | RTSP test suite specification |

### External

| Ref | URL | Description |
|-----|-----|-------------|
| E1 | https://github.com/cross-platform/dspatch | DSPatch header-only dataflow framework |
| E2 | https://github.com/cross-platform/dspatchables | DSPatch component repository |
| E3 | https://github.com/cross-platform/dspatcher | DSPatch graphical circuit editor |
| E4 | https://flowbasedprogramming.com | Flow-based programming concepts |

---

*End of document.*
