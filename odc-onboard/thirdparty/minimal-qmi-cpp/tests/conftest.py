"""Shared fixtures for ultima-qmi-controller test suite."""
import os
import subprocess
import time
import json
import re
import pytest
from pathlib import Path

TESTS_DIR = Path(__file__).parent
ECOSYSTEM = TESTS_DIR / "ecosystem.config.js"
LOG_DIR = TESTS_DIR / "logs"
ERR_LOG = LOG_DIR / "qmi-agent-err.log"
OUT_LOG = LOG_DIR / "qmi-agent-out.log"
BINARY = TESTS_DIR.parent / "build" / "ultima-qmi-controller"
PM2_APP = "qmi-agent"


def _run(cmd: str, check=True, timeout=15) -> subprocess.CompletedProcess:
    return subprocess.run(
        cmd, shell=True, capture_output=True, text=True,
        check=check, timeout=timeout,
    )


def pm2_jlist() -> list[dict]:
    r = _run("pm2 jlist", check=False)
    try:
        return json.loads(r.stdout)
    except json.JSONDecodeError:
        return []


def pm2_app_info() -> dict | None:
    for app in pm2_jlist():
        if app.get("name") == PM2_APP:
            return app
    return None


# ── Fixtures ──────────────────────────────────────────────────


@pytest.fixture(scope="session", autouse=True)
def ensure_binary():
    """Fail fast if the binary hasn't been built."""
    assert BINARY.exists(), (
        f"Binary not found at {BINARY}. Run `cmake --build build` first."
    )


@pytest.fixture(scope="session", autouse=True)
def ensure_pm2():
    """Fail fast if pm2 is not installed."""
    r = _run("pm2 --version", check=False)
    assert r.returncode == 0, "pm2 is not installed. Run `npm i -g pm2`."


@pytest.fixture()
def clean_logs():
    """Wipe log files before a test, return helpers to read them."""
    LOG_DIR.mkdir(exist_ok=True)
    for f in (ERR_LOG, OUT_LOG):
        f.write_text("")

    class Logs:
        @staticmethod
        def stderr() -> str:
            return ERR_LOG.read_text() if ERR_LOG.exists() else ""

        @staticmethod
        def stdout() -> str:
            return OUT_LOG.read_text() if OUT_LOG.exists() else ""

        @staticmethod
        def combined() -> str:
            return Logs.stderr() + Logs.stdout()

        @staticmethod
        def wait_for(pattern: str, timeout: float = 30) -> str | None:
            """Poll logs until `pattern` appears or timeout."""
            deadline = time.monotonic() + timeout
            regex = re.compile(pattern)
            while time.monotonic() < deadline:
                text = Logs.combined()
                m = regex.search(text)
                if m:
                    return m.group(0)
                time.sleep(0.3)
            return None

        @staticmethod
        def lines() -> list[str]:
            return [l for l in Logs.combined().splitlines() if l.strip()]

        @staticmethod
        def errors() -> list[str]:
            return [l for l in Logs.lines() if "[ERR]" in l]

        @staticmethod
        def warnings() -> list[str]:
            return [l for l in Logs.lines() if "[WRN]" in l]

    return Logs()


@pytest.fixture()
def qmi_agent(clean_logs):
    """Start qmi-agent via pm2, yield log helpers, stop on teardown."""
    _run(f"pm2 start {ECOSYSTEM}", check=True)
    time.sleep(1)  # let the process initialise
    yield clean_logs
    _run(f"pm2 stop {PM2_APP}", check=False)
    _run(f"pm2 delete {PM2_APP}", check=False)


@pytest.fixture()
def stopped_agent():
    """Ensure qmi-agent is NOT running (cleanup from prior tests)."""
    _run(f"pm2 stop {PM2_APP}", check=False)
    _run(f"pm2 delete {PM2_APP}", check=False)
    yield
