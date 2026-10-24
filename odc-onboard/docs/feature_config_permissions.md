# Feature: Config File Permission Validation

## Overview

odc-onboard validates configuration file permissions at startup and during config validation to prevent credential exposure through overly permissive file modes.

## Checks Performed

| Check | Condition | Result |
|-------|-----------|--------|
| World-readable | Mode has `other` read bit (`o+r`) | Warning |
| World-writable | Mode has `other` write bit (`o+w`) | Warning |

## Automatic Fix

The `fix_config_file_permissions()` function sets the file mode to `0600` (owner read/write only):

```cpp
fix_config_file_permissions("/etc/odc-onboard/config.json");
```

## CLI Validation

```
odc-onboard> vpn config validate /etc/odc-onboard/vpn/my-wg.conf

Config validation: /etc/odc-onboard/vpn/my-wg.conf
Valid: YES

Warnings:
  - Permission issue: File is world-readable
```

## Recommended Permissions

| Path | Mode | Owner |
|------|------|-------|
| `/etc/odc-onboard/` | `700` | `root:root` |
| `/etc/odc-onboard/config.json` | `600` | `root:root` |
| `/etc/odc-onboard/vpn/*.conf` | `600` | `root:root` |
| `/etc/odc-onboard/vpn/*.creds.enc` | `600` | `root:root` |
| `/etc/odc-onboard/vpn/*.key` | `600` | `root:root` |

## Source Files

- `src/core/privilege_check.hpp` (`check_config_file_permissions`, `fix_config_file_permissions`)
