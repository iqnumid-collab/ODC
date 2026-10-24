#!/bin/bash
# odc-root Integration Test Framework — helpers.sh
# Shared assertion functions and reporting mechanism.

export REPORT_DIR="${REPORT_DIR:-/output}"
export REPORT_FILE="${REPORT_DIR}/test_report.json"
export LOG_DIR="${REPORT_DIR}/logs"
export CERTS_DIR="${REPORT_DIR}/certs"

mkdir -p "$REPORT_DIR" "$LOG_DIR" "$CERTS_DIR"

# Global counters
PASS=0
FAIL=0
SKIP=0
TOTAL=0
START_TIME=$(date +%s)

# Problem tracking arrays
declare -a PROBLEM_SOURCES=()
declare -a PROBLEM_DETAILS=()
declare -a TEST_RESULTS=()

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m'

section_count=0

# ── Assertion helpers ──────────────────────────────────────────────────────

pass() {
    echo -e "  ${GREEN}PASS${NC}: $1"
    ((PASS++)) || true
    ((TOTAL++)) || true
    TEST_RESULTS+=("PASS")
    PROBLEM_SOURCES+=("")
    PROBLEM_DETAILS+=("")
}

fail() {
    echo -e "  ${RED}FAIL${NC}: $1"
    ((FAIL++)) || true
    ((TOTAL++)) || true
    TEST_RESULTS+=("FAIL")
    PROBLEM_SOURCES+=("$2")
    local detail="${3:-$1}"
    PROBLEM_DETAILS+=("$detail")
}

skip() {
    echo -e "  ${YELLOW}SKIP${NC}: $1 (reason: ${2:-condition not met})"
    ((SKIP++)) || true
    ((TOTAL++)) || true
    TEST_RESULTS+=("SKIP")
    PROBLEM_SOURCES+=("$2")
    PROBLEM_DETAILS+=("${3:-condition not met}")
}

test_section() {
    ((section_count++)) || true
    echo
    echo -e "${BLUE}═══ Section ${section_count}: $1 ═══${NC}"
    echo "$(date -Iseconds) | SECTION | $1" >> "$LOG_DIR/test_run.log"
}

# ── Value assertions ───────────────────────────────────────────────────────

assert() {
    ((TOTAL++)) || true
    if eval "$1" 2>/dev/null; then
        echo -e "  ${GREEN}PASS${NC}: $2"
        ((PASS++)) || true
        TEST_RESULTS+=("PASS")
        PROBLEM_SOURCES+=("")
        PROBLEM_DETAILS+=("")
    else
        echo -e "  ${RED}FAIL${NC}: $2 (expected: true, got: false)"
        ((FAIL++)) || true
        TEST_RESULTS+=("FAIL")
        PROBLEM_SOURCES+=("$3")
        PROBLEM_DETAILS+=("${4:-$2}")
    fi
}

assert_eq() {
    ((TOTAL++)) || true
    local actual
    actual=$(eval "$1" 2>/dev/null)
    if [ "$actual" = "$2" ]; then
        echo -e "  ${GREEN}PASS${NC}: $3"
        ((PASS++)) || true
        TEST_RESULTS+=("PASS")
        PROBLEM_SOURCES+=("")
        PROBLEM_DETAILS+=("")
    else
        echo -e "  ${RED}FAIL${NC}: $3 (expected: '$2', got: '$actual')"
        ((FAIL++)) || true
        TEST_RESULTS+=("FAIL")
        PROBLEM_SOURCES+=("$4")
        PROBLEM_DETAILS+=("${5:-expected '$2', got '$actual'}")
    fi
}

assert_contains() {
    ((TOTAL++)) || true
    local haystack
    haystack=$(eval "$2" 2>/dev/null)
    if echo "$haystack" | grep -qF "$1" 2>/dev/null; then
        echo -e "  ${GREEN}PASS${NC}: $3"
        ((PASS++)) || true
        TEST_RESULTS+=("PASS")
        PROBLEM_SOURCES+=("")
        PROBLEM_DETAILS+=("")
    else
        echo -e "  ${RED}FAIL${NC}: $3 (expected to contain: '$1')"
        ((FAIL++)) || true
        TEST_RESULTS+=("FAIL")
        PROBLEM_SOURCES+=("$4")
        PROBLEM_DETAILS+=("${5:-expected to contain '$1'}")
    fi
}

