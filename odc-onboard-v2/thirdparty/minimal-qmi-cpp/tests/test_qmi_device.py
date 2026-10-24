#!/usr/bin/env python3
"""Real-device integration tests for ultima-qmi-controller.

Requires:
  - A real QMI modem attached (e.g. /dev/cdc-wdm0)
  - Root privileges (for QMI device access and ip commands)
  - The binary built at ../build/ultima-qmi-controller

Usage:
  sudo python3 test_qmi_device.py [/dev/cdc-wdmX]
"""

import os, sys, re, glob, time, signal, subprocess, tempfile, atexit

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BINARY = os.path.join(ROOT, "build", "ultima-qmi-controller")

# ── Test framework ───────────────────────────────────────────
passed = failed = skipped = 0
proc = None
log_lines = []


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        print(f"  \u2713 {name}")
        passed += 1
    else:
        print(f"  \u2717 {name}" + (f" \u2014 {detail}" if detail else ""))
        failed += 1


def skip(name, reason=""):
    global skipped
    print(f"  - SKIP {name}" + (f" \u2014 {reason}" if reason else ""))
    skipped += 1


def cleanup():
    if proc and proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


atexit.register(cleanup)


# ── Helpers ──────────────────────────────────────────────────
def find_qmi_device():
    """Auto-detect a QMI device via /dev/cdc-wdm*."""
    devs = sorted(glob.glob("/dev/cdc-wdm*"))
    for d in devs:
        # Verify it's backed by qmi_wwan driver
        name = os.path.basename(d)
        driver_link = f"/sys/class/usbmisc/{name}/device/driver"
        if os.path.islink(driver_link):
            drv = os.path.basename(os.readlink(driver_link))
            if drv in ("qmi_wwan", "cdc_wdm"):
                return d
    # Fallback: return first cdc-wdm if any
    return devs[0] if devs else None


def find_wwan_iface(device_path):
    """Find the wwan interface associated with a QMI device."""
    name = os.path.basename(device_path)
    sysfs = f"/sys/class/usbmisc/{name}/device"
    if not os.path.exists(sysfs):
        return "wwan0"
    # Walk up to USB device, look for net children
    try:
        usb_dev = os.path.realpath(os.path.join(sysfs, ".."))
        for root, dirs, _ in os.walk(usb_dev):
            if "net" in dirs:
                net_dir = os.path.join(root, "net")
                ifaces = os.listdir(net_dir)
                for iface in ifaces:
                    if iface.startswith("wwan"):
                        return iface
    except OSError:
        pass
    return "wwan0"


def write_config(device, iface, **overrides):
    """Write a test config INI and return its path."""
    defaults = dict(
        apn="", ip_type=4, auth="none", pin="",
        max_retries=3, backoff_base_ms=500, backoff_max_ms=5000,
        backoff_jitter_ms=200, circuit_breaker_ms=999999,
        watchdog_interval_s=10, ping_target="8.8.8.8",
        modem_info_interval_s=5,
        open_timeout_ms=15000, init_timeout_ms=15000,
        sim_timeout_ms=15000, reg_timeout_ms=60000,
        data_timeout_ms=30000,
    )
    defaults.update(overrides)
    d = defaults
    content = f"""[device]
path = {device}
interface = {iface}

[connection]
apn = {d['apn']}
ip_type = {d['ip_type']}
auth = {d['auth']}
auto_connect = true

[sim]
pin = {d['pin']}

[recovery]
max_retries = {d['max_retries']}
backoff_base_ms = {d['backoff_base_ms']}
backoff_max_ms = {d['backoff_max_ms']}
backoff_jitter_ms = {d['backoff_jitter_ms']}
circuit_breaker_ms = {d['circuit_breaker_ms']}

[watchdog]
interval_s = {d['watchdog_interval_s']}
ping_target = {d['ping_target']}
modem_info_interval_s = {d['modem_info_interval_s']}

[timeouts]
open_timeout_ms = {d['open_timeout_ms']}
init_timeout_ms = {d['init_timeout_ms']}
sim_timeout_ms = {d['sim_timeout_ms']}
reg_timeout_ms = {d['reg_timeout_ms']}
data_timeout_ms = {d['data_timeout_ms']}

[kafka]
enabled = false
"""
    f = tempfile.NamedTemporaryFile(mode="w", suffix=".conf", delete=False)
    f.write(content)
    f.close()
    return f.name


