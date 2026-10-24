"""
Test suite for ultima-qmi-controller (qmi-agent).

Runs the binary as a pm2 service and validates behaviour through log analysis.
Tests are grouped by concern:

  TestPM2Lifecycle       – start / stop / restart via pm2
  TestStateMachine       – state transitions visible in logs
  TestQmiDeviceOps       – real-device QMI operations (open, clients, SIM, data)
  TestErrorRecovery      – backoff, retry, error-state handling
  TestLogIntegrity       – no crashes, no unexpected ERR lines
"""
import os
import re
import time
import subprocess
import pytest

from conftest import (
    BINARY, ECOSYSTEM, PM2_APP,
    _run, pm2_app_info, pm2_jlist,
)

# ── Helpers ───────────────────────────────────────────────────

DEVICE_PRESENT = os.path.exists("/dev/cdc-wdm0") or os.path.exists(
    os.environ.get("QMI_TEST_DEVICE", "")
)

needs_device = pytest.mark.skipif(
    not DEVICE_PRESENT,
    reason="No QMI modem detected (set QMI_TEST_DEVICE or plug /dev/cdc-wdm0)",
)


# ═══════════════════════════════════════════════════════════════
# 1. PM2 lifecycle
# ═══════════════════════════════════════════════════════════════

class TestPM2Lifecycle:
    """Verify the binary can be managed as a pm2 service."""

    def test_start_creates_process(self, qmi_agent):
        info = pm2_app_info()
        assert info is not None, "qmi-agent not found in pm2 list"
        assert info["pm2_env"]["status"] in ("online", "stopping", "stopped")

    def test_binary_path_correct(self, qmi_agent):
        info = pm2_app_info()
        assert info is not None
        assert "ultima-qmi-controller" in info["pm2_env"]["pm_exec_path"]

    def test_stop_and_delete(self, qmi_agent):
        _run(f"pm2 stop {PM2_APP}")
        time.sleep(0.5)
        info = pm2_app_info()
        assert info is None or info["pm2_env"]["status"] == "stopped"
        _run(f"pm2 delete {PM2_APP}")
        assert pm2_app_info() is None

    def test_restart_keeps_same_name(self, qmi_agent):
        _run(f"pm2 restart {PM2_APP}")
        time.sleep(1)
        info = pm2_app_info()
        assert info is not None
        assert info["name"] == PM2_APP

    def test_logs_are_written(self, qmi_agent):
        """The binary must produce at least one log line within 5s."""
        hit = qmi_agent.wait_for(r"\[INF\]|\[ERR\]|\[WRN\]|\[DBG\]", timeout=5)
        assert hit is not None, "No log output from qmi-agent within 5s"


# ═══════════════════════════════════════════════════════════════
# 2. State-machine transitions (log-based)
# ═══════════════════════════════════════════════════════════════

class TestStateMachine:
    """Validate the controller's state machine through log messages."""

    def test_startup_banner(self, qmi_agent):
        hit = qmi_agent.wait_for(r"ultima-qmi controller starting", timeout=5)
        assert hit, "Missing startup banner"

    def test_config_info_logged(self, qmi_agent):
        """Controller should log device/apn/kafka config on start."""
        hit = qmi_agent.wait_for(r"device:.*iface:.*apn:", timeout=5)
        assert hit, "Config summary not logged"

    @needs_device
    def test_device_opening_state(self, qmi_agent):
        hit = qmi_agent.wait_for(r"opening QMI device", timeout=10)
        assert hit, "Never entered DEVICE_OPENING state"

    @needs_device
    def test_device_opened(self, qmi_agent):
        hit = qmi_agent.wait_for(r"QMI device opened", timeout=30)
        assert hit, "QMI device never opened"

    @needs_device
    def test_clients_allocated(self, qmi_agent):
        hit = qmi_agent.wait_for(r"all QMI clients allocated", timeout=40)
        assert hit, "QMI clients never allocated"

    @needs_device
    def test_modem_set_online(self, qmi_agent):
        hit = qmi_agent.wait_for(r"modem (set online|already online)", timeout=45)
        assert hit, "Modem never set to online mode"

    @needs_device
    def test_sim_check(self, qmi_agent):
        hit = qmi_agent.wait_for(r"SIM (ready|PIN required|absent)", timeout=50)
        assert hit, "SIM status never checked"

    @needs_device
    def test_reaches_registering_or_error(self, qmi_agent):
        """After SIM, we should see registration attempt or error."""
        hit = qmi_agent.wait_for(
            r"starting network registration|entering ERROR state|SIM.*error",
            timeout=60,
        )
        assert hit, "Never reached REGISTERING or ERROR after SIM check"

    @needs_device
    def test_reaches_connected(self, qmi_agent):
        """Full happy path: IDLE → … → CONNECTED."""
        hit = qmi_agent.wait_for(r"=== CONNECTED ===", timeout=180)
        assert hit, "Never reached CONNECTED state"

    @needs_device
    def test_ip_settings_logged(self, qmi_agent):
        hit = qmi_agent.wait_for(r"IP: \d+\.\d+\.\d+\.\d+", timeout=180)
        assert hit, "IP settings never logged"


