# ultima-qmi-controller Test Suite

Black-box test suite that launches `ultima-qmi-controller` as a pm2 service and validates its behaviour through log analysis.

## Prerequisites

- **Built binary**: `cmake --build build` from `minimal-qmi-cpp/`
- **pm2**: `npm i -g pm2`
- **Python ≥ 3.10**: with pip

## Setup

```bash
cd minimal-qmi-cpp/tests
pip install -r requirements.txt
```

## Running

### Without a modem (CI / desktop)

```bash
pytest -v
```

Tests that require a real QMI modem are automatically skipped.

### With a modem plugged in

```bash
pytest -v                          # auto-detects /dev/cdc-wdm0
QMI_TEST_DEVICE=/dev/cdc-wdm1 pytest -v   # custom device path
```

### Run a specific test class

```bash
pytest -v -k TestPM2Lifecycle
pytest -v -k TestStateMachine
pytest -v -k TestErrorRecovery
pytest -v -k TestLogIntegrity
pytest -v -k TestQmiDeviceOps
```

## Test classes

| Class | Tests | Needs modem | What it checks |
|---|---|---|---|
| `TestPM2Lifecycle` | 5 | No | pm2 start/stop/restart, log output |
| `TestStateMachine` | 10 | Partial | State transitions: IDLE → CONNECTED |
| `TestQmiDeviceOps` | 6 | Yes | Device open, clients, SIM, data, watchdog |
| `TestErrorRecovery` | 5 | Partial | Backoff, graceful shutdown, missing config |
| `TestLogIntegrity` | 9 | Partial | No segfaults, GLib CRITICALs, leaks, protocol errors |

## Custom config

Set `QMI_CONFIG_PATH` to use a specific config file:

```bash
QMI_CONFIG_PATH=/path/to/ultima-qmi.conf pytest -v
```

## Log files

Logs are written to `tests/logs/` and wiped before each test.
