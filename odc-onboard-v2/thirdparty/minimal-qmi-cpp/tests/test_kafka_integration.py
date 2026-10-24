#!/usr/bin/env python3
"""Integration tests for minimal-qmi-cpp Kafka communication layer.

Starts minimal-redpanda-cpp broker and ultima-qmi-controller as subprocesses,
then exercises all Kafka operations: GET_STATUS, GET_METRICS, CONNECT, DISCONNECT,
and verifies the qmi-events stream.
"""

import os, sys, time, signal, subprocess, tempfile, atexit
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qmi_agent_pb2 as pb
from kafka import KafkaProducer, KafkaConsumer
from kafka.structs import TopicPartition

# ── Paths ────────────────────────────────────────────────────
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BROKER_BIN    = os.path.join(ROOT, "../minimal-redpanda-cpp/build/minimal_broker")
CONTROLLER_BIN= os.path.join(ROOT, "build/ultima-qmi-controller")
BROKER        = "localhost:19099"  # use non-default port to avoid conflicts
API_VER       = (2, 0, 0)

EVENTS_TOPIC    = "qmi-events"
TELEMETRY_TOPIC = "qmi-telemetry"
COMMANDS_TOPIC  = "qmi-commands"
RESPONSES_TOPIC = "qmi-responses"

# ── Test state ───────────────────────────────────────────────
passed = failed = 0
procs = []

def check(name, cond, detail=""):
    global passed, failed
    if cond:
        print(f"  ✓ {name}"); passed += 1
    else:
        print(f"  ✗ {name}" + (f" — {detail}" if detail else "")); failed += 1

def cleanup():
    for p in procs:
        try:
            p.send_signal(signal.SIGTERM)
            p.wait(timeout=3)
        except Exception:
            try: p.kill()
            except Exception: pass

atexit.register(cleanup)

# ── Kafka helpers ────────────────────────────────────────────
def make_producer():
    for _ in range(20):
        try:
            p = KafkaProducer(bootstrap_servers=BROKER, api_version=API_VER)
            return p
        except Exception:
            time.sleep(0.5)
    raise RuntimeError("broker not reachable")

def make_consumer(topic):
    c = KafkaConsumer(
        bootstrap_servers=BROKER, api_version=API_VER,
        check_crcs=False, enable_auto_commit=False, group_id=None,
    )
    tp = TopicPartition(topic, 0)
    c.assign([tp])
    c.seek_to_end(tp)
    return c

def make_consumer_from_start(topic):
    c = KafkaConsumer(
        bootstrap_servers=BROKER, api_version=API_VER,
        check_crcs=False, enable_auto_commit=False, group_id=None,
    )
    tp = TopicPartition(topic, 0)
    c.assign([tp])
    c.seek_to_beginning(tp)
    return c

def drain(consumer, timeout=3):
    msgs, deadline = [], time.time() + timeout
    while time.time() < deadline:
        batch = consumer.poll(timeout_ms=400, max_records=50)
        for records in batch.values():
            for r in records:
                if r.value:
                    msgs.append(r.value)
        if msgs:
            break
    return msgs

def send_cmd(producer, **kwargs):
    cmd = pb.QmiCommand(**kwargs)
    producer.send(COMMANDS_TOPIC, value=cmd.SerializeToString()).get(timeout=5)
    producer.flush()

