# ── odc-hook Multi-Platform Build System ──────────────────────────────────────

Docker-based cross-compilation for Linux, Windows, and macOS from a Linux host.

## Directory Structure

```
deploy/
├── README.md                 # This file
├── Makefile                  # Orchestration: make linux / windows / macos / all / clean
├── docker-compose.yml        # Alternative: docker compose run <platform>-builder
│
├── linux/
│   ├── Dockerfile            # Native g++-12 build on ubuntu:22.04
│   ├── entrypoint.sh         # Build script (runs inside container)
│   └── install.sh            # Install script (bundled into output)
│
├── windows/
│   ├── Dockerfile            # MinGW-w64 cross-compiler + OpenSSL cross-build
│   ├── toolchain.cmake       # CMake toolchain for Windows x86_64
│   ├── entrypoint.sh         # Build script
│   └── install.ps1           # PowerShell install script (bundled into output)
│
├── macos/
│   ├── Dockerfile            # osxcross cross-compiler + OpenSSL cross-build
│   ├── toolchain.cmake       # CMake toolchain for macOS x86_64
│   ├── entrypoint.sh         # Build script
│   ├── install.sh            # Install script (bundled into output)
│   ├── com.odc.hook.plist    # launchd plist (bundled into output)
│   └── SDK_README.md         # How to obtain Apple SDK (license required)
│
├── common/
│   └── build-common.sh       # Shared helpers (OpenSSL/SQLite cross-compile, flags)
│
└── output/
    ├── linux/                # odc-hook + odc-hook.yaml + odc-hook.service + install.sh
    ├── windows/              # odc-hook.exe + odc-hook.yaml + install.ps1
    └── macos/                # odc-hook + odc-hook.yaml + com.odc.hook.plist + install.sh
```

## Quick Start

### Prerequisites

- Docker 24+ with BuildKit enabled (`export DOCKER_BUILDKIT=1`)
- `make`
- Internet access (for CPM fetching dependencies)

### Build for Linux

```bash
cd odc-root
make -f deploy/Makefile linux
# Output: deploy/output/linux/
#   odc-hook             # binary
#   odc-hook.yaml        # config template
#   odc-hook.service     # systemd unit
#   install.sh           # install script
```

### Build for Windows

```bash
make -f deploy/Makefile windows
# Output: deploy/output/windows/
#   odc-hook.exe         # binary (statically linked)
#   odc-hook.yaml        # config template
#   install.ps1          # install script
```

### Build for macOS

First, obtain the macOS SDK (see [macos/SDK_README.md](macos/SDK_README.md)). Then:

```bash
make -f deploy/Makefile macos SDK_TARBALL=deploy/macos/MacOSX14.0.sdk.tar.xz
# Output: deploy/output/macos/
#   odc-hook             # binary (statically linked)
#   odc-hook.yaml        # config template
#   com.odc.hook.plist   # launchd plist
#   install.sh           # install script
```

### Build All

```bash
make -f deploy/Makefile all
```

### Using docker-compose

```bash
cd odc-root/deploy

# Linux
docker compose build linux-builder
docker compose run --rm linux-builder

# Windows
docker compose build windows-builder
docker compose run --rm windows-builder
```

## Platform Details

| Platform  | Compiler                    | Linkage   | Target Triple           |
|-----------|-----------------------------|-----------|-------------------------|
| Linux     | g++-12 (native)             | dynamic   | x86_64-linux-gnu        |
| Windows   | MinGW-w64 gcc 12            | static    | x86_64-w64-mingw32      |
| macOS     | Apple Clang (osxcross)      | static    | x86_64-apple-darwin     |

### Dependency Resolution

All C++ dependencies (spdlog, fmt, yaml-cpp, nlohmann_json, CLI11, asio, lz4, SQLite3, Catch2)
are fetched and built from source via CPM (CMake Package Manager) during the CMake configure
step. This ensures identical dependency versions across all platforms.

The only pre-built dependency is **OpenSSL**, which is cross-compiled from source in the
Dockerfile for Windows and macOS targets. For Linux, `libssl-dev` is installed via apt.

### Build Time Estimates

| Platform  | First Build | Cached Build |
|-----------|-------------|--------------|
| Linux     | ~3 minutes  | ~30 seconds  |
| Windows   | ~8 minutes  | ~2 minutes   |
| macOS     | ~15 minutes | ~3 minutes   |

First builds include downloading and compiling all CPM dependencies plus OpenSSL.
Cached builds benefit from Docker layer caching and CMake cache reuse.

## Binary Verification

After building, verify the binary type:

```bash
# Linux
file deploy/output/linux/odc-hook
# → ELF 64-bit LSB executable, x86-64

# Windows
file deploy/output/windows/odc-hook.exe
# → PE32+ executable (console) x86-64

# macOS
file deploy/output/macos/odc-hook
# → Mach-O 64-bit executable x86_64
```

## CI/CD Integration

Example GitHub Actions workflow:

```yaml
jobs:
  build-linux:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - name: Build odc-hook (Linux)
        run: make -f deploy/Makefile linux

  build-windows:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - name: Build odc-hook (Windows)
        run: make -f deploy/Makefile windows
```
