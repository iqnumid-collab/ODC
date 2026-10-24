# odc-rtsp-monitor — User Guide

A C++ binary that connects to an RTSP stream, consumes the video, and measures real-time bandwidth. Designed as a companion tool to odc-video-source for verifying bandwidth throttling.

## Overview

odc-rtsp-monitor connects to any RTSP stream, decodes the video, and reports bandwidth statistics in real-time. It is used to verify that the odc-video-source server is correctly enforcing bandwidth limits.

## Quick Start

```bash
# Build
cd tools/odc-rtsp-monitor && mkdir build && cd build && cmake .. && make -j$(nproc)

# Connect to a local RTSP stream
./odc-rtsp-monitor rtsp://localhost:8554/stream

# Connect with TCP transport for 10 seconds
./odc-rtsp-monitor rtsp://localhost:8554/stream --tcp --duration 10
```

## CLI Reference

```
Usage: ./odc-rtsp-monitor <rtsp_uri> [--tcp] [--duration <seconds>]
```

| Flag | Description | Default |
|------|-------------|---------|
| `<rtsp_uri>` | RTSP URI to connect to (required) | — |
| `--tcp` | Force RTP-over-TCP transport | UDP |
| `--duration <sec>` | Run for N seconds then exit | `30` |
| `--help`, `-h` | Print usage information | — |

## Log Output

### Startup

```
[2026-06-01 16:46:41.144] [info] odc-rtsp-monitor v1.0.0 starting
[2026-06-01 16:46:41.144] [info]   Connecting to: rtsp://localhost:8554/stream
[2026-06-01 16:46:41.144] [info]   Transport    : TCP
[2026-06-01 16:46:41.144] [info]   Duration     : 25 seconds
[2026-06-01 16:46:41.144] [info] --------------------------------------------------
```

### Video Stream Info

```
[2026-06-01 16:46:41.365] [info] ===== Video Stream Info =====
[2026-06-01 16:46:41.365] [info]   Codec      : video/x-raw
[2026-06-01 16:46:41.365] [info]   Resolution : 1920x1080
[2026-06-01 16:46:41.365] [info]   Framerate  : 60.0 fps
[2026-06-01 16:46:41.365] [info]   Format     : NV12
[2026-06-01 16:46:41.365] [info]   Transport  : RTP/TCP
[2026-06-01 16:46:41.365] [info] =============================
```

### Real-time Bandwidth (every 500ms)

```
[2026-06-01 16:46:41.670] [info] [CLIENT BW] current: 476.4 kbps  avg: 476.4 kbps  peak: 476.4 kbps  total: 0.0 MB  frames: 45  elapsed: 0.5 s
```

| Field | Description |
|-------|-------------|
| `current` | Instantaneous bandwidth over the last 500ms window |
| `avg` | Average bandwidth since connection start |
| `peak` | Highest instantaneous bandwidth observed |
| `total` | Cumulative data received |
| `frames` | Total frames (RTP packets) received |
| `elapsed` | Time since connection started |

### Shutdown Report

```
[2026-06-01 16:47:06.271] [info] ========== Client Final Report ==========
[2026-06-01 16:47:06.271] [info]   Stream URI   : rtsp://localhost:8554/stream
[2026-06-01 16:47:06.271] [info]   Codec        : video/x-raw
[2026-06-01 16:47:06.271] [info]   Resolution   : 1920x1080
[2026-06-01 16:47:06.271] [info]   Framerate    : 60.00 fps
[2026-06-01 16:47:06.271] [info]   Pixel Format : NV12
[2026-06-01 16:47:06.271] [info]   Transport    : RTP/TCP
[2026-06-01 16:47:06.271] [info]   ----------------------------------------
[2026-06-01 16:47:06.271] [info]   Avg Bandwidth: 2693.8 kbps (2.69 Mbps)
[2026-06-01 16:47:06.271] [info]   Peak Bandwidth: 6537.8 kbps (6.54 Mbps)
[2026-06-01 16:47:06.271] [info]   Total Data   : 8.0 MB
[2026-06-01 16:47:06.271] [info]   Total Frames : 6476
[2026-06-01 16:47:06.271] [info]   Duration     : 24.9 s
[2026-06-01 16:47:06.271] [info] ===========================================
```

## Usage Scenarios

### Scenario 1: Connect and Measure (Default 30s)

```bash
./odc-rtsp-monitor rtsp://localhost:8554/stream
```

Connects via UDP, runs for 30 seconds, then prints the final report.

### Scenario 2: Force TCP Transport

```bash
./odc-rtsp-monitor rtsp://localhost:8554/stream --tcp
```

Forces RTP-over-TCP interleaved transport. Useful for testing behind firewalls or NAT.

### Scenario 3: Short Test Run

```bash
./odc-rtsp-monitor rtsp://localhost:8554/stream --tcp --duration 10
```

Runs for 10 seconds then exits.

### Scenario 4: Verify Throttle Enforcement

Start the server with a throttle cap, then run the monitor to verify:

```bash
# Terminal 1 — Server with 1500 kbps cap
./odc-video-source --video mercedes.mp4 --max-bandwidth 1500 --throttle-mode drop

# Terminal 2 — Monitor to verify
./odc-rtsp-monitor rtsp://localhost:8554/stream --tcp --duration 15
```

Check the monitor's `Avg Bandwidth` in the final report. It should be at or below 1500 kbps.

### Scenario 5: Verify Random Throttle

```bash
# Terminal 1 — Server with random throttle (20-100% of 2000 kbps)
./odc-video-source --video mercedes.mp4 --max-bandwidth 2000 --rand-throttle --rand-min 20 --rand-max 100

# Terminal 2 — Monitor to observe fluctuation
./odc-rtsp-monitor rtsp://localhost:8554/stream --tcp --duration 30
```

Watch the monitor's `[CLIENT BW]` lines. The `current` bandwidth should fluctuate as the server's random throttle updates every 5 seconds.

## Architecture

### Pipeline

```
rtspsrc → identity(probe) → decodebin → fakesink
```

- **rtspsrc**: Handles RTSP negotiation, SDP parsing, RTP/RTCP reception
- **identity**: Pass-through element with a buffer probe to measure raw RTP byte throughput
- **decodebin**: Auto-negotiates RTP depayloading and video decoding
- **fakesink**: Discards decoded frames (no rendering needed for measurement)

### Bandwidth Measurement

A `GstPadProbe` is installed on the `identity` element's sink pad. Every buffer that passes through triggers the probe callback, which:

1. Reads the buffer size in bytes
2. Accumulates total bytes and frame count
3. Computes instantaneous bandwidth every 500ms: `current_kbps = (delta_bytes * 8 / 1000) / dt`
4. Updates peak and average statistics
5. Fires a callback that logs the stats via spdlog

## Environment Variables

| Variable | Description |
|----------|-------------|
| `SPDLOG_LEVEL` | Log level: `trace`, `debug`, `info`, `warn`, `error`, `critical`, `off` |

```bash
SPDLOG_LEVEL=debug ./odc-rtsp-monitor rtsp://localhost:8554/stream
```

## Dependencies

| Library | Purpose |
|---------|---------|
| GStreamer 1.0 | Media pipeline framework |
| gstreamer-plugins-base | video, app, pbutils, sdp modules |
| gstreamer-plugins-good | rtspsrc element |
| spdlog | Structured logging |

### Install on Ubuntu/Debian

```bash
sudo apt install -y cmake build-essential pkg-config \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly gstreamer1.0-libav \
    libspdlog-dev
```
