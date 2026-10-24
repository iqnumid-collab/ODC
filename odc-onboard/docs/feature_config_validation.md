# Feature: VPN Config Validation

## Overview

The `vpn config validate` command validates VPN configuration files before they are added to the system. It checks file existence, format, content, and permissions.

## Validation Checks

### File Checks

| Check | Condition | Result |
|-------|-----------|--------|
| File exists | `std::filesystem::exists(path)` | Error if missing |
| File non-empty | File size > 0 | Error if empty |

### WireGuard Format Checks

| Check | Condition | Result |
|-------|-----------|--------|
| `[Interface]` section | Content contains `[Interface]` | Error if missing |
| `[Peer]` section | Content contains `[Peer]` | Error if missing |
| `PrivateKey` | Content contains `PrivateKey` | Error if missing |

### OpenVPN Format Checks

| Check | Condition | Result |
|-------|-----------|--------|
| Format detection | Content contains `dev ` or `remote ` | Warning if not detected |

### Permission Checks

| Check | Condition | Result |
|-------|-----------|--------|
| World-readable | Mode has `o+r` | Warning |
| World-writable | Mode has `o+w` | Warning |

## Usage

```
odc-onboard> vpn config validate /etc/odc-onboard/vpn/my-wg.conf

Config validation: /etc/odc-onboard/vpn/my-wg.conf
Valid: YES
```

### With Warnings

```
odc-onboard> vpn config validate /etc/odc-onboard/vpn/bad.conf

Config validation: /etc/odc-onboard/vpn/bad.conf
Valid: NO

Errors:
  - Missing [Peer] section

Warnings:
  - Permission issue: File is world-readable
```

### JSON Output

```
odc-onboard> --json
odc-onboard> vpn config validate /path/to/wg.conf
{
  "valid": true,
  "path": "/path/to/wg.conf",
  "errors": [],
  "warnings": []
}
```

## Source Files

- `src/cli/cli_app.hpp` (`cmd_vpn_config_validate`)
- `src/core/privilege_check.hpp` (`check_config_file_permissions`)
