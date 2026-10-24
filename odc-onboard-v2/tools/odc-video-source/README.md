# odc-video-source — User Guide

A C++ binary that consumes an MP4 video file and serves it as an RTSP stream with configurable bandwidth throttling. Built natively with GStreamer as a lightweight alternative to MediaMTX.

## Overview

odc-video-source reads an MP4 file and creates an RTSP server that clients can connect to. It supports two bandwidth throttling modes (delay and drop), passthrough or transcoding, infinite looping, and real-time bandwidth monitoring via spdlog.

## Quick Start

```bash
# Build
cd tools/odc-video-source && mkdir build && cd build && cmake .. && make -j$(nproc)

# Run — serve mercedes.mp4 on rtsp://0.0.0.0:8554/stream
./odc-video-source --video ../vid-src/mercedes.mp4

# Run with 2 Mbps throttle cap
./odc-video-source --video ../vid-src/mercedes.mp4 --max-bandwidth 2000

# Run with random bandwidth fluctuation (20-100% of max every 5 seconds)
./odc-video-source --video ../vid-src/mercedes.mp4 --max-bandwidth 2000 --rand-throttle
```

## CLI Reference

```
Usage: ./odc-video-source --video <file> [options]
       ./odc-video-source --config <config.json>
```

### General Options

| Flag | Description | Default |
|------|-------------|---------|
| `--video <file>` | MP4 video file path (required unless `--config`) | — |
| `--config <file>` | Load configuration from a JSON file | — |
| `--help`, `-h` | Print usage information | — |

### Server Options

| Flag | Description | Default |
|------|-------------|---------|
| `--address <addr>` | RTSP bind address | `0.0.0.0` |
| `--port <port>` | RTSP server port | `8554` |
| `--path <path>` | RTSP mount path | `/stream` |
| `--loop` | Loop video infinitely | `true` |
| `--no-loop` | Disable looping | — |

### Throttle Options

| Flag | Description | Default |
|------|-------------|---------|
| `--max-bandwidth <kbps>` | Maximum bandwidth cap in kbps (0 = disabled) | `0` |
| `--throttle-mode <mode>` | Throttle strategy: `delay` or `drop` | `delay` |
| `--rand-throttle` | Enable random bandwidth fluctuation | off |
| `--rand-interval <sec>` | How often to change random throttle (seconds) | `5` |
| `--rand-min <percent>` | Minimum percentage of max bandwidth | `20` |
| `--rand-max <percent>` | Maximum percentage of max bandwidth | `100` |

### Transcode Options

| Flag | Description | Default |
|------|-------------|---------|
| `--codec <codec>` | Output codec: `copy`, `h264`, `h265` | `copy` |
| `--bitrate <kbps>` | Target bitrate for transcoding (kbps) | `0` |
| `--width <px>` | Output width for transcoding | original |
| `--height <px>` | Output height for transcoding | original |
| `--framerate <fps>` | Output framerate for transcoding | original |
| `--preset <preset>` | Encoder speed preset | `ultrafast` |

## JSON Configuration

### Full Config Example

```json
{
    "video_file": "mercedes.mp4",
    "rtsp_address": "0.0.0.0",
    "rtsp_port": 8554,
    "mount_path": "/stream",
    "loop": true,
    "max_bandwidth_kbps": 2000,
    "throttle_mode": "delay",
    "rand_throttle": true,
    "rand_interval_sec": 5,
    "rand_min_percent": 20,
    "rand_max_percent": 100,
    "codec": "copy",
    "bitrate": 0,
    "width": 0,
    "height": 0,
    "framerate": 0,
    "preset": "ultrafast"
}
```

### Config Fields

| Field | Type | Description |
|-------|------|-------------|
| `video_file` | string | Path to the MP4 file (required) |
| `rtsp_address` | string | Bind address for the RTSP server |
| `rtsp_port` | int | Port for the RTSP server |
| `mount_path` | string | URL path for the stream (e.g. `/stream`) |
| `loop` | bool | Whether to loop the video infinitely |
| `max_bandwidth_kbps` | int | Maximum bandwidth in kbps (0 = no throttle) |
| `throttle_mode` | string | `"delay"` or `"drop"` |
| `rand_throttle` | bool | Enable random bandwidth fluctuation |
| `rand_interval_sec` | int | Seconds between random throttle updates |
| `rand_min_percent` | int | Minimum % of max bandwidth for random mode |
| `rand_max_percent` | int | Maximum % of max bandwidth for random mode |
| `codec` | string | `"copy"` for passthrough, `"h264"` or `"h265"` for transcode |
| `bitrate` | int | Target bitrate for transcoding in kbps |
| `width` | int | Output width (0 = keep original) |
| `height` | int | Output height (0 = keep original) |
| `framerate` | float | Output framerate (0 = keep original) |
| `preset` | string | Encoder preset (e.g. `ultrafast`, `superfast`, `fast`) |

