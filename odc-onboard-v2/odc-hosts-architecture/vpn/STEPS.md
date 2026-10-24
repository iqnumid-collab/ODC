# VPN suite steps

Ordered procedure for humans or a future runner. **No execution is implied by this doc alone.**

## 0. Preflight (once per session)

1. From molt: ICMP + SSH to `.11`, `.12`, `.16`.
2. Confirm hub has tunnel-manager (or equivalent) listening for WG and/or OVPN.
3. Confirm each DUT has `odc-onboard-v2` package with VPN tools and profiles.
4. Snapshot current `system.yaml` on each DUT for restore.
5. Create evidence dir: `logs/<date>/<host>/ODC-DEV-VPN/`.

## 1. Apply fixture (per DUT)

1. Install VPN-only YAML (WG profile **or** OVPN profile — one protocol per case group).
2. Ensure no MAV/RTSP components are declared.
3. Reload/restart onboard service so graph loads the fixture.
4. Record `graph.components.list` (or CLI equivalent).

## 2. Variant B — ethernet WAN

1. Assert DUT can reach `192.168.10.16` via eth.
2. Start VPN component.
3. Wait for connected / `ip_ready`.
4. Record overlay IP + iface name.
5. From DUT: ping hub overlay IP; from hub: ping DUT overlay IP (if policy allows).
6. On hub: peer list shows this DUT.
7. Disconnect; wait for down; reconnect; repeat ping.
8. Stop component; confirm iface gone or down.

Repeat steps 2–8 for the other protocol (WG ↔ OVPN) if both are in scope.

## 3. Dual-client concurrent

1. With Variant B still valid, bring up peer on rpi-4b and rpi-5 **together**.
2. Hub peer count ≥ 2.
3. Stop rpi-4b VPN only → rpi-5 still connected.
4. Restart rpi-4b → both up again.
5. Tear down both.

## 4. Variant A — cellular WAN (optional)

1. Prereq: cellular suite PASS or live `wan_ready` on DUT.
2. Prefer routing VPN outer traffic via `wwan0` (document how: default route change or `Table`/`FwMark` if used).
3. Keep SSH orchestration on eth if possible.
4. Re-run connect + overlay ping + reconnect for one protocol (WG preferred).
5. Restore eth default route; tear down cellular session if suite owns it.

## 5. Isolation check

While any VPN case is active:

1. Assert MAV router/extender not running (or not in graph).
2. Assert RTSP compressor not listening.
3. FAIL if fixture or runtime violated isolation.

## 6. Teardown

1. Stop all VPN components on both DUTs.
2. Remove leftover `wg*` / `tun*` ifaces if component left them.
3. Restore previous `system.yaml` and restart service.
4. Archive logs, `ip addr`, `wg show`, OVPN status, hub peer dump to molt evidence dir.

## Step ↔ case mapping

| Steps | Cases |
|-------|-------|
| 0 | VPN-P01…P03 |
| 2 WG | VPN-WG-01…04 |
| 2 OVPN | VPN-OV-01…04 |
| 3 | VPN-MX-01 |
| 4 | VPN-MX-03 |
| 5 | VPN-MX-02 |
| 6 | (housekeeping; no case ID) |
