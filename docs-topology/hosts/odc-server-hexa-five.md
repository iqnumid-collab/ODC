# odc-server — hexa-five

| Field | Value |
|-------|-------|
| Role | Ground / VPN + registry server (`odc-root`) |
| Hostname | `hexa-five` |
| IP | `192.168.10.16` |
| SSH user | `hexa-five` |
| Password | `hexa` |
| Arch | `x86_64` (amd64) |
| Config | [`host-configs/hexa-five.yaml`](../../host-configs/hexa-five.yaml) |

## Purpose

Lab **odc-server**: runs WireGuard/OpenVPN servers, mTLS WebSocket event registry, and exposes IPC for CLI-driven peer and service management. Target boards connect here for VPN; **`odc-hook` on `molt`** connects here for the event stream (do not install hook on this host).

## Services to install

| Package / binary | Why |
|------------------|-----|
| `odc-tunnel-manager` | WG + OpenVPN server, peer lifecycle, session tracking |
| `odc-registry-manager` | mTLS WSS (`:9443`), service registry, webhooks, event pub/sub |
| `odc-cli` | Certs, tunnel ops, events watch, services CRUD |
| WireGuard kernel module + tools | Server-side WG |
| OpenVPN (system) | Server subprocess for OVPN |
| SQLite | `/var/lib/odc-root/odc-root.db` |
| systemd units | `odc-tunnel-manager.service`, `odc-registry-manager.service` |

## Deploy path

```
/opt/odc/bin/odc-{tunnel-manager,registry-manager,cli}
/etc/odc-root/{tunnel-manager,registry-manager}.yaml
/var/lib/odc-root/odc-root.db
/var/run/odc/{tunnel-ipc,odc-ipc}.sock
```

## Per-host checks

See [PER_HOST_CHECKS.md](../PER_HOST_CHECKS.md#odc-server--hexa-five).
