# Build Export Directory

This directory contains per-platform build artifacts produced by the Docker cross-compilation pipeline.

## Directory Structure

```
export/
├── linux/
│   ├── odc-hook                  # Linux x86_64 binary
│   ├── odc-registry-manager       # Linux x86_64 binary
│   ├── odc-cli                    # Linux x86_64 binary
│   ├── odc-tunnel-manager         # Linux x86_64 binary (if built)
│   ├── install.sh                 # Linux install script
│   ├── config/
│   │   ├── odc-hook.yaml
│   │   ├── registry-manager.yaml
│   │   ├── tunnel-manager.yaml
│   │   └── *.service              # systemd unit files
│   ├── tests/                     # Test executables
│   └── certs/                     # Test certificates (integration test)
├── windows/
│   ├── odc-hook.exe               # Windows x86_64 PE32+ binary
│   ├── odc-cli.exe                # Windows x86_64 PE32+ binary
│   ├── wintun.dll                 # Signed TUN driver (required at runtime)
│   ├── install.ps1                # Windows PowerShell install script
│   ├── config/
│   │   └── odc-hook.yaml
│   └── tests/                     # Test executables (.exe)
└── macos/
    ├── odc-hook                   # macOS x86_64 Mach-O binary
    ├── odc-cli                    # macOS x86_64 Mach-O binary
    ├── install.sh                 # macOS install script
    ├── config/
    │   ├── odc-hook.yaml
    │   └── com.odc.hook.plist     # macOS LaunchDaemon plist
    └── tests/                     # Test executables
```

## Usage

Each directory is a self-contained deployment package. Copy the entire directory to the target machine and run the install script, or place binaries and configs manually.

```
# Linux
make -f deploy/Makefile linux
scp -r deploy/export/linux/ user@server:/tmp/
ssh user@server "cd /tmp/linux && sudo ./install.sh"

# Windows
make -f deploy/Makefile windows
# Copy deploy/export/windows/ to Windows machine, run install.ps1

# macOS
make -f deploy/Makefile macos SDK_TARBALL=path/to/MacOSX14.0.sdk.tar.xz
# Copy deploy/export/macos/ to Mac, run install.sh
```