## Throttle Modes

### Delay Mode (Token Bucket)

Buffers are delayed when the token budget is exhausted. This preserves all frames but adds latency proportional to how far over the budget the stream is.

**Best for:** Quality-critical scenarios where frame drops are unacceptable.

**Behavior:**
- Token bucket replenishes at the configured rate
- When a buffer arrives and tokens are insufficient, the thread sleeps until enough tokens accumulate
- No frames are dropped
- Cannot reduce bandwidth below the encoded bitrate of the source file

### Drop Mode (Frame Shedding)

Non-keyframe (delta/P/B-frame) buffers are dropped when the bandwidth exceeds the configured limit. Keyframes (I-frames) are never dropped.

**Best for:** Low-latency scenarios where some quality degradation is acceptable.

**Behavior:**
- Tracks bytes sent in the current measurement window
- When the window exceeds the budget, delta frames are dropped
- Keyframes always pass through to prevent stream corruption
- Can reduce bandwidth significantly below the encoded bitrate

### Random Throttle (Floating)

Every N seconds (configurable), the effective bandwidth limit is set to a random percentage of the configured maximum. This simulates fluctuating network conditions.

**Best for:** Testing client behavior under variable bandwidth, stress testing, or simulating real-world network variability.

**Behavior:**
- A background thread picks a random percentage between `rand_min_percent` and `rand_max_percent`
- The effective bandwidth is updated: `effectiveBW = maxBW * randomPercent / 100`
- Both delay and drop modes respect the fluctuating limit
- Each update is logged: `[RandThrottle] Updated bandwidth: 400kbps (20% of 2000kbps max)`

## Log Output

### Startup

```
[2026-06-01 16:46:39.140] [info] odc-video-source v1.0.0 starting
[2026-06-01 16:46:39.140] [info]   Video file : ../vid-src/mercedes.mp4
[2026-06-01 16:46:39.140] [info]   RTSP       : rtsp://0.0.0.0:8554/stream
[2026-06-01 16:46:39.140] [info]   Loop       : yes
[2026-06-01 16:46:39.140] [info]   Codec      : copy
[2026-06-01 16:46:39.140] [info]   Throttle   : enabled (max: 2000 kbps, mode: delay)
[2026-06-01 16:46:39.140] [info]   RandThrottle: 5s interval (20-100% of max)
[2026-06-01 16:46:39.140] [info] --------------------------------------------------
```

### Real-time Bandwidth (every 500ms)

```
[2026-06-01 16:46:41.706] [info] [BW] current: 682.8 kbps  avg: 1356.8 kbps  peak: 682.8 kbps  total: 0.2 MB  frames: 59  elapsed: 1.0 s
```

| Field | Description |
|-------|-------------|
| `current` | Instantaneous bandwidth over the last sample window |
| `avg` | Average bandwidth since stream start |
| `peak` | Highest instantaneous bandwidth observed |
| `total` | Cumulative data sent |
| `frames` | Total frames (RTP packets) sent |
| `elapsed` | Time since stream start |

### Random Throttle Updates

```
[2026-06-01 16:46:46.206] [info] [RandThrottle] Updated bandwidth: 1460kbps (73% of 2000kbps max)
[2026-06-01 16:47:01.206] [info] [RandThrottle] Updated bandwidth: 400kbps (20% of 2000kbps max)
```

### Shutdown Report

