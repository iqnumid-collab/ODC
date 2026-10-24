# Production Deployment Guide

## Overview

HexaRtspCompressor installs as a single systemd service (`hexa-bridge`) that connects to your existing RTSP source (IP camera, NVR, etc.), transcodes the stream, and re-serves it with optional authentication and adaptive bitrate.

The installer places the binary and config template on disk. **Nothing runs until you configure it and start the service yourself.**

```
/opt/hexa/
├── bin/HexaRtspCompressor      # compiled binary
├── config/
│   └── bridge.json             # ← you edit this (RTSP source, output settings)
└── README                      # quick-start instructions
```

## 1. Prerequisites

```bash
sudo apt update && sudo apt install -y \
    build-essential cmake pkg-config \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-good1.0-dev libgstreamer-plugins-bad1.0-dev \
    gstreamer1.0-plugins-ugly gstreamer1.0-rtsp \
    libgstrtspserver-1.0-dev ffmpeg
```

For Jetson, also install `nvidia-l4t-gstreamer`.

## 2. Build

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ..
```

## 3. Install

```bash
sudo ./install.sh
```

This creates:
- System user `hexa` (no login, video/render groups for GPU)
- `/opt/hexa/bin/HexaRtspCompressor`
- `/opt/hexa/config/bridge.json` — template, must be edited
- Systemd service `hexa-bridge` — installed **disabled**, not started

## 4. Configure

### Set your RTSP source

```bash
sudo -u hexa nano /opt/hexa/config/bridge.json
```

Required: set `input_uri` to your camera's RTSP URL.

```json
{
    "input_uri": "rtsp://192.168.1.100:554/stream",
    "input_use_tcp": true,
    "output_port": 8552,
    "output_path": "/stream",
    "output_codec": "h265",
    "output_bitrate": 800,
    "output_width": 1280,
    "output_height": 720,
    "output_framerate": 30,
    "use_hw_codec": true,
    "output_username": "viewer",
    "output_password": "changeme"
}
```

You can first test that the source is reachable:

```bash
/opt/hexa/bin/HexaRtspCompressor analyse rtsp://192.168.1.100:554/stream --tcp --duration 10
```

This prints the source codec, resolution, framerate, and bandwidth without starting any service.

## 5. Start

```bash
sudo systemctl start hexa-bridge
```

Check it's running:

```bash
sudo systemctl status hexa-bridge
journalctl -u hexa-bridge -f
```

Clients connect to `rtsp://viewer:changeme@<host>:8552/stream`.

## 6. Enable on boot

Only after confirming it works:

```bash
sudo systemctl enable hexa-bridge
```

## Service Management

```bash
sudo systemctl start hexa-bridge       # start
sudo systemctl stop hexa-bridge        # stop
sudo systemctl restart hexa-bridge     # restart (after config edit)
sudo systemctl status hexa-bridge      # status
journalctl -u hexa-bridge -f           # live logs
journalctl -u hexa-bridge -n 100       # last 100 lines
```

## Configuration Reference

### bridge.json

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `input_uri` | string | `""` | **Required.** Source RTSP URI |
| `input_use_tcp` | bool | true | Force TCP for input |
| `output_address` | string | `0.0.0.0` | Bind address |
| `output_port` | int | 8552 | RTSP server port |
| `output_path` | string | `/stream` | Mount path |
| `output_codec` | string | `h265` | `h264`, `h265`, or `copy` |
| `output_bitrate` | int | 800 | Target kbps |
| `output_width` | int | 1280 | Output width |
| `output_height` | int | 720 | Output height |
| `output_framerate` | double | 30 | Output FPS |
| `output_username` | string | `""` | RTSP auth user (empty = no auth) |
| `output_password` | string | `""` | RTSP auth password |
| `use_hw_codec` | bool | true | Prefer HW encoder |
| `abr_enabled` | bool | false | Enable adaptive bitrate |
| `abr_min_bitrate` | int | 200 | ABR floor (kbps) |
| `abr_max_bitrate` | int | 0 | ABR ceiling (0 = output_bitrate) |

## Authentication

When `output_username` and `output_password` are set, clients must authenticate:

```bash
# Works
ffplay rtsp://viewer:changeme@host:8552/stream

# Rejected (401 Unauthorized)
ffplay rtsp://host:8552/stream
```

## Adaptive Bitrate

ABR adjusts output bitrate based on client packet loss (RTCP feedback). Only works with UDP clients.

```json
{
    "abr_enabled": true,
    "abr_min_bitrate": 200,
    "abr_max_bitrate": 2000
}
```

- Loss > 5% → bitrate steps down 20%
- Loss < 1% for 10s → bitrate steps up 20%

## Firewall

```bash
sudo ufw allow 8552/tcp    # RTSP control
sudo ufw allow 8552/udp    # RTP/RTCP data (needed for ABR)
```

## Updating

```bash
cd /path/to/source && git pull
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
sudo install -m 755 HexaRtspCompressor /opt/hexa/bin/
sudo systemctl restart hexa-bridge
```

Configs are preserved — only the binary is replaced.

## Uninstall

```bash
sudo ./install.sh --uninstall
sudo userdel -r hexa    # optional: remove user
```

## Troubleshooting

**Service won't start**
```bash
journalctl -u hexa-bridge -n 50 --no-pager
```
- `input_uri is required` → edit bridge.json, set your RTSP source
- `Failed to start RTSP bridge` → source unreachable, test with `analyse` subcommand first

**No video from output**
- Check auth: `ffprobe rtsp://user:pass@host:8552/stream`
- Check bridge logs for `clients=` count

**ABR not reacting**
- Only works with UDP clients (TCP masks packet loss)
- Check `[RTCP] RR` lines in logs
- Software x265 doesn't support runtime bitrate changes — use VAAPI/NVENC
