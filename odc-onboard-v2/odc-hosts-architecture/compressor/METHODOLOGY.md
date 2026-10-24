# Compressor test methodology

![Test methodology](diagrams/rendered/compressor-test-methodology.svg)

## Result model

| Result | Meaning |
|--------|---------|
| **PASS** | Prereqs present and all assertions for the case succeeded |
| **FAIL** | Prereqs present but behavior incorrect |
| **SKIP** | Declared attachment / tool / media / **LAN peer** missing |

## Decision tree (every case)

1. **Preflight** — tools, media, ports; for XBR also **L3 reachability** between hosts.  
2. **Stimulus** — start/stop source/compressor/monitor on the **correct host roles**.  
3. **Observe** — monitor stdout **on the consumer host**, DUT `graph.status` / events, journals on source + compressor hosts, `ss` on listeners.  
4. **Judge** — compare to [SUITE_COMPRESSOR.md](SUITE_COMPRESSOR.md).

## Mandatory vs optional

| Set | Cases | Required for suite green |
|-----|-------|--------------------------|
| Local mandatory | PRE + OPS-01..05 + OPS-09..10 + EVT-01..04 + EVT-08 | Yes (both DUTs) |
| Bridge mandatory | XBR-01..03 per DUT + XBR-04/05 matrix | Yes |
| Extended | OPS-06..08, 11..13, EVT-05..07, XBR-06..08 | Run when ready; FAIL if wrong |
| Optional | OPT-01 USB, OPT-02 hexa viewer | SKIP without hardware/tools |

## Cross-host evidence (in addition to local)

| Artifact | Where collected |
|----------|-----------------|
| `monitor-remote.txt` | Consumer host (molt / peer DUT / hexa) |
| `monitor-local.txt` | DUT (for dual-path XBR-03) |
| `source-host.txt` | Host running `odc-video-source` (DUT or molt) |
| `ping-mesh.txt` | molt↔DUT, DUT↔DUT |
| `ss-ports-<host>.txt` | Every host that binds 8554/8555 |
| `events.jsonl` | Compressor DUT (remote client events for XBR-07) |

Root: `logs/video-suite/<timestamp>/` with subdirs `molt/`, `odc-device-rpi-4b/`, `odc-device-rpi-5/`, optional `hexa-five/`.

## Bridge-specific flake controls

| Risk | Mitigation |
|------|------------|
| Wrong ingest host | Fixture `input_host` must be **source LAN IP**, not `127.0.0.1` |
| Bind localhost only | Compressor / video-source listen `0.0.0.0` for bridge cases |
| Firewall / wifi path | Prefer eth0 `192.168.10.0/24`; document if wlan used |
| NAT confusion | All lab hosts on same L2/L3; no port forwards |
| Asymmetric BW | Assert both directions independently in XBR-05 |
| Clock / settle | Wait after remote connect before BW sample |

## Isolation checks

- No live `vpn.*` / `mavlink.*` / `cellular.qmi` in the fixture.  
- Bridge traffic is **lab ethernet RTSP**, not tunnel overlays.  
- After teardown, suite ports clear on **all** hosts that participated.