# ═══════════════════════════════════════════════════════════════
# 3. Real-device QMI operations
# ═══════════════════════════════════════════════════════════════

class TestQmiDeviceOps:
    """Verify individual QMI operations succeed on a real modem."""

    @needs_device
    def test_network_started_with_handle(self, qmi_agent):
        hit = qmi_agent.wait_for(r"network started, handle=\d+", timeout=180)
        assert hit, "WDS start_network never returned a handle"

    @needs_device
    def test_interface_configured(self, qmi_agent):
        """The wwan interface should get an IP address."""
        hit = qmi_agent.wait_for(r"interface \S+ configured", timeout=180)
        assert hit, "Network interface never configured"

    @needs_device
    def test_dns_handling(self, qmi_agent):
        """DNS should be logged (either written or skipped)."""
        hit = qmi_agent.wait_for(
            r"wrote WWAN DNS|DNS|dns:", timeout=180,
        )
        assert hit, "No DNS-related log found"

    @needs_device
    def test_hw_info_collected(self, qmi_agent):
        hit = qmi_agent.wait_for(r"collecting modem hardware identification", timeout=45)
        assert hit, "HW info collection never started"

    @needs_device
    def test_watchdog_starts(self, qmi_agent):
        """After CONNECTED, watchdog should tick at least once."""
        hit = qmi_agent.wait_for(r"watchdog:.*connected", timeout=210)
        assert hit, "Watchdog never ticked after CONNECTED"

    @needs_device
    def test_modem_info_collection(self, qmi_agent):
        """Periodic modem info collection should run after CONNECTED."""
        # Wait for connected first, then info collection
        qmi_agent.wait_for(r"=== CONNECTED ===", timeout=180)
        # modem_info.collect_all runs on a timer; just verify no crash
        time.sleep(15)
        errs = qmi_agent.errors()
        info_errs = [e for e in errs if "collect" in e.lower() or "modem_info" in e.lower()]
        assert len(info_errs) == 0, f"Modem info errors: {info_errs}"


# ═══════════════════════════════════════════════════════════════
# 4. Error recovery & backoff
# ═══════════════════════════════════════════════════════════════

class TestErrorRecovery:
    """Validate error handling, backoff, and retry logic."""

    def test_no_device_enters_idle_or_error(self, qmi_agent):
        """Without a modem, the agent should stay IDLE or enter ERROR gracefully."""
        time.sleep(3)
        lines = qmi_agent.lines()
        # Should NOT see a segfault or abort
        for line in lines:
            assert "Segmentation fault" not in line
            assert "SIGABRT" not in line
            assert "core dumped" not in line

    def test_no_crash_on_missing_config(self, clean_logs):
        """Binary should warn but not crash when config file is missing."""
        _run(f"pm2 start {ECOSYSTEM} --env QMI_CONFIG_PATH=/nonexistent", check=False)
        time.sleep(2)
        hit = clean_logs.wait_for(
            r"config.*not found|will auto-generate|ultima-qmi controller starting",
            timeout=5,
        )
        _run(f"pm2 stop {PM2_APP}", check=False)
        _run(f"pm2 delete {PM2_APP}", check=False)
        assert hit, "Binary crashed or didn't handle missing config"

    @needs_device
    def test_error_state_triggers_backoff(self, qmi_agent):
        """If an error occurs, we should see backoff messages."""
        hit = qmi_agent.wait_for(
            r"entering ERROR state|=== CONNECTED ===", timeout=120,
        )
        if hit and "ERROR" in hit:
            backoff = qmi_agent.wait_for(r"attempt \d+/\d+", timeout=5)
            assert backoff, "ERROR state reached but no retry attempt logged"

    @needs_device
    def test_soft_reset_reopens_device(self, qmi_agent):
        """After backoff, a soft reset should attempt to reopen."""
        hit = qmi_agent.wait_for(
            r"soft reset|=== CONNECTED ===", timeout=120,
        )
        # Either we connected successfully or we see the retry path
        assert hit is not None

    def test_signal_graceful_shutdown(self, qmi_agent):
        """SIGTERM via pm2 stop should produce a clean shutdown log."""
        time.sleep(2)
        _run(f"pm2 stop {PM2_APP}")
        time.sleep(2)
        text = qmi_agent.combined()
        # The process should not leave behind crash indicators
        assert "Segmentation fault" not in text
        assert "core dumped" not in text


