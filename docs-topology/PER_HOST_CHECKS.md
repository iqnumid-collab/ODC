# Per-host checks

Run from `molt` unless noted. Source host env first:

```bash
set -a; source host-configs/molt.env; set +a
# or per-target: source host-configs/rpi-4b.env
```

![Per-Host Checks](diagrams/rendered/per-host-checks.svg)

---

## odc-host — molt

| Check | Command / criterion |
|-------|---------------------|
| Network | `ping -c1 192.168.10.11` … `.12` … `.16` |
| Docker | `docker info` succeeds |
| Repo | `test -d odc-root && test -d odc-onboard-v2` |
| Deploy Makefile | `make -f odc-root/deploy/Makefile help` (or `linux` dry-run) |
| SSH keys/aliases | `ssh -o BatchMode=yes hexa-five true` (prefer keys) |
| `odc-hook` unit | `systemctl is-active odc-hook` |
| Hook IPC | `odc-cli hook status` → `connected=true` |
| Hook events | after Pi VPN churn: `events_received` and `events_processed` increase |
| Hook pool | `thread_pool_active` returns to 0 when idle |
| Hook journal | `journalctl -u odc-hook -n 50` has no mTLS / reconnect storms |

Hook monitoring is owned by the **dev host**: treat a failed hook status as a lab-red failure even if tunnel-manager and Pis look healthy.
---

## odc-server — hexa-five

```bash
ssh hexa-five@192.168.10.16
```

| Check | Command / criterion |
|-------|---------------------|
| Units up | `systemctl is-active odc-tunnel-manager odc-registry-manager` |
| IPC ping | `odc-cli` → registry `ping` / `status` |
| Tunnel health | `odc-cli tunnel health` |
| WSS listen | `ss -lntp \| grep 9443` |
| DB present | `test -f /var/lib/odc-root/odc-root.db` |
| Peer appears | after Pi VPN connect: `odc-cli tunnel peers list` shows peer |

From `molt` without login shell:

```bash
ssh hexa-five@192.168.10.16 'systemctl is-active odc-tunnel-manager odc-registry-manager'
ssh hexa-five@192.168.10.16 'ss -lntp | grep 9443'
```

---

## rpi-4b — odc-device-rpi-4b

```bash
ssh odc-device-rpi-4b@192.168.10.11
```

| Check | Command / criterion |
|-------|---------------------|
| Unit up | `systemctl is-active odc-onboard-v2` |
| IPC | graph CLI `ping` / `graph components list` |
| Config | `test -f /etc/odc-onboard/system.yaml` |
| VPN creds | `ls /etc/odc-onboard/vpn/` non-empty |
| CAP_NET_ADMIN | daemon can create TUN |
| Tunnel iface | after connect: `ip -br link` shows `wg*` / `tun*` |
| Optional UART | `test -c /dev/ttyACM0` (skip MAV if missing) |
| RTSP lab | video-source + monitor ports open |

---

## rpi-5 — odc-device-rpi-5

Same checklist as rpi-4b against `192.168.10.12` / user `odc-device-rpi-5`.

Additionally:

| Check | Criterion |
|-------|-----------|
| Board identity | `cat /proc/device-tree/model` contains Raspberry Pi 5 |
| Arch artifact | binary is aarch64 (`file /opt/odc/bin/odc-onboard-v2`) |

---

## Cross-host smoke (one script idea)

```bash
#!/usr/bin/env bash
set -euo pipefail
for h in 192.168.10.11 192.168.10.12 192.168.10.16; do
  ping -c1 -W2 "$h" >/dev/null && echo "OK ping $h" || echo "FAIL ping $h"
done
systemctl is-active odc-hook
odc-cli hook status
ssh hexa-five@192.168.10.16 'systemctl is-active odc-tunnel-manager odc-registry-manager'
ssh odc-device-rpi-4b@192.168.10.11 'systemctl is-active odc-onboard-v2'
ssh odc-device-rpi-5@192.168.10.12 'systemctl is-active odc-onboard-v2'
```