def start_controller(config_path, timeout=60):
    """Start the controller binary, return Popen."""
    global proc, log_lines
    log_lines = []
    proc = subprocess.Popen(
        [BINARY, config_path],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1,
    )
    return proc


def collect_logs(duration):
    """Read stderr lines for `duration` seconds, append to log_lines."""
    import select
    deadline = time.time() + duration
    while time.time() < deadline:
        remaining = max(0.05, deadline - time.time())
        ready, _, _ = select.select([proc.stdout], [], [], min(remaining, 0.2))
        if ready:
            line = proc.stdout.readline()
            if line:
                log_lines.append(line.rstrip("\n"))


def wait_for_log(pattern, timeout=30):
    """Block until a log line matches `pattern` regex, or timeout."""
    import select
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = max(0.05, deadline - time.time())
        ready, _, _ = select.select([proc.stdout], [], [], min(remaining, 0.2))
        if ready:
            line = proc.stdout.readline()
            if line:
                log_lines.append(line.rstrip("\n"))
                if re.search(pattern, line):
                    return line.rstrip("\n")
    return None


def stop_controller():
    """Send SIGTERM and wait for clean exit."""
    global proc
    if proc and proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    ret = proc.returncode if proc else None
    proc = None
    return ret


def has_log(pattern):
    """Check if any collected log line matches pattern."""
    return any(re.search(pattern, l) for l in log_lines)


def get_log(pattern):
    """Return first log line matching pattern, or None."""
    for l in log_lines:
        if re.search(pattern, l):
            return l
    return None


def iface_has_ip(iface):
    """Check if interface has an IPv4 address."""
    try:
        out = subprocess.check_output(
            ["ip", "-4", "addr", "show", iface], text=True, timeout=5
        )
        return "inet " in out
    except Exception:
        return False


def can_ping(target, iface, count=1, timeout=5):
    """Ping through the wwan interface."""
    try:
        r = subprocess.run(
            ["ping", "-c", str(count), "-W", str(timeout), "-I", iface, target],
            capture_output=True, timeout=timeout + 2,
        )
        return r.returncode == 0
    except Exception:
        return False


# ── Test 0: Prerequisites ────────────────────────────────────
def test_prerequisites(device, iface):
    print("\n[0] Prerequisites")
    check("binary exists", os.path.isfile(BINARY), BINARY)
    check("binary executable", os.access(BINARY, os.X_OK))
    check("QMI device exists", os.path.exists(device), device)
    check("QMI device readable", os.access(device, os.R_OK), "need root?")
    check("wwan iface found", iface is not None, "no wwan* interface")
    check("running as root", os.geteuid() == 0, "some tests need root")
    return os.path.isfile(BINARY) and os.path.exists(device)


# ── Test 1: Device open and client allocation ────────────────
def test_device_open(device, iface):
    print("\n[1] Device open + QMI client allocation")
    cfg = write_config(device, iface)
    try:
        start_controller(cfg)
        line = wait_for_log(r"transition:.*DEVICE_OPENING", timeout=15)
        check("entered DEVICE_OPENING", line is not None)

        line = wait_for_log(r"DEVICE_OPENED", timeout=20)
        check("device opened", line is not None)

        line = wait_for_log(r"allocating QMI clients", timeout=5)
        check("client allocation started", line is not None)

        line = wait_for_log(r"CLIENTS_READY", timeout=20)
        check("clients allocated", line is not None)
    finally:
        stop_controller()
        os.unlink(cfg)


