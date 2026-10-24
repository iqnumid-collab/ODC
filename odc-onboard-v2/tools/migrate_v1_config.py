#!/usr/bin/env python3
"""Best-effort conversion of odc-onboard v1 JSON config to v2 YAML graph schema."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

try:
    import yaml
except ImportError as exc:  # pragma: no cover - runtime dependency check
    print("PyYAML is required: pip install pyyaml", file=sys.stderr)
    raise SystemExit(1) from exc


DEFAULT_SOCKET = "/run/odc-onboard/odc-onboard.sock"


def _deep_copy(value: Any) -> Any:
    if isinstance(value, dict):
        return {k: _deep_copy(v) for k, v in value.items()}
    if isinstance(value, list):
        return [_deep_copy(v) for v in value]
    return value


def _strip_empty(value: Any) -> Any:
    if isinstance(value, dict):
        cleaned = {k: _strip_empty(v) for k, v in value.items() if v is not None}
        return {k: v for k, v in cleaned.items() if v != {} and v != []}
    if isinstance(value, list):
        return [_strip_empty(v) for v in value if v is not None]
    return value


def _connection_component(
    conn: dict[str, Any],
    vpn_type: str,
    wan_component: str | None,
) -> dict[str, Any]:
    metadata = conn.get("metadata") or {}
    config: dict[str, Any] = {
        "config_path": conn.get("config_path", ""),
    }

    if conn.get("credentials_path"):
        config["creds_path"] = conn["credentials_path"]
    if wan_component:
        config["wan"] = wan_component
    if metadata.get("name"):
        config["name"] = metadata["name"]
    if "auto_start" in metadata:
        config["auto_start"] = metadata["auto_start"]
    if metadata.get("max_retries") is not None:
        config["max_retries"] = metadata["max_retries"]
    if metadata.get("health_check"):
        config["health_check"] = _deep_copy(metadata["health_check"])

    return {
        "id": conn["id"],
        "type": vpn_type,
        "enabled": conn.get("enabled", True),
        "config": config,
    }


def _mavlink_component(mavlink: dict[str, Any]) -> dict[str, Any]:
    config: dict[str, Any] = {}
    for key in (
        "tcp_server_port",
        "dedup_period_sec",
        "sniffer_sysid",
        "report_stats",
        "endpoints",
        "logging",
    ):
        if key in mavlink:
            config[key] = _deep_copy(mavlink[key])

    return {
        "id": "mav_primary",
        "type": "mavlink.router",
        "enabled": mavlink.get("enabled", True),
        "config": config,
    }


def _extender_component(
    ext_id: str,
    extender_auto: dict[str, Any],
) -> dict[str, Any]:
    config: dict[str, Any] = {
        "auto": extender_auto.get("enabled", True),
    }
    if extender_auto.get("udp_port") is not None:
        config["udp_port"] = extender_auto["udp_port"]

    return {
        "id": ext_id,
        "type": "mavlink.extender",
        "enabled": True,
        "config": config,
    }


def _rtsp_component(rtsp: dict[str, Any]) -> dict[str, Any]:
    config = _deep_copy(rtsp)
    config.pop("enabled", None)
    config.pop("auto_start_on_vpn", None)

    return {
        "id": "rtsp0",
        "type": "rtsp.compressor",
        "enabled": rtsp.get("enabled", True),
        "config": config,
    }


def _qmi_component(qmi: dict[str, Any]) -> dict[str, Any]:
    field_map = {
        "device_path": "device",
        "iface_name": "iface",
        "apn": "apn",
        "ip_type": "ip_type",
        "auth_type": "auth_type",
        "username": "username",
        "password": "password",
        "pin": "pin",
        "auto_connect": "auto_connect",
    }

    config: dict[str, Any] = {}
    for src, dst in field_map.items():
        if src in qmi:
            config[dst] = qmi[src]

    return {
        "id": "cellular0",
        "type": "cellular.qmi",
        "enabled": True,
        "config": config,
    }


def _vpn_link(vpn_id: str, target_id: str, suffix: str) -> dict[str, Any]:
    return {
        "id": f"{vpn_id}_{suffix}",
        "from": {"component": vpn_id, "port": "tunnel"},
        "to": {"component": target_id, "port": "transport"},
        "bind": {"fields": ["connection_id", "interface_name", "target_ip"]},
    }


def _router_to_extender_link(ext_id: str) -> dict[str, Any]:
    return {
        "id": f"router_to_{ext_id}",
        "from": {"component": "mav_primary", "port": "router"},
        "to": {"component": ext_id, "port": "upstream"},
        "bind": {"fields": ["router_ref"]},
    }


def _connect_trigger(vpn_id: str, link_ids: list[str]) -> dict[str, Any]:
    return {
        "id": f"{vpn_id}_services_on_connect",
        "on": "vpn.connected",
        "source": vpn_id,
        "activate": link_ids,
    }


def _disconnect_trigger(vpn_id: str, link_ids: list[str]) -> dict[str, Any]:
    return {
        "id": f"{vpn_id}_services_on_disconnect",
        "on": "vpn.disconnected",
        "source": vpn_id,
        "deactivate": link_ids,
    }


def convert_v1_to_v2(v1: dict[str, Any]) -> dict[str, Any]:
    general_v1 = v1.get("general") or {}
    general = {
        "log_level": general_v1.get("log_level", "info"),
        "socket": general_v1.get("socket", DEFAULT_SOCKET),
    }

    components: list[dict[str, Any]] = []
    links: list[dict[str, Any]] = []
    triggers: list[dict[str, Any]] = []

    qmi = v1.get("qmi")
    wan_component: str | None = None
    if qmi:
        qmi_component = _qmi_component(qmi)
        components.append(qmi_component)
        wan_component = qmi_component["id"]

    vpn = v1.get("vpn") or {}
    vpn_ids: list[str] = []

    for conn in (vpn.get("wireguard") or {}).get("connections") or []:
        if not conn.get("id"):
            continue
        components.append(_connection_component(conn, "vpn.wireguard", wan_component))
        vpn_ids.append(conn["id"])

    for conn in (vpn.get("openvpn") or {}).get("connections") or []:
        if not conn.get("id"):
            continue
        components.append(_connection_component(conn, "vpn.openvpn", wan_component))
        vpn_ids.append(conn["id"])

    mavlink = v1.get("mavlink")
    extender_ids: list[str] = []
    if mavlink:
        components.append(_mavlink_component(mavlink))

        extender_auto = mavlink.get("extender_auto") or {}
        if extender_auto.get("enabled"):
            for vpn_id in vpn_ids:
                ext_id = f"mav_ext_{vpn_id}"
                components.append(_extender_component(ext_id, extender_auto))
                extender_ids.append((vpn_id, ext_id))
                links.append(_vpn_link(vpn_id, ext_id, "mavlink"))
                links.append(_router_to_extender_link(ext_id))

    rtsp = v1.get("rtsp")
    rtsp_enabled = bool(rtsp and rtsp.get("enabled", True))
    if rtsp and rtsp_enabled:
        components.append(_rtsp_component(rtsp))

        auto_start = rtsp.get("auto_start_on_vpn", False)
        if auto_start:
            for vpn_id in vpn_ids:
                links.append(_vpn_link(vpn_id, "rtsp0", "rtsp"))

    for vpn_id in vpn_ids:
        activate_links = [
            link["id"]
            for link in links
            if link["from"]["component"] == vpn_id
        ]
        if activate_links:
            triggers.append(_connect_trigger(vpn_id, activate_links))
            triggers.append(_disconnect_trigger(vpn_id, activate_links))

    return _strip_empty(
        {
            "version": 2,
            "general": general,
            "components": components,
            "links": links,
            "triggers": triggers,
        }
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert odc-onboard v1 JSON config to v2 YAML graph schema."
    )
    parser.add_argument("input", type=Path, help="Path to v1 config.json")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Output YAML path (default: stdout)",
    )
    args = parser.parse_args()

    with args.input.open("r", encoding="utf-8") as handle:
        v1 = json.load(handle)

    v2 = convert_v1_to_v2(v1)
    rendered = yaml.safe_dump(
        v2,
        sort_keys=False,
        default_flow_style=False,
        allow_unicode=True,
    )

    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        sys.stdout.write(rendered)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
