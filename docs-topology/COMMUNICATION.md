# Host communication

![Communication](diagrams/rendered/communication.svg)

## Planes

| Plane | Path | Purpose |
|-------|------|---------|
| **Lab L2/L3** | Ethernet `192.168.10.0/24` | SSH, deploy, WSS, management |
| **Control (IPC)** | Unix sockets on each host | Local CLI ↔ daemons |
| **Control (WSS)** | `wss://hexa-five:9443/ws` mTLS | Registry pub/sub, service register |
| **VPN data** | WG / OpenVPN tunnels | MAVLink UDP, RTSP/RTP over tunnel IPs |
| **Orchestration** | SSH from `molt` → all hosts | Build artifacts, configs, test scripts |

## Who talks to whom

```
molt (192.168.10.10)
  │ SSH deploy / test orchestration
  ├──────────────► hexa-five (192.168.10.16)   odc-server
  ├──────────────► odc-device-rpi-4b (.11)     onboard
  └──────────────► odc-device-rpi-5 (.12)      onboard

  │ odc-hook (required on odc-host)
  │ WSS mTLS :9443  — monitored locally via hook.sock / odc-cli
  └──────────────► hexa-five registry-manager

rpi-* (onboard VPN clients)
  │ WireGuard / OpenVPN
  └──────────────► hexa-five tunnel-manager
```

## Ports and sockets

### hexa-five (odc-server)

| Endpoint | Binding | Consumers |
|----------|---------|-----------|
| WSS mTLS | `0.0.0.0:9443` | `odc-hook` on `molt`, optional `odc-cli events watch` |
| WG listen | UDP (config) | Pi VPN peers |
| OpenVPN listen | UDP/TCP (config) | Pi VPN peers |
| `tunnel-ipc.sock` | local | `odc-cli tunnel *` |
| `odc-ipc.sock` | local | tunnel-manager notifications, CLI services |

### Target boards

| Endpoint | Binding | Consumers |
|----------|---------|-----------|
| onboard IPC | `/var/run/odc-onboard.sock` | graph CLI, tests |
| RTSP in/out | configurable (e.g. 8554→8555) | video-source / monitor |
| MAVLink UART | `/dev/ttyACM0` | flight controller (optional) |

### molt

| Endpoint | Purpose |
|----------|---------|
| SSH outbound | deploy + remote checks |
| Docker bridge | cross-compile / container integration tests |
| `odc-hook` → WSS | mTLS client to `192.168.10.16:9443` (system event probe) |
| `hook.sock` | local IPC for `odc-cli hook status` / monitoring |
| Optional SSH local-forward | remote CLI to server IPC if needed |

## Trust model (lab)

- Lab passwords in `host-configs/*.env` for automation only; prefer SSH keys for day-to-day.
- Registry WSS uses mTLS client certs issued by `odc-cli cert …` on the server.
- VPN peer credentials live under `/etc/odc-onboard/vpn/` on devices and tunnel-manager config on server.
