# Build System

## Overview

odc-onboard uses CMake with a hybrid dependency strategy: system packages for common libraries and CPM.cmake for cached third-party dependencies.

## Requirements

| Dependency | Source | Version |
|------------|--------|---------|
| CMake | System | >= 3.18 |
| GCC/Clang | System | C++20 support |
| spdlog | System package | Any |
| CLI11 | System package | Any |
| nlohmann_json | System package | Any |
| fmt | System package | Any |
| OpenSSL | System package | Any |
| libsodium | System package | Any |
| CAF | System | 0.17.6 |
| pthread | System | -- |

## Third-Party (CPM.cmake)

| Library | Repository | Version |
|---------|------------|---------|
| asio | chriskohlhoff/asio | asio-1-30-2 |
| jsoncpp | open-source-parsers/jsoncpp | 1.9.6 |
| lz4 | lz4/lz4 | v1.10.0 |

CPM caches dependencies in `~/.cache/CPM` to avoid re-downloading.

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

## Install

```bash
sudo cmake --install .
```

Installs:
- Binary to `/usr/bin/odc-onboard`
- Config files to `/etc/odc-onboard/vpn/`
- systemd service to `/lib/systemd/system/`

## Compiler Flags

```
-std=c++20 -fno-extensions
-std=c11
-Wall -Wextra -Wpedantic -Wno-sign-compare
```

## Third-Party Libraries (Built In-Tree)

| Library | Type | Sources |
|---------|------|---------|
| `thread_orchestrator` | Header-only | `thirdparty/thread_orchestrator/` |
| `wgclient` | Static | `thirdparty/minimal-wireguard-cpp/` + wg-tools C sources |
| `openvpn3_client` | Static | `thirdparty/minimal-openvpn3/` |

### Compile Definitions

**wgclient**:
- `NO_FILE_ENCRYPTYION` (disables file encryption in wg-tools)

**openvpn3_client**:
- `ASIO_STANDALONE`
- `USE_ASIO`
- `HAVE_LZ4`
- `HAVE_JSONCPP`
- `NOMINMAX`
- `USE_OPENSSL`

## Source Files

- `CMakeLists.txt`
