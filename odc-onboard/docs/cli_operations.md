# CLI Operations Guide

## Invocation

```
odc-onboard [OPTIONS]
```

## Command-Line Options

| Flag | Description | Default |
|------|-------------|---------|
| `--config <path>` | Path to the JSON configuration file | `/etc/odc-onboard/config.json` |
| `--cli` | Start the interactive CLI console | Daemon mode (no console) |
| `--check-privileges` | Verify `CAP_NET_ADMIN` capability and exit | Off |
| `--json` | Enable JSON output for all console output | Off |
| `--drain-timeout <seconds>` | Graceful shutdown drain timeout | `10` |
| `--encrypt-creds <in> <out> <key>` | Encrypt a credentials file | -- |
| `--decrypt-creds <in> <key>` | Decrypt and print credentials to stdout | -- |
| `--help`, `-h` | Show help text | -- |

## Running Modes

### Daemon Mode (default)

Runs in the background, manages VPN connections, and responds to signals.

```bash
odc-onboard --config /etc/odc-onboard/config.json
```

### Interactive CLI Mode

Starts a console for managing VPN connections in real time.

```bash
odc-onboard --config /etc/odc-onboard/config.json --cli
```

### Privilege Check Only

Verify that the process has the required capabilities without starting any services.

```bash
odc-onboard --check-privileges
odc-onboard --check-privileges --json
```

## Interactive CLI Commands

Once inside the CLI console (prompt: `odc-onboard>`), the following commands are available.

### System Commands

#### `grant-requirements [password]`

Automatically configures the system for odc-onboard operation. Requires `sudo` access.

Steps performed:
1. Sets `CAP_NET_ADMIN` capability on the binary
2. Creates `/etc/odc-onboard` directory with mode `700`, owned by `root:root`
3. Ensures `/dev/net/tun` device exists
4. Reloads systemd daemon

If no password is provided inline, the command prompts with echo-off for security.

```
odc-onboard> grant-requirements
Enter sudo password:

Granting requirements...

  Setting CAP_NET_ADMIN capability on binary... OK
  Creating config directory... OK
  Setting config directory permissions (700)... OK
  Setting config directory ownership (root:root)... OK
  Ensuring /dev/net/tun exists... OK
  Reloading systemd daemon... OK

Verifying privileges...
  CAP_NET_ADMIN granted successfully
```

#### `check-privileges`

Displays the current privilege status.

```
odc-onboard> check-privileges
Running as root: NO
CAP_NET_ADMIN: NO
Missing capabilities: CAP_NET_ADMIN
```

With JSON output enabled:

```
odc-onboard> --json
odc-onboard> check-privileges
{
  "is_root": false,
  "has_net_admin": false,
  "missing_capabilities": ["CAP_NET_ADMIN"]
}
```

#### `log level <level>`

Changes the log level at runtime.

```
odc-onboard> log level debug
Log level set to: debug
```

Valid levels: `trace`, `debug`, `info`, `warn`, `error`.

#### `metrics`

Displays thread orchestrator statistics.

```
odc-onboard> metrics

Thread Orchestrator Metrics:
  Active tasks:    2
  Pending tasks:   0
  Completed tasks: 15
  Worker count:    4
```

### VPN Management Commands

#### `vpn list`

Lists all configured VPN connections.

```
odc-onboard> vpn list

VPN Connections:
ID                  Type        State         Enabled   Name
----------------------------------------------------------------------------
test-wg             wireguard   Idle          yes       Test WG
test-ovpn           openvpn     Idle          yes       Test OVPN
```

#### `vpn status <id>`

Shows detailed status for a specific connection.

```
odc-onboard> vpn status test-wg

Connection: test-wg
  Type:      wireguard
  State:     Connected
  Enabled:   yes
  Name:      Test WG
  Config:    /etc/odc-onboard/vpn/test-wg.conf

  Statistics:
    Bytes in:      1048576
    Bytes out:     524288
    Packets in:    1024
    Packets out:   512
    Duration:      3600s
    Reconnects:    0

  Health:
    Healthy:       yes
    Latency:       12ms
    Failures:      0
```

#### `vpn start <id>`

Starts a VPN connection.

```
odc-onboard> vpn start test-wg
Starting connection: test-wg
```

#### `vpn stop <id>`

Stops a VPN connection.

```
odc-onboard> vpn stop test-wg
Stopping connection: test-wg
```

#### `vpn restart <id>`

Restarts a connection (stop, 2s delay, start).

```
odc-onboard> vpn restart test-wg
Restarting connection: test-wg
```

#### `vpn remove <id>`

Removes a connection and persists the change to the config file.

```
odc-onboard> vpn remove test-wg
Removed connection: test-wg
```

#### `vpn edit <id>`

Shows the config file path and editable fields for a connection.

```
odc-onboard> vpn edit test-wg

Edit connection: test-wg
  Config file: /etc/odc-onboard/vpn/test-wg.conf

To edit this connection:
  1. Edit the config file: /etc/odc-onboard/vpn/test-wg.conf
  2. Or edit credentials file if OpenVPN: /etc/odc-onboard/vpn/test-wg.conf.creds
  3. Restart the connection: vpn restart test-wg
```

### VPN Add Commands

#### `vpn add wireguard`

Adds a new WireGuard connection from a `.conf` file.

```
odc-onboard> vpn add wireguard --id my-wg --config /path/to/wg.conf --name "My WG" --auto-start
Added WireGuard connection: my-wg
```

| Option | Required | Description |
|--------|----------|-------------|
| `--id <id>` | Yes | Unique connection identifier |
| `--config <path>` | Yes | Path to the WireGuard `.conf` file |
| `--name <name>` | No | Friendly display name |
| `--auto-start` | No | Automatically start on app launch |

#### `vpn add openvpn`

Adds a new OpenVPN connection from an `.ovpn` file.

```
odc-onboard> vpn add openvpn --id my-ovpn --config /path/to/client.ovpn --creds /path/to/creds.enc --name "My OVPN" --auto-start
Added OpenVPN connection: my-ovpn
```

| Option | Required | Description |
|--------|----------|-------------|
| `--id <id>` | Yes | Unique connection identifier |
| `--config <path>` | Yes | Path to the OpenVPN `.ovpn` file |
| `--creds <path>` | No | Path to encrypted credentials file |
| `--name <name>` | No | Friendly display name |
| `--auto-start` | No | Automatically start on app launch |

### VPN Config Commands

#### `vpn config validate <path>`

Validates a VPN configuration file.

```
odc-onboard> vpn config validate /path/to/wg.conf

Config validation: /path/to/wg.conf
Valid: YES
```

Checks performed:
- File exists and is non-empty
- WireGuard format: `[Interface]`, `[Peer]`, `PrivateKey` sections present
- OpenVPN format: `dev` or `remote` directives present
- File permissions: warns if world-readable or world-writable

### Output Mode

#### `--json` / `--no-json`

Toggle JSON output for all subsequent commands within the session.

```
odc-onboard> --json
JSON output mode enabled
odc-onboard> vpn list
[
  {
    "id": "test-wg",
    "type": "wireguard",
    "state": "Idle",
    "enabled": true,
    "name": "Test WG",
    ...
  }
]
```

### Utility Commands

| Command | Description |
|---------|-------------|
| `help`, `?` | Show available commands |
| `clear` | Clear the terminal screen |
| `exit`, `quit` | Exit the CLI console |
| `EOF` (Ctrl+D) | Exit the CLI console |

### Unknown Commands

Typing an unrecognized command produces an error:

```
odc-onboard> foobar
Unknown command: foobar
```