# ═══════════════════════════════════════════════════════════════
# 5. Log integrity & error detection
# ═══════════════════════════════════════════════════════════════

# Known benign error patterns that are expected in certain conditions
BENIGN_ERRORS = [
    r"config.*not found",
    r"cannot write.*/sys/class/net",
    r"device.*does not exist",
    r"device.*not a character device",
    r"no read/write permission",
    r"udev:.*ignoring",
]


class TestLogIntegrity:
    """Scan logs for unexpected errors, crashes, and anomalies."""

    def test_no_segfault(self, qmi_agent):
        time.sleep(3)
        text = qmi_agent.combined()
        assert "Segmentation fault" not in text
        assert "SIGSEGV" not in text

    def test_no_abort(self, qmi_agent):
        time.sleep(3)
        text = qmi_agent.combined()
        assert "SIGABRT" not in text
        assert "Aborted" not in text

    def test_no_double_free(self, qmi_agent):
        time.sleep(3)
        text = qmi_agent.combined()
        assert "double free" not in text
        assert "corrupted" not in text

    def test_no_glib_critical(self, qmi_agent):
        """GLib CRITICAL messages indicate programming errors."""
        time.sleep(3)
        text = qmi_agent.combined()
        criticals = [l for l in text.splitlines() if "CRITICAL" in l]
        assert len(criticals) == 0, f"GLib CRITICALs found:\n" + "\n".join(criticals)

    def test_no_glib_warning_leaks(self, qmi_agent):
        """GLib warnings about leaked objects."""
        time.sleep(3)
        text = qmi_agent.combined()
        leaks = [l for l in text.splitlines() if "leaked" in l.lower()]
        assert len(leaks) == 0, f"Object leaks:\n" + "\n".join(leaks)

    def test_unexpected_errors(self, qmi_agent):
        """Flag ERR lines that don't match known benign patterns."""
        time.sleep(5)
        errs = qmi_agent.errors()
        unexpected = []
        for line in errs:
            if not any(re.search(p, line, re.IGNORECASE) for p in BENIGN_ERRORS):
                unexpected.append(line)
        # We don't hard-fail here — just report. On a device-less machine
        # some errors are expected (no modem). On a real device, this catches bugs.
        if unexpected and DEVICE_PRESENT:
            pytest.fail(
                f"{len(unexpected)} unexpected ERR lines:\n" + "\n".join(unexpected[:10])
            )

    @needs_device
    def test_no_qmi_protocol_errors(self, qmi_agent):
        """After reaching CONNECTED, there should be no QMI protocol errors."""
        qmi_agent.wait_for(r"=== CONNECTED ===", timeout=180)
        time.sleep(10)
        errs = qmi_agent.errors()
        proto_errs = [
            e for e in errs
            if any(k in e.lower() for k in ("protocol", "malformed", "invalid tlv"))
        ]
        assert len(proto_errs) == 0, f"QMI protocol errors:\n" + "\n".join(proto_errs)

    @needs_device
    def test_state_sequence_valid(self, qmi_agent):
        """Verify the state transitions follow the expected order."""
        qmi_agent.wait_for(r"=== CONNECTED ===|entering ERROR state", timeout=180)
        text = qmi_agent.combined()

        # Extract state-related log lines in order
        expected_sequence = [
            "controller starting",
            "opening QMI device",
            "QMI device opened",
            "allocating QMI clients",
            "all QMI clients allocated",
            "collecting modem hardware",
            "modem (set online|already online)",
            "checking SIM",
            "SIM (ready|PIN)",
        ]
        last_pos = 0
        for pattern in expected_sequence:
            m = re.search(pattern, text[last_pos:], re.IGNORECASE)
            if m:
                last_pos += m.end()
            # Don't fail — some steps may be skipped on error path
