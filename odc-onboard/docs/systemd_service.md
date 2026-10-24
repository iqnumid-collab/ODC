# systemd Service Guide

## Overview

odc-onboard runs as a systemd service with hardened security settings. The service file is installed to `/lib/systemd/system/odc-onboard.service`.

## Service File

```ini
[Unit]
Description=ODC Onboard VPN Management Service
Documentation=man:odc-onboard(8)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/odc-onboard --config /etc/odc-onboard/config.json
Restart=on-failure
RestartSec=5
WatchdogSec=30

Capabilities=CAP_NET_ADMIN+ep
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/etc/odc-onboard /var/log/odc-onboard
PrivateTmp=true

StandardOutput=journal
StandardError=journal
SyslogIdentifier=odc-onboard

[Install]
WantedBy=multi-user.target
```

## Security Hardening

| Directive | Effect |
|-----------|--------|
| `Capabilities=CAP_NET_ADMIN+ep` | Grants only network administration capability. No full root access. |
| `NoNewPrivileges=true` | Prevents the process from gaining additional privileges via setuid/setgid or file capabilities. |
| `ProtectSystem=strict` | Mounts the entire filesystem read-only except for paths explicitly allowed. |
| `ProtectHome=true` | Makes `/home`, `/root`, and `/run/user` inaccessible to the service. |
| `ReadWritePaths=...` | Only these directories are writable. All others are read-only. |
| `PrivateTmp=true` | Provides an isolated `/tmp` namespace for the service. |
| `WatchdogSec=30` | systemd watchdog: if the service does not respond within 30s, it is restarted. |

## Installation

After building and installing the binary:

```bash
sudo cp odc-onboard.service /lib/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable odc-onboard
sudo systemctl start odc-onboard
```

## Service Management

### Start / Stop / Restart

```bash
sudo systemctl start odc-onboard
sudo systemctl stop odc-onboard
sudo systemctl restart odc-onboard
```

### Check Status

```bash
sudo systemctl status odc-onboard
```

### View Logs

```bash
sudo journalctl -u odc-onboard -f
sudo journalctl -u odc-onboard --since "1 hour ago"
```

### Auto-Restart Behavior

The service is configured with `Restart=on-failure` and `RestartSec=5`. This means:
- If the process crashes or exits with a non-zero code, systemd restarts it after 5 seconds.
- Normal stops via `systemctl stop` do not trigger a restart.
- The watchdog (`WatchdogSec=30`) will restart the service if it becomes unresponsive for more than 30 seconds.

## Running with CLI Console

To run the service interactively (for debugging or manual management), stop the service and run manually:

```bash
sudo systemctl stop odc-onboard
sudo odc-onboard --config /etc/odc-onboard/config.json --cli
```

## Configuration Directory

The service reads its configuration from `/etc/odc-onboard/config.json`. The directory structure:

```
/etc/odc-onboard/
├── config.json          # Main application configuration
└── vpn/                 # VPN configuration files
    ├── wg1.conf         # WireGuard config
    ├── wg1.conf.creds   # Encrypted credentials (OpenVPN)
    └── wg1.conf.key     # Decryption key
```

Permissions should be:
- `/etc/odc-onboard/` -- mode `700`, owned by `root:root`
- Config files -- mode `600`, owned by `root:root`

Use `grant-requirements` in the CLI or run manually:

```bash
sudo mkdir -p /etc/odc-onboard
sudo chmod 700 /etc/odc-onboard
sudo chown root:root /etc/odc-onboard
```
