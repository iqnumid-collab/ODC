# Video suite SKIP/FAIL tracker
run: /home/molt/Documents/GitHub/opendronecore/logs/video-suite/20260725-190512
summary: 66 PASS, 0 FAIL, 2 SKIP

## FAIL
(none)

## SKIP
- VID-OPT-01 (odc-device-rpi-4b): odc-device-rpi-4b no /dev/video0
- VID-OPT-01 (odc-device-rpi-5): odc-device-rpi-5 no /dev/video0

## Notes
- Only remaining SKIP is VID-OPT-01 (no USB camera on lab DUTs).
- Prior failures cleared: pkill/self-match, x264enc missing, glib types, IPC UAF, software ClientConnected, SourceLost unprepared hook, ABR force_cycle lab path.
