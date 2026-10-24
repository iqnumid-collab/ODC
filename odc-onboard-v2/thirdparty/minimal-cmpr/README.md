# HexaRtspCompressor

RTSP stream analyser and re-encoding bridge with adaptive bitrate control and authenticated output.

## Architecture

```
┌──────────────┐     RTSP      ┌──────────────────────────┐     RTSP (auth)     ┌──────────┐
│  IP Camera   │──────────────▶│  HexaRtspCompressor      │────────────────────▶│  Clients │
│  or MediaMTX │   H.264 4K   │  (bridge mode)            │   H.265 720p       │  VLC/    │
└──────────────┘               │                           │   800 kbps         │  ffplay  │
                               │  ┌─ VAAPI/NVENC/Jetson ─┐ │                    └──────────┘
                               │  │  HW transcode        │ │
                               │  └──────────────────────┘ │
                               └──────────────────────────┘
```

## Features

- **Analyse mode** — connect to any RTSP stream, report codec, resolution, framerate, bandwidth
- **Bridge mode** — ingest RTSP, transcode (HW-accelerated), re-serve as authenticated RTSP
- **Adaptive Bitrate (ABR)** — automatic bitrate adjustment based on RTCP client feedback
- **Hardware detection** — auto-selects Jetson (nvv4l2), NVENC, VAAPI, or software encoding
- **Authentication** — Basic auth on output RTSP
- **Thread orchestrator** — all async work managed by a custom thread pool library

## Quick Start

### Build

```bash
# Dependencies (Ubuntu 22.04 / Jetson)
sudo apt install -y build-essential cmake pkg-config \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-good1.0-dev libgstreamer-plugins-bad1.0-dev \
    gstreamer1.0-plugins-ugly gstreamer1.0-rtsp \
    libgstrtspserver-1.0-dev ffmpeg

# Build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Analyse a stream

```bash
./build/HexaRtspCompressor analyse rtsp://camera:554/stream --tcp --duration 30
```

Output:
```
  Codec        : video/x-raw
  Resolution   : 1920x1080
  Framerate    : 60.00 fps
  Avg Bandwidth: 2257.9 kbps
  Total Frames : 969
```

### Run the bridge

```bash
./build/HexaRtspCompressor bridge config/bridge.json
```

Clients connect to `rtsp://user:pass@host:8552/stream`.

## CLI Reference

```
HexaRtspCompressor — RTSP stream analyser & bridge

Subcommands:
  analyse    Analyse an RTSP stream (bandwidth, codec, resolution)
  bridge     Start an RTSP re-encoding bridge

analyse <uri> [--tcp] [--view] [--duration N]
  uri          RTSP URI to analyse
  --tcp        Force RTP-over-TCP transport
  --view       Open a video preview window
  --duration   Run for N seconds then exit

bridge <config>
  config       Path to bridge JSON config
```

## Configuration

### Bridge Config (`config/bridge.json`)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `input_uri` | string | required | Source RTSP URI |
| `input_use_tcp` | bool | false | Force TCP for input |
| `output_address` | string | `0.0.0.0` | Bind address |
| `output_port` | int | 8554 | RTSP server port |
| `output_path` | string | `/stream` | Mount path |
| `output_codec` | string | `""` | `h264`, `h265`, or `copy` |
| `output_bitrate` | int | 0 | Target kbps (0 = original) |
| `output_width` | int | 0 | Output width (0 = original) |
| `output_height` | int | 0 | Output height (0 = original) |
| `output_framerate` | double | 0 | Output FPS (0 = original) |
| `output_username` | string | `""` | RTSP auth user (empty = no auth) |
| `output_password` | string | `""` | RTSP auth password |
| `use_hw_codec` | bool | false | Prefer HW encoder |
| `speed_preset` | string | `ultrafast` | x264/x265 preset |
| `abr_enabled` | bool | false | Enable adaptive bitrate |
| `abr_min_bitrate` | int | 200 | ABR floor (kbps) |
| `abr_max_bitrate` | int | 0 | ABR ceiling (0 = output_bitrate) |
| `abr_loss_threshold_down` | double | 5.0 | % loss to step down |
| `abr_loss_threshold_up` | double | 1.0 | % loss to step up |
| `abr_stable_seconds` | int | 10 | Stable time before step-up |
| `abr_interval_seconds` | int | 3 | ABR evaluation interval |

## Hardware Encoding

The binary auto-detects available encoders at startup:

| Platform | Encoder | Strategy |
|----------|---------|----------|
| NVIDIA Jetson | `nvv4l2h264enc` / `nvv4l2h265enc` | JetsonHW |
| Desktop NVIDIA | `nvh264enc` / `nvh265enc` | DesktopNVENC |
| Intel/AMD | `vaapih264enc` / `vaapih265enc` | VAAPI |
| Any | `x264enc` / `x265enc` | Software |

Set `"use_hw_codec": true` in bridge config to prefer hardware encoding.

## Deployment

See [docs/production.md](docs/production.md) for the full production guide.

### Quick install

```bash
# Build
mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
cd ..

# Install binary + config template + systemd service (nothing starts yet)
sudo ./install.sh

# Configure your RTSP source
sudo -u hexa nano /opt/hexa/config/bridge.json
# → set "input_uri" to your camera

# Start
sudo systemctl start hexa-bridge

# Enable on boot (after confirming it works)
sudo systemctl enable hexa-bridge
```

### Development (PM2)

```bash
# Isolated PM2 instance (won't interfere with other PM2 services)
./dev/hexa-pm2 start dev/ecosystem.config.js
./dev/hexa-pm2 logs hexa-bridge --lines 50
./dev/hexa-pm2 monit
```

## Project Structure

```
├── src/                    C++ source files
│   ├── main.cpp            CLI entry point (CLI11 subcommands)
│   ├── RtspAnalyzer.cpp    Stream analysis (GStreamer pipeline)
│   ├── RtspBridge.cpp      Bridge: ingest → transcode → RTSP server
│   └── HardwareInfo.cpp    GPU/encoder detection
├── include/                Headers
├── config/                 Production config template
│   └── bridge.json.template
├── systemd/                hexa-bridge.service
├── docs/                   Production deployment guide
├── dev/                    Development-only resources
│   ├── config/             Dev configs (bridge_local_test.json, etc.)
│   ├── src-pipeline/       MediaMTX test source + video
│   ├── ecosystem.config.js PM2 config
│   └── hexa-pm2            Isolated PM2 wrapper
├── thirdparty/             CLI11, thread-orchestrator
├── install.sh              Production installer
└── CMakeLists.txt          Build system
```

## License

Proprietary.
