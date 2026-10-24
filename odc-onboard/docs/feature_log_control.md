# Feature: Log Control

## Overview

odc-onboard uses spdlog for structured, colored console logging with dynamic log level control at runtime.

## Log Levels

| Level | Description |
|-------|-------------|
| `trace` | Detailed tracing information |
| `debug` | Debugging information |
| `info` | General operational information |
| `warn` | Warning conditions |
| `error` | Error conditions |
| `critical` | Critical failures |
| `off` | Logging disabled |

## Named Loggers

Loggers are created on demand by name:

```cpp
auto logger = log_control.get_logger("main");
SPDLOG_LOGGER_INFO(logger, "Application started");
```

## Dynamic Log Level Changes

### CLI Console

```
odc-onboard> log level debug
Log level set to: debug
```

### EventBus

The Log Actor subscribes to `SetLogLevelEvent` for remote log level changes:

```cpp
bus.publish(SetLogLevelEvent{
    .logger_name = "",  // empty = global default
    .level = LogLevel::Debug
});
```

## Event Integration

The Log Actor subscribes to the EventBus for:
- `LogEvent` -- routes log messages through spdlog
- `SetLogLevelEvent` -- changes log level at runtime

## Output Format

Colored stdout output via spdlog's `color_stdout_sink`:

```
[2026-05-24 10:30:00.123] [info] [main] Application started
[2026-05-24 10:30:00.456] [debug] [actor] Processing event
```

## Source Files

- `src/core/log_control.hpp`
