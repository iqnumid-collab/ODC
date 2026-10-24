# odc-onboard Documentation

## User Guides

| Document | Description |
|----------|-------------|
| [CLI Operations](cli_operations.md) | Complete guide to CLI commands and options |
| [systemd Service](systemd_service.md) | Service installation, management, and security hardening |

## Feature Documentation

| Document | Description |
|----------|-------------|
| [IPC Daemon Architecture](feature_ipc_daemon.md) | Daemon + client via JSON-RPC over Unix sockets |
| [Privilege Check](feature_privilege_check.md) | CAP_NET_ADMIN verification and setup |
| [Credential Encryption](feature_credential_encryption.md) | libsodium-based credential encryption |
| [WireGuard Connection](feature_wireguard.md) | WireGuard connection lifecycle and configuration |
| [OpenVPN Connection](feature_openvpn.md) | OpenVPN connection lifecycle and TUN builder |
| [TUN Device Management](feature_tun_device.md) | TUN/TAP device creation via /dev/net/tun |
| [Netlink Manager](feature_netlink.md) | Low-level network interface configuration |
| [ICMP Ping Health Checks](feature_icmp_ping.md) | Connectivity monitoring and auto-reconnect |
| [Interface Statistics](feature_interface_stats.md) | Traffic stats from sysfs |
| [Config Hot-Reload](feature_config_hot_reload.md) | inotify-based configuration watching |
| [Config Permissions](feature_config_permissions.md) | File permission validation and enforcement |
| [Config Validation](feature_config_validation.md) | VPN config file format validation |
| [Graceful Shutdown](feature_graceful_shutdown.md) | Signal handling and drain timeout |
| [Event Bus](feature_event_bus.md) | Publish/subscribe event system |
| [CAF Actor System](feature_actor_system.md) | Actor-based concurrent components |
| [Thread Orchestrator](feature_thread_orchestrator.md) | Async task pool with futures and cancellation |
| [VPN Manager](feature_vpn_manager.md) | Connection registry and lifecycle management |
| [JSON Output Mode](feature_json_output.md) | Machine-readable output for automation |
| [Log Control](feature_log_control.md) | Dynamic logging with spdlog |
| [App Lifecycle](feature_app_lifecycle.md) | Startup and shutdown orchestration |
| [Build System](build_system.md) | CMake configuration and dependencies |
