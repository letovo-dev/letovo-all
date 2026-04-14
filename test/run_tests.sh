#!/bin/bash
# run_tests.sh
# Kill server_starter → rebuild via install-run-core.sh → run all tests → compare failure count.
# Exit 0: pass (count unchanged or first baseline run)
# Exit 1: FAIL (failure count changed)
# Exit 2: ERROR (server never came up within timeout)

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$PROJECT_ROOT/test"
LAST_FAILURES_FILE="$TEST_DIR/.last_failed_count"
SERVER_PORT=8080
SERVER_WAIT_SECS=300  # allow up to 5 min for cmake build + server start

# ── 1. Kill any running server_starter ────────────────────────────────────────
echo "[1/4] Stopping existing server_starter..."
pkill -f "server_starter" 2>/dev/null && echo "  Killed existing process." || echo "  No process found."
sleep 1

# ── 2. Build and start server in background ───────────────────────────────────
echo "[2/4] Starting install-run-core.sh in background..."
cd "$PROJECT_ROOT"
./install-run-core.sh &
PARENT_PID=$!

echo "  Waiting up to ${SERVER_WAIT_SECS}s for port ${SERVER_PORT}..."
ELAPSED=0
while ! curl -s "http://0.0.0.0:${SERVER_PORT}" >/dev/null 2>&1; do
    sleep 2
    ELAPSED=$((ELAPSED + 2))
    if ! kill -0 "$PARENT_PID" 2>/dev/null; then
        echo "ERROR: install-run-core.sh exited unexpectedly."
        pkill -f "server_starter" 2>/dev/null || true
        exit 2
    fi
    if [ "$ELAPSED" -ge "$SERVER_WAIT_SECS" ]; then
        echo "ERROR: Server did not start within ${SERVER_WAIT_SECS}s."
        kill "$PARENT_PID" 2>/dev/null || true
        pkill -f "server_starter" 2>/dev/null || true
        exit 2
    fi
done
echo "  Server ready after ${ELAPSED}s."

# ── 3. Run all test_*.py files ────────────────────────────────────────────────
echo "[3/4] Running tests..."
cd "$TEST_DIR"
TEST_FILES=$(ls test_*.py 2>/dev/null | tr '\n' ' ')

if [ -z "$TEST_FILES" ]; then
    echo "ERROR: No test_*.py files found in $TEST_DIR"
    kill "$PARENT_PID" 2>/dev/null || true
    pkill -f "server_starter" 2>/dev/null || true
    exit 2
fi

PYTEST_OUTPUT=$(python3 -m pytest $TEST_FILES -v --continue-on-collection-errors 2>&1) || true
echo "$PYTEST_OUTPUT"

# Parse summary line: "N failed, M passed, ... K errors in Xs"
SUMMARY_LINE=$(echo "$PYTEST_OUTPUT" | grep -E "^=+ [0-9]+ (failed|passed|error)" | tail -1)
FAILED_COUNT=$(echo "$SUMMARY_LINE" | grep -oP '\d+(?= failed)' || echo 0)
ERROR_COUNT=$(echo "$SUMMARY_LINE"  | grep -oP '\d+(?= error)'  || echo 0)
CURRENT_FAILURES=$(( ${FAILED_COUNT:-0} + ${ERROR_COUNT:-0} ))

# ── 4. Stop server ────────────────────────────────────────────────────────────
echo "[4/4] Stopping server..."
kill "$PARENT_PID" 2>/dev/null || true
pkill -f "server_starter" 2>/dev/null || true

# ── Report ────────────────────────────────────────────────────────────────────
echo ""
echo "======================================="
echo "           TEST REPORT"
echo "======================================="
echo "  Failures this run : $CURRENT_FAILURES"

if [ -f "$LAST_FAILURES_FILE" ]; then
    PREV_FAILURES=$(cat "$LAST_FAILURES_FILE")
    echo "  Failures last run : $PREV_FAILURES"
else
    PREV_FAILURES=""
    echo "  Failures last run : (no baseline)"
fi

# Save current count as new baseline
echo "$CURRENT_FAILURES" > "$LAST_FAILURES_FILE"

echo "======================================="

if [ -z "$PREV_FAILURES" ]; then
    echo "RESULT: Baseline established ($CURRENT_FAILURES failures). Run again to compare."
    exit 0
elif [ "$CURRENT_FAILURES" -ne "$PREV_FAILURES" ]; then
    echo "RESULT: FAILED — count changed: $PREV_FAILURES → $CURRENT_FAILURES"
    exit 1
else
    echo "RESULT: PASSED — failure count unchanged ($CURRENT_FAILURES)"
    exit 0
fi
