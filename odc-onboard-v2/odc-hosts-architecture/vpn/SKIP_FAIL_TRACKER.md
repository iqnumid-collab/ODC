# VPN suite skip/fail tracker

**Last run:** `logs/vpn-suite/20260725-155803` → **25 PASS, 0 FAIL, 2 SKIP**  
**Ownership rule:** tunnels/ifaces → `odc-onboard-v2` + `odc-tunnel-manager`; certs/keys/profiles → `odc-tunnel-manager` (no openssl/`wg`/`system()` in VPN binary paths).

## Ownership (tunnel-manager)

| Area | Change |
|------|--------|
| CertManager | `issue_server_cert` (serverAuth), clientAuth clients, raw WG `generate_wg_keypair`, `write_tls_auth_key`, `ca_cert_pem` |
| Bootstrap | On start: CA, OVPN server cert, tls-auth, WG server key — all in-process |
| IPC | Accept loop on dedicated thread; `tunnel.peers.disconnect` by CN for OpenVPN |
| `tunnel.clients.issue` | Issues OVPN/WG profiles via `odc-cli tunnel clients issue` |
| WireGuard | Persist real keys; full peer set on `REPLACE_PEERS`; decode private key on add/remove |
| OpenVPN server | In-process OpenVPN3 `ServerProto` (UDP); no classic `openvpn` package/child |
| Lab scripts | Provision via tunnel-manager only (`lab-provision-vpn-on-hexa.sh`) |

## OpenVPN client data-plane — fixed

| Issue | Fix |
|-------|-----|
| Dual TUN (`tun` + `ovpn-*`) | Force `USE_TUN_BUILDER`; strip `dev` from profile; keep pre-created TUN fd across `tun_builder_reset` |
| ASIO freeze after CONNECTED | **Root cause of OV-02/03:** `apply_network_config()` ran synchronous netlink on the OpenVPN io-thread → blocked forever → UDP Recv-Q grew, keepalives died, ping TX stalled. Removed; iface is configured only in `tun_builder_establish` |
| Netlink hangs / bad flags | `NLM_F_ACK` + `SO_RCVTIMEO`; `ifi_change=IFF_UP` only (was `0xFFFFFFFF` clobbering flags); IPv4 subnet attrs `IFA_LOCAL`+`IFA_ADDRESS`+`IFA_BROADCAST` |
| Profile permission ABRT | Suite `chmod a+r` on vpn profiles |

## In-process OpenVPN server — fixed

| Issue | Fix |
|-------|-----|
| `BIO_R_INVALID_ARGUMENT` mid-handshake | `ssl_cfg->set_frame(frame)` before `new_factory()` (null Frame → MemQ `frame_uninitialized`) |
| `AES-256-GCM: bad cipher for data channel` | `CryptoAlgs::allow_default_dc_algs` after SSL factory (same as client path) |
| Stale duplicate-CN / VIP mux | Disconnect previous CN on auth; registry disconnect only if Recv still owns CN |

## Current suite matrix

| Result | Cases |
|--------|-------|
| PASS | All VPN-P*, VPN-WG-*, VPN-OV-*, VPN-MX-01/02 |
| SKIP | VPN-MX-03 ×2 — cellular WAN not automated |

## Remaining external runtime

| Item | Notes |
|------|-------|
| VPN-MX-03 | Needs SIM/APN + `apply_wan` path |