# ── Main ─────────────────────────────────────────────────────
def main():
    global passed, failed

    print("=== minimal-qmi-cpp Kafka integration test ===\n")

    # ── Start broker ─────────────────────────────────────────
    print("[setup] Starting minimal-redpanda-cpp broker on port 19099...")
    broker_proc = subprocess.Popen(
        [BROKER_BIN, "--port", "19099"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )
    procs.append(broker_proc)
    time.sleep(1)
    check("broker started", broker_proc.poll() is None)

    # ── Write controller config ───────────────────────────────
    cfg_content = f"""[device]
path = /dev/nonexistent-qmi-device
interface = wwan0

[connection]
apn =
ip_type = 4
auth = none
auto_connect = false

[sim]
pin =

[recovery]
max_retries = 3
backoff_base_ms = 500
backoff_max_ms = 2000
circuit_breaker_ms = 99999999

[watchdog]
interval_s = 30
ping_target = 8.8.8.8
modem_info_interval_s = 0

[kafka]
enabled = true
broker = {BROKER}
client_id = ultima-qmi-test
events_topic = {EVENTS_TOPIC}
telemetry_topic = {TELEMETRY_TOPIC}
commands_topic = {COMMANDS_TOPIC}
responses_topic = {RESPONSES_TOPIC}
"""
    cfg_file = tempfile.NamedTemporaryFile(mode='w', suffix='.conf', delete=False)
    cfg_file.write(cfg_content)
    cfg_file.close()

    # ── Start controller ──────────────────────────────────────
    print("[setup] Starting ultima-qmi-controller...")
    ctrl_proc = subprocess.Popen(
        [CONTROLLER_BIN, cfg_file.name],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    procs.append(ctrl_proc)
    time.sleep(2)
    check("controller started", ctrl_proc.poll() is None)

    # ── Connect producer + consumers ──────────────────────────
    print("[setup] Connecting Kafka clients...")
    producer = make_producer()

    # Bootstrap all topics so consumers can seek_to_end
    for t in [EVENTS_TOPIC, TELEMETRY_TOPIC, COMMANDS_TOPIC, RESPONSES_TOPIC]:
        producer.send(t, value=b"").get(timeout=5)
    producer.flush()
    time.sleep(0.5)

    events_c    = make_consumer(EVENTS_TOPIC)
    telemetry_c = make_consumer(TELEMETRY_TOPIC)
    responses_c = make_consumer(RESPONSES_TOPIC)
    time.sleep(0.5)
    # Drain any bootstrap noise
    drain(events_c, 0.5); drain(telemetry_c, 0.5); drain(responses_c, 0.5)
    print("  Connected.\n")

    # ── Test 1: GET_STATUS ────────────────────────────────────
    print("[1] GET_STATUS — expect IDLE response")
    send_cmd(producer, action=pb.QmiCommand.GET_STATUS, correlation_id="corr-1")
    time.sleep(1)
    msgs = drain(responses_c, timeout=4)
    check("response received", len(msgs) > 0, f"got {len(msgs)}")
    if msgs:
        resp = pb.QmiResponse()
        resp.ParseFromString(msgs[-1])
        check("correlation_id echoed", resp.correlation_id == "corr-1", f"'{resp.correlation_id}'")
        check("ok=true", resp.ok, f"ok={resp.ok}")
        check("state=IDLE", resp.state == "IDLE", f"state='{resp.state}'")
        check("has timestamp", resp.timestamp_ms > 0, f"ts={resp.timestamp_ms}")

    # ── Test 2: GET_METRICS ───────────────────────────────────
    print("\n[2] GET_METRICS — expect telemetry + response")
    drain(telemetry_c, 0.5); drain(responses_c, 0.5)
    send_cmd(producer, action=pb.QmiCommand.GET_METRICS, correlation_id="corr-2")
    time.sleep(1)
    tel_msgs  = drain(telemetry_c, timeout=4)
    resp_msgs = drain(responses_c, timeout=4)
    check("telemetry published", len(tel_msgs) > 0, f"got {len(tel_msgs)}")
    check("response received",   len(resp_msgs) > 0, f"got {len(resp_msgs)}")
    if tel_msgs:
        tel = pb.QmiTelemetry()
        tel.ParseFromString(tel_msgs[-1])
        check("telemetry has timestamp", tel.timestamp_ms > 0, f"ts={tel.timestamp_ms}")
    if resp_msgs:
        resp = pb.QmiResponse()
        resp.ParseFromString(resp_msgs[-1])
        check("corr-2 echoed", resp.correlation_id == "corr-2", f"'{resp.correlation_id}'")
        check("ok=true", resp.ok)

    # ── Test 3: CONNECT — triggers state machine ──────────────
    print("\n[3] CONNECT — expect state events (DEVICE_OPENING → ERROR since no modem)")
    drain(events_c, 0.5); drain(responses_c, 0.5)
    send_cmd(producer, action=pb.QmiCommand.CONNECT, correlation_id="corr-3")
    time.sleep(1)
    resp_msgs = drain(responses_c, timeout=4)
    check("connect response received", len(resp_msgs) > 0, f"got {len(resp_msgs)}")
    if resp_msgs:
        resp = pb.QmiResponse()
        resp.ParseFromString(resp_msgs[-1])
        check("corr-3 echoed", resp.correlation_id == "corr-3", f"'{resp.correlation_id}'")
        check("ok=true", resp.ok)

    # Wait for state machine to cycle (IDLE→DEVICE_OPENING→ERROR→backoff→...)
    time.sleep(3)
    ev_msgs = drain(events_c, timeout=4)
    check("state events published", len(ev_msgs) > 0, f"got {len(ev_msgs)}")
    if ev_msgs:
        states_seen = []
        for raw in ev_msgs:
            try:
                ev = pb.QmiStateEvent()
                ev.ParseFromString(raw)
                states_seen.append(ev.state)
                check(f"event has timestamp ({ev.state})", ev.timestamp_ms > 0)
            except Exception as e:
                check("event parse", False, str(e))
        print(f"    states seen: {states_seen}")
        check("ERROR state published", "ERROR" in states_seen, str(states_seen))

    # ── Test 4: DISCONNECT ────────────────────────────────────
    print("\n[4] DISCONNECT — expect response (DISCONNECT_REQ injected)")
    drain(responses_c, 0.5)
    send_cmd(producer, action=pb.QmiCommand.DISCONNECT, correlation_id="corr-4")
    time.sleep(1)
    msgs = drain(responses_c, timeout=4)
    check("disconnect response received", len(msgs) > 0, f"got {len(msgs)}")
    if msgs:
        resp = pb.QmiResponse()
        resp.ParseFromString(msgs[-1])
        check("corr-4 echoed", resp.correlation_id == "corr-4", f"'{resp.correlation_id}'")
        check("ok=true", resp.ok)

    # ── Test 5: Event stream integrity ───────────────────────
    print("\n[5] Event stream — verify QmiStateEvent fields")
    # Read all events published so far (from beginning) to verify field integrity.
    # The ERROR event from test 3's CONNECT is already in qmi-events.
    ev_all = make_consumer_from_start(EVENTS_TOPIC)
    time.sleep(0.5)
    ev_msgs = drain(ev_all, timeout=5)
    ev_all.close()
    check("events received", len(ev_msgs) > 0, f"got {len(ev_msgs)}")
    for raw in ev_msgs:
        try:
            ev = pb.QmiStateEvent()
            ev.ParseFromString(raw)
            if not ev.state:
                continue
            check(f"state non-empty ({ev.state})", len(ev.state) > 0)
            check(f"timestamp > 0 ({ev.state})", ev.timestamp_ms > 0)
            break
        except Exception as e:
            check("event parse", False, str(e))

    # ── Test 6: Unknown command ───────────────────────────────
    print("\n[6] Unknown action — expect ok=false response")
    drain(responses_c, 0.5)
    cmd = pb.QmiCommand()
    cmd.action = 99  # invalid
    cmd.correlation_id = "corr-6"
    producer.send(COMMANDS_TOPIC, value=cmd.SerializeToString()).get(timeout=5)
    producer.flush()
    time.sleep(1)
    msgs = drain(responses_c, timeout=4)
    check("response received", len(msgs) > 0, f"got {len(msgs)}")
    if msgs:
        resp = pb.QmiResponse()
        resp.ParseFromString(msgs[-1])
        check("corr-6 echoed", resp.correlation_id == "corr-6", f"'{resp.correlation_id}'")
        check("ok=false for unknown action", not resp.ok, f"ok={resp.ok}")

    # ── Summary ───────────────────────────────────────────────
    print(f"\n{'='*50}")
    print(f"Results: {passed} passed, {failed} failed, {passed+failed} total")
    print(f"{'='*50}")

    producer.close()
    events_c.close(); telemetry_c.close(); responses_c.close()
    os.unlink(cfg_file.name)
    sys.exit(0 if failed == 0 else 1)

if __name__ == "__main__":
    main()