# ── Test 2: HW info collection ───────────────────────────────
def test_hw_info(device, iface):
    print("\n[2] Hardware info collection")
    cfg = write_config(device, iface)
    try:
        start_controller(cfg)
        line = wait_for_log(r"collecting modem hardware identification", timeout=30)
        check("hw info collection started", line is not None)

        line = wait_for_log(r"HW_INFO_DONE", timeout=20)
        check("hw info collected", line is not None)

        # Check for modem online
        line = wait_for_log(r"MODEM_ONLINE", timeout=15)
        check("modem set online", line is not None)
    finally:
        stop_controller()
        os.unlink(cfg)


# ── Test 3: SIM authentication ───────────────────────────────
def test_sim_auth(device, iface):
    print("\n[3] SIM authentication")
    cfg = write_config(device, iface)
    try:
        start_controller(cfg)
        line = wait_for_log(r"checking SIM card status", timeout=45)
        check("SIM check started", line is not None)

        # Either SIM_READY or SIM_PIN_NEEDED
        line = wait_for_log(r"SIM ready|SIM PIN required|SIM_READY|SIM_PIN_NEEDED", timeout=20)
        check("SIM status determined", line is not None)

        if line and "PIN" in line:
            skip("SIM PIN flow", "SIM requires PIN — set pin= in config to test")
        else:
            check("SIM ready (no PIN)", line is not None and "ready" in line.lower())
    finally:
        stop_controller()
        os.unlink(cfg)


# ── Test 4: Network registration ─────────────────────────────
def test_registration(device, iface):
    print("\n[4] Network registration")
    cfg = write_config(device, iface, reg_timeout_ms=60000)
    try:
        start_controller(cfg)
        line = wait_for_log(r"starting network registration", timeout=50)
        check("registration started", line is not None)

        line = wait_for_log(r"REGISTERED", timeout=60)
        check("network registered", line is not None)
    finally:
        stop_controller()
        os.unlink(cfg)


# ── Test 5: Full connection (IDLE → CONNECTED) ───────────────
def test_full_connection(device, iface):
    print("\n[5] Full connection: IDLE → CONNECTED")
    cfg = write_config(device, iface, modem_info_interval_s=5)
    connected = False
    try:
        start_controller(cfg)

        # Track state progression
        states_seen = []
        expected = [
            ("DEVICE_OPENING", 15),
            ("INITIALIZING", 25),
            ("SIM_AUTH", 50),
            ("REGISTERING", 55),
            ("DATA_CONNECTING", 70),
            ("CONNECTED", 90),
        ]
        for state, timeout in expected:
            line = wait_for_log(rf"transition:.*->\s*{state}", timeout=timeout)
            if line:
                states_seen.append(state)
            check(f"reached {state}", line is not None,
                  f"last seen: {states_seen[-1] if states_seen else 'none'}")
            if line is None:
                break

        connected = "CONNECTED" in states_seen

        if connected:
            check("=== CONNECTED logged", wait_for_log(r"=== CONNECTED ===", timeout=5) is not None)

            # Verify interface got an IP
            time.sleep(2)
            check(f"{iface} has IP address", iface_has_ip(iface))

            # Verify internet connectivity
            reachable = can_ping("8.8.8.8", iface, timeout=5)
            check("internet reachable via modem", reachable)

            # Verify modem info collection runs
            line = wait_for_log(r"Modem Data", timeout=15)
            check("modem info collected", line is not None)

        return connected
    finally:
        if not connected:
            stop_controller()
            os.unlink(cfg)
        # If connected, caller (test_disconnect) will reuse the process
        else:
            # Store config path for later cleanup
            test_full_connection._cfg = cfg


# ── Test 6: Telemetry data quality ───────────────────────────
def test_telemetry(device, iface):
    """Run after test_full_connection if it reached CONNECTED."""
    print("\n[6] Telemetry data quality (from modem info logs)")
    # Collect more logs from the already-running controller
    collect_logs(10)

    # Check signal info was collected
    check("signal strength logged", has_log(r"RSSI:.*dBm"))
    check("registration info logged", has_log(r"Registration:"))
    check("IP settings logged", has_log(r"IP:.*GW:"))
    check("internet check logged", has_log(r"Internet:.*reachable|unreachable"))

    # Check for ping latency (offloaded to thread pool)
    ping_line = get_log(r"Internet:.*reachable")
    if ping_line and "reachable" in ping_line:
        has_latency = "ms)" in ping_line
        check("ping latency measured", has_latency, ping_line)
    else:
        skip("ping latency", "internet not reachable")