assert_file_exists() {
    ((TOTAL++)) || true
    if [ -f "$1" ]; then
        echo -e "  ${GREEN}PASS${NC}: $2"
        ((PASS++)) || true
        TEST_RESULTS+=("PASS")
        PROBLEM_SOURCES+=("")
        PROBLEM_DETAILS+=("")
    else
        echo -e "  ${RED}FAIL${NC}: $2 (file not found: $1)"
        ((FAIL++)) || true
        TEST_RESULTS+=("FAIL")
        PROBLEM_SOURCES+=("$3")
        PROBLEM_DETAILS+=("${4:-file not found: $1}")
    fi
}

assert_port_open() {
    ((TOTAL++)) || true
    if timeout 3 bash -c "echo >/dev/tcp/127.0.0.1/$1" 2>/dev/null; then
        echo -e "  ${GREEN}PASS${NC}: $2"
        ((PASS++)) || true
        TEST_RESULTS+=("PASS")
        PROBLEM_SOURCES+=("")
        PROBLEM_DETAILS+=("")
    else
        echo -e "  ${RED}FAIL${NC}: $2 (port $1 not open)"
        ((FAIL++)) || true
        TEST_RESULTS+=("FAIL")
        PROBLEM_SOURCES+=("$3")
        PROBLEM_DETAILS+=("${4:-port $1 not open}")
    fi
}

assert_process_running() {
    ((TOTAL++)) || true
    if pgrep -f "$1" >/dev/null 2>&1; then
        echo -e "  ${GREEN}PASS${NC}: $2"
        ((PASS++)) || true
        TEST_RESULTS+=("PASS")
        PROBLEM_SOURCES+=("")
        PROBLEM_DETAILS+=("")
    else
        echo -e "  ${RED}FAIL${NC}: $2 (process not running: $1)"
        ((FAIL++)) || true
        TEST_RESULTS+=("FAIL")
        PROBLEM_SOURCES+=("$3")
        PROBLEM_DETAILS+=("${4:-process $1 not running}")
    fi
}

assert_json_field() {
    ((TOTAL++)) || true
    local v
    v=$(echo "$2" | jq -r "$1" 2>/dev/null)
    if [ "$v" = "$3" ]; then
        echo -e "  ${GREEN}PASS${NC}: $4"
        ((PASS++)) || true
        TEST_RESULTS+=("PASS")
        PROBLEM_SOURCES+=("")
        PROBLEM_DETAILS+=("")
    else
        echo -e "  ${RED}FAIL${NC}: $4 (field $1: expected '$3', got '$v')"
        ((FAIL++)) || true
        TEST_RESULTS+=("FAIL")
        PROBLEM_SOURCES+=("$5")
        PROBLEM_DETAILS+=("${6:-field $1 expected '$3', got '$v'}")
    fi
}

# ── Reporting ──────────────────────────────────────────────────────────────

