# Event pipelines

![Event Pipelines](diagrams/rendered/event-pipelines.svg)

Three related pipelines run in the lab: **server event bus**, **onboard graph events**, and **orchestrator + hook monitoring on `molt`**.

## 1. odc-root server pipeline (`hexa-five`)

```
tunnel-manager  --JSON-RPC notify-->  registry-manager  --WSS publish-->  odc-hook on molt
                                              │
                                              └── webhook HTTP POST (optional)
```

### Tunnel → registry (IPC notifications)

| Event | Meaning |
|-------|---------|
| `tunnel.server.started` / `stopped` | VPN server lifecycle |
| `tunnel.client.connected` / `disconnected` | Peer join/leave |
| `tunnel.cert.issued` | Credential issued |
| `tunnel.error` | Server-side failure |

### Registry → odc-hook on odc-host (WSS)

Primary subscriber is **`odc-hook` on `molt`** (required). Optional ad-hoc: `odc-cli events watch` from the same host.

1. Hook mTLS-connects to `wss://192.168.10.16:9443/ws`
2. Subscribes to tunnel / service topics
3. Receives published events
4. Enqueues into `EventBus` → worker `ThreadPool`
5. Dev host monitors via `odc-cli hook status` and journal — correctness gate for the event path

## 2. Onboard graph pipeline (`rpi-4b` / `rpi-5`)

```
VPN component state change
    → graph triggers (YAML)
        → bind MAV extender / RTSP compressor to tunnel iface + IP
            → data plane: MAVLink UDP + GStreamer RTSP (not via DSPatch buses)
```

Lifecycle events of interest for tests:

| Signal | Assert |
|--------|--------|
| `VpnState::Connected` | Tunnel up; triggers fire |
| Extender started | MAVLink over tunnel IP |
| Compressor started | RTSP out port serving |
| Daemon restart | Graph restored from `system.yaml` |

See also [`odc-onboard-v2/docs/REAL_HARDWARE_TEST_SUITE.md`](../odc-onboard-v2/docs/REAL_HARDWARE_TEST_SUITE.md).

## 3. Orchestrator + hook monitor pipeline (`molt`)

```
build → package → scp → remote install
  → odc-hook health (local) + remote health checks
  → E2E scripts → assert hook event counters → collect logs
```

Test orchestration events (bash / CI):

| Stage | Signal |
|-------|--------|
| Preflight | SSH reachable; **`odc-hook` connected**; remote services active |
| Deploy | checksum of binaries match artifact |
| Runtime | VPN peer visible on server + tunnel on device |
| Events | hook `events_received` / `events_processed` rise on peer churn |
| Media | RTSP monitor frames > 0 |
| Teardown | stop remote units; leave hook running for soak if desired |

## End-to-end expected chain (happy path)

1. `molt` deploys configs + binaries to `hexa-five` and a Pi; ensures `odc-hook` is up locally.
2. Tunnel-manager starts WG/OVPN servers; registry listens on `:9443`.
3. `odc-hook` on `molt` establishes mTLS; `odc-cli hook status` shows `connected=true`.
4. Pi `odc-onboard-v2` connects VPN → peer appears → `tunnel.client.connected` published.
5. Hook on `molt` receives the event; counters increase (dev-host correctness check).
6. Graph triggers spawn extender + RTSP compressor on tunnel.
7. Lab video-source → compressor → rtsp-monitor validates media path.