# ── Test 7: Disconnect ───────────────────────────────────────
def test_disconnect(device, iface):
    """Send SIGTERM to a connected controller, verify clean shutdown."""
    print("\n[7] Clean disconnect via SIGTERM")
    # Controller should still be running from test_full_connection
    if proc is None or proc.poll() is not None:
        skip("disconnect test", "controller not running")
        return

    # Send SIGTERM
    proc.send_signal(signal.SIGTERM)
    line = wait_for_log(r"signal.*received|shutting down", timeout=10)
    check("shutdown signal received", line is not None)

    # Wait for process exit
    try:
        rc = proc.wait(timeout=15)
        check("process exited", True)
        check("clean exit code (0)", rc == 0, f"exit code: {rc}")
    except subprocess.TimeoutExpired:
        check("process exited", False, "timeout waiting for exit")
        proc.kill()
        proc.wait()

    # Verify interface was deconfigured
    time.sleep(1)
    ip_gone = not iface_has_ip(iface)
    check(f"{iface} IP removed after shutdown", ip_gone)

    # Cleanup config from test_full_connection
    cfg = getattr(test_full_connection, "_cfg", None)
    if cfg and os.path.exists(cfg):
        os.unlink(cfg)


# ── Test 8: Error recovery with backoff ──────────────────────
def test_error_recovery(device, iface):
    print("\n[8] Error recovery — bad device path triggers backoff")
    cfg = write_config("/dev/nonexistent-qmi-device", iface,
                       max_retries=2, backoff_base_ms=500, backoff_max_ms=2000)
    try:
        start_controller(cfg)

        # Should try to open, fail, enter ERROR
        line = wait_for_log(r"entering ERROR state", timeout=15)
        check("entered ERROR state", line is not None)

        # Should see backoff retry
        line = wait_for_log(r"BACKOFF_EXPIRED|soft reset", timeout=10)
        check("backoff retry triggered", line is not None)

        # Should eventually exhaust retries
        line = wait_for_log(r"attempt 2/2|entering ERROR state", timeout=15)
        check("retry count incremented", line is not None)

        # After max retries, should go to IDLE
        line = wait_for_log(r"transition:.*ERROR.*IDLE|BACKOFF_EXPIRED.*IDLE", timeout=15)
        check("returned to IDLE after max retries", line is not None)
    finally:
        stop_controller()
        os.unlink(cfg)


# ── Test 9: Udev auto-detection ──────────────────────────────
def test_udev_detection(device, iface):
    print("\n[9] Udev auto-detection (no device in config)")
    # Write config with empty device path — controller should auto-detect
    cfg = write_config("", iface)
    try:
        start_controller(cfg)

        line = wait_for_log(r"udev monitor started", timeout=10)
        check("udev monitor started", line is not None)

        # Should detect the existing modem
        line = wait_for_log(r"QMI device (detected|found):", timeout=15)
        check("modem auto-detected", line is not None)

        if line:
            # Verify it found the right device
            check("detected correct device", device.split("/")[-1] in line, line)

        # Should start opening the device
        line = wait_for_log(r"DEVICE_OPENING", timeout=10)
        check("auto-started connection", line is not None)
    finally:
        stop_controller()
        os.unlink(cfg)


# ── Test 10: SIGINT handling ─────────────────────────────────
def test_sigint(device, iface):
    print("\n[10] SIGINT handling")
    cfg = write_config(device, iface)
    try:
        start_controller(cfg)
        # Wait for it to start doing something
        wait_for_log(r"DEVICE_OPENING|udev monitor started", timeout=15)

        proc.send_signal(signal.SIGINT)
        try:
            rc = proc.wait(timeout=10)
            check("exited on SIGINT", True)
            check("clean exit code", rc == 0, f"exit code: {rc}")
        except subprocess.TimeoutExpired:
            check("exited on SIGINT", False, "timeout")
            proc.kill()
            proc.wait()
    finally:
        os.unlink(cfg)


