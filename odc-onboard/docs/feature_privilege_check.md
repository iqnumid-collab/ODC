# Feature: Privilege Check

## Overview

odc-onboard requires the `CAP_NET_ADMIN` Linux capability to manage network interfaces, create TUN devices, and configure routes. The privilege check runs at startup and can be invoked standalone.

## Implementation

Uses the `capget` syscall directly (no `libcap` dependency) to inspect the process's effective capability set.

## Checks Performed

1. **Root status** -- checks if the effective UID is 0
2. **CAP_NET_ADMIN** -- checks if the capability is present in the effective set
3. **Missing capabilities** -- lists any required capabilities that are absent

## Usage

### Command-line

```bash
# Text output
odc-onboard --check-privileges

# JSON output
odc-onboard --check-privileges --json
```

### CLI Console

```
odc-onboard> check-privileges
Running as root: NO
CAP_NET_ADMIN: YES
```

### Startup Enforcement

If `CAP_NET_ADMIN` is missing and the process is not running as root, odc-onboard exits immediately with code 1:

```
ERROR: Missing CAP_NET_ADMIN privilege. VPN operations require network administration rights.

Fix options:
  1. Run with: sudo ./odc-onboard
  2. Set capability: sudo setcap cap_net_admin+ep ./odc-onboard
  3. Use --check-privileges to verify before starting
```

## Granting Privileges

### Option 1: Run as root

```bash
sudo odc-onboard
```

### Option 2: Set file capability

```bash
sudo setcap cap_net_admin+ep $(which odc-onboard)
```

### Option 3: Use grant-requirements in CLI

```
odc-onboard> grant-requirements
Enter sudo password:
```

This automates capability setup, directory creation, and permission configuration.

## JSON Output Schema

```json
{
  "is_root": false,
  "has_net_admin": true,
  "missing_capabilities": []
}
```

## Source Files

- `src/core/privilege_check.hpp`