```
[2026-06-01 16:47:16.206] [info] ========== Final Bandwidth Report ==========
[2026-06-01 16:47:16.206] [info]   Video File   : ../vid-src/mercedes.mp4
[2026-06-01 16:47:16.206] [info]   Stream       : rtsp://0.0.0.0:8554/stream
[2026-06-01 16:47:16.206] [info]   Codec        : copy
[2026-06-01 16:47:16.206] [info]   ----------------------------------------
[2026-06-01 16:47:16.206] [info]   Avg Bandwidth: 2658.6 kbps (2.66 Mbps)
[2026-06-01 16:47:16.206] [info]   Peak Bandwidth: 4069.5 kbps (4.07 Mbps)
[2026-06-01 16:47:16.206] [info]   Total Data   : 7.9 MB
[2026-06-01 16:47:16.206] [info]   Total Frames : 947
[2026-06-01 16:47:16.206] [info]   Dropped      : 0
[2026-06-01 16:47:16.206] [info]   Duration     : 25.0 s
[2026-06-01 16:47:16.206] [info]   Throttle     : enabled (max: 2000 kbps, mode: delay)
[2026-06-01 16:47:16.206] [info]   RandThrottle : 5s interval (20-100% of max)
[2026-06-01 16:47:16.206] [info] ============================================
```

## Usage Scenarios

### Scenario 1: Simple Passthrough (No Throttle)

```bash
./odc-video-source --video mercedes.mp4
```

Stream: `rtsp://<IP>:8554/stream`

### Scenario 2: Fixed Bandwidth Cap (Delay Mode)

```bash
./odc-video-source --video mercedes.mp4 --max-bandwidth 1500 --throttle-mode delay
```

Caps output at 1500 kbps. Frames are delayed, not dropped.

### Scenario 3: Fixed Bandwidth Cap (Drop Mode)

```bash
./odc-video-source --video mercedes.mp4 --max-bandwidth 1500 --throttle-mode drop
```

Caps output at 1500 kbps. Non-keyframe packets are dropped when over budget.

### Scenario 4: Random Bandwidth Fluctuation

```bash
./odc-video-source --video mercedes.mp4 --max-bandwidth 2000 --rand-throttle --rand-min 20 --rand-max 100 --rand-interval 5
```

Every 5 seconds, bandwidth randomly fluctuates between 400 kbps (20%) and 2000 kbps (100%).

### Scenario 5: Transcode to Lower Resolution

```bash
./odc-video-source --video mercedes.mp4 --codec h264 --bitrate 1500 --width 1280 --height 720 --framerate 30
```

Decodes the source and re-encodes as H264 at 720p30 with 1500 kbps target bitrate.

### Scenario 6: Transcode with Throttle Safety Cap

```bash
./odc-video-source --video mercedes.mp4 --codec h265 --bitrate 2000 --max-bandwidth 2500 --throttle-mode drop
```

Encodes at 2000 kbps target, with a hard cap at 2500 kbps via drop mode as a safety net.

## Architecture

### Passthrough Pipeline

```
filesrc → qtdemux → h264parse → identity(throttle probe) → rtph264pay → GstRTSPServer
```

### Transcode Pipeline

```
filesrc → qtdemux → h264parse → avdec_h264 → videoconvert → [videoscale] → [videorate]
  → x264enc → identity(throttle probe) → rtph264pay → GstRTSPServer
```

### Throttle Probe

An `identity` GStreamer element with a `GstPadProbe` callback is inserted into the pipeline. The probe intercepts every buffer and applies the throttle logic:

- **Delay mode**: If tokens are insufficient, the probe thread sleeps until the token budget replenishes
- **Drop mode**: If the current window exceeds the budget, non-keyframe buffers return `GST_PAD_PROBE_DROP`

The probe is installed when a client connects via the `media-constructed` signal on the `GstRTSPMediaFactory`.

## Environment Variables

| Variable | Description |
|----------|-------------|
| `SPDLOG_LEVEL` | Log level: `trace`, `debug`, `info`, `warn`, `error`, `critical`, `off` |

```bash
SPDLOG_LEVEL=debug ./odc-video-source --video mercedes.mp4
```

## Dependencies

| Library | Purpose |
|---------|---------|
| GStreamer 1.0 | Media pipeline framework |
| gstreamer-rtsp-server-1.0 | RTSP server |
| spdlog | Structured logging |
| nlohmann/json | JSON config parsing |

### Install on Ubuntu/Debian

```bash
sudo apt install -y cmake build-essential pkg-config \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    libgstrtspserver-1.0-dev \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly gstreamer1.0-libav \
    libspdlog-dev nlohmann-json3-dev
```