# ── Test 11: Config file generation ──────────────────────────
def test_config_generation(device, iface):
    print("\n[11] Config file auto-generation")
    cfg_path = tempfile.mktemp(suffix=".conf")
    # Don't create the file — controller should generate it
    try:
        # Start with non-existent config, let udev detect device
        p = subprocess.Popen(
            [BINARY, cfg_path],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        time.sleep(8)  # Give time for udev scan + config generation
        p.send_signal(signal.SIGTERM)
        try:
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()

        generated = os.path.exists(cfg_path)
        check("config file generated", generated)

        if generated:
            with open(cfg_path) as f:
                content = f.read()
            check("has [device] section", "[device]" in content)
            check("has [recovery] section", "[recovery]" in content)
            check("has [timeouts] section", "[timeouts]" in content)
            check("has backoff_jitter_ms", "backoff_jitter_ms" in content)
            check("has open_timeout_ms", "open_timeout_ms" in content)
    finally:
        if os.path.exists(cfg_path):
            os.unlink(cfg_path)


# ── Test 12: Repeated connect/disconnect cycles ──────────────
def test_reconnect_cycle(device, iface):
    print("\n[12] Reconnect cycle stability")
    cfg = write_config(device, iface, modem_info_interval_s=0)
    try:
        start_controller(cfg)

        # Wait for first CONNECTED
        line = wait_for_log(r"=== CONNECTED ===", timeout=90)
        if not line:
            skip("reconnect cycle", "never reached CONNECTED")
            return

        check("first connection established", True)

        # Verify IP
        time.sleep(1)
        check("IP assigned (cycle 1)", iface_has_ip(iface))

        # The controller doesn't expose a disconnect command without Kafka,
        # so we test by killing and restarting
        stop_controller()
        time.sleep(2)

        # Verify cleanup
        check("IP removed after stop", not iface_has_ip(iface))

        # Restart — should reconnect
        start_controller(cfg)
        line = wait_for_log(r"=== CONNECTED ===", timeout=90)
        check("reconnected after restart", line is not None)

        if line:
            time.sleep(1)
            check("IP assigned (cycle 2)", iface_has_ip(iface))
    finally:
        stop_controller()
        os.unlink(cfg)


# ── Main ─────────────────────────────────────────────────────
def main():
    print("=" * 60)
    print("  ultima-qmi-controller — Real Device Integration Tests")
    print("=" * 60)

    # Determine device
    device = sys.argv[1] if len(sys.argv) > 1 else find_qmi_device()
    if not device:
        print("\nERROR: No QMI device found. Pass device path as argument.")
        print("Usage: sudo python3 test_qmi_device.py [/dev/cdc-wdmX]")
        sys.exit(1)

    iface = find_wwan_iface(device)
    print(f"\nDevice: {device}")
    print(f"Interface: {iface}")

    if not test_prerequisites(device, iface):
        print("\nPrerequisites not met. Aborting.")
        sys.exit(1)

    # Tests that don't need a full connection
    test_error_recovery(device, iface)
    test_udev_detection(device, iface)
    test_sigint(device, iface)
    test_config_generation(device, iface)

    # Progressive connection tests
    test_device_open(device, iface)
    test_hw_info(device, iface)
    test_sim_auth(device, iface)
    test_registration(device, iface)

    # Full connection + dependent tests
    connected = test_full_connection(device, iface)
    if connected:
        test_telemetry(device, iface)
        test_disconnect(device, iface)
    else:
        skip("telemetry test", "never reached CONNECTED")
        skip("disconnect test", "never reached CONNECTED")

    # Reconnect stability (independent full cycle)
    test_reconnect_cycle(device, iface)

    # Summary
    print(f"\n{'=' * 60}")
    print(f"  Results: {passed} passed, {failed} failed, {skipped} skipped")
    print(f"  Total:   {passed + failed + skipped}")
    print(f"{'=' * 60}")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