generate_report() {
    local end_time=$(date +%s)
    local elapsed=$((end_time - START_TIME))

    echo
    echo "══════════════════════════════════════════════════════"
    echo "              TEST RESULTS SUMMARY"
    echo "══════════════════════════════════════════════════════"
    echo -e "  Total:   $TOTAL"
    echo -e "  ${GREEN}Passed:  $PASS${NC}"
    echo -e "  ${RED}Failed:  $FAIL${NC}"
    echo -e "  ${YELLOW}Skipped: $SKIP${NC}"
    echo "  Elapsed: ${elapsed}s"
    echo "══════════════════════════════════════════════════════"

    # JSON report
    cat > "$REPORT_FILE" << JSONEOF
{
  "test_suite": "odc-root-integration",
  "timestamp": "$(date -Iseconds)",
  "start_time": "$START_TIME",
  "end_time": "$end_time",
  "elapsed_seconds": $elapsed,
  "total": $TOTAL,
  "passed": $PASS,
  "failed": $FAIL,
  "skipped": $SKIP,
  "pass_rate": "$(awk "BEGIN {printf \"%.1f\", ($PASS/$TOTAL)*100}" 2>/dev/null || echo "0")%"
}
JSONEOF

    # Problem tracking report
    if [ "$FAIL" -gt 0 ]; then
        echo
        echo "══════════════════════════════════════════════════════"
        echo "              PROBLEM TRACKING REPORT"
        echo "══════════════════════════════════════════════════════"

        # Group by source
        declare -A source_counts
        for src in "${PROBLEM_SOURCES[@]}"; do
            if [ -n "$src" ]; then
                ((source_counts[$src]++)) || true
            fi
        done

        echo
        echo "  Failures by component:"
        for src in "${!source_counts[@]}"; do
            echo -e "    ${RED}${src}${NC}: ${source_counts[$src]} failures"
        done

        echo
        echo "  Detailed failure log:"
        local idx=0
        for result in "${TEST_RESULTS[@]}"; do
            if [ "$result" = "FAIL" ]; then
                echo -e "    ${RED}#${idx}${NC} Source: ${PROBLEM_SOURCES[$idx]:-unknown}"
                echo "      Detail: ${PROBLEM_DETAILS[$idx]:-no detail}"
                echo
            fi
            ((idx++)) || true
        done

        # Append to report JSON
        python3 -c "
import json
with open('$REPORT_FILE', 'r') as f:
    report = json.load(f)
report['problems'] = []
for i, r in enumerate('${TEST_RESULTS[*]}'.split()):
    if r == 'FAIL':
        report['problems'].append({
            'index': i,
            'source': '${PROBLEM_SOURCES[$i]}' if i < ${#PROBLEM_SOURCES[@]} else 'unknown',
            'detail': '${PROBLEM_DETAILS[$i]}' if i < ${#PROBLEM_DETAILS[@]} else ''
        })
with open('$REPORT_FILE', 'w') as f:
    json.dump(report, f, indent=2)
" 2>/dev/null || true
    fi

    # Copy logs
    cp /var/log/odc-root/*.log "$LOG_DIR/" 2>/dev/null || true
    cp /tmp/odc-test-*.log "$LOG_DIR/" 2>/dev/null || true
    journalctl -u 'odc-*' --no-pager -n 200 > "$LOG_DIR/journalctl.log" 2>/dev/null || true

    # Return exit code
    [ "$FAIL" -eq 0 ]
}

print_summary() {
    generate_report
}

# ── Test utilities ─────────────────────────────────────────────────────────

wait_for_port() {
    local port=$1
    local timeout=${2:-30}
    local deadline=$(($(date +%s) + timeout))
    while [ $(date +%s) -lt $deadline ]; do
        if timeout 1 bash -c "echo >/dev/tcp/127.0.0.1/$port" 2>/dev/null; then
            return 0
        fi
        sleep 1
    done
    return 1
}

wait_for_file() {
    local file=$1
    local timeout=${2:-30}
    local deadline=$(($(date +%s) + timeout))
    while [ $(date +%s) -lt $deadline ]; do
        if [ -e "$file" ]; then
            return 0
        fi
        sleep 1
    done
    return 1
}

wait_for_process() {
    local pattern=$1
    local timeout=${2:-30}
    local deadline=$(($(date +%s) + timeout))
    while [ $(date +%s) -lt $deadline ]; do
        if pgrep -f "$pattern" >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done
    return 1
}

kill_process_safe() {
    local pattern=$1
    local pid
    pid=$(pgrep -f "$pattern" 2>/dev/null || true)
    if [ -n "$pid" ]; then
        kill $pid 2>/dev/null || true
        sleep 1
        kill -9 $pid 2>/dev/null || true
    fi
}

echo "$(date -Iseconds) | START | odc-root integration test suite" > "$LOG_DIR/test_run.log"
