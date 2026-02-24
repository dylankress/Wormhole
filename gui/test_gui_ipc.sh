#!/bin/bash
# Headless GUI IPC test — single-node localhost smoke test (15 tests).
# Starts 1 daemon, runs tests 1-14 + 22 via test_gui_ipc binary.
# Tests 15-21 (multi-node replication + transfer) are SKIPPED here.
# For full 22-test multi-node run, use: cd docker && ./test_gui_ipc.sh
#
# Usage:
#   ./test_gui_ipc.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_linux"
SRC_DIR="${SCRIPT_DIR}/../src"
DAEMON="${SRC_DIR}/build/wormholed"
TEST_BIN="${BUILD_DIR}/test_gui_ipc"

echo "=== GUI IPC Test (Single-Node, 15 tests) ==="
echo ""
echo "Tests 15-21 (replication/transfer) require Docker multi-node."
echo "Run: cd docker && ./test_gui_ipc.sh"
echo ""

# --- Verify binaries ---
if [ ! -x "$DAEMON" ]; then
    echo "ERROR: Daemon not found at $DAEMON"
    echo "       Build it first: cd src && make"
    exit 1
fi

if [ ! -x "$TEST_BIN" ]; then
    echo "ERROR: Test binary not found at $TEST_BIN"
    echo "       Build it first: cd gui && cmake -B build_linux && cmake --build build_linux"
    exit 1
fi

# --- Kill leftovers ---
pkill -f "wormholed.*--port 4567" 2>/dev/null || true
sleep 1

# --- Create temp directory ---
TEST_DIR=$(mktemp -d)
echo "Test dir: $TEST_DIR"

HOME1="${TEST_DIR}/home1"
mkdir -p "${HOME1}/.wormhole"

PIDS=""
EXIT_CODE=0

cleanup() {
    echo ""
    echo "Cleaning up..."
    for pid in $PIDS; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

show_logs() {
    echo ""
    echo "=== Diagnostic Logs ==="
    if [ -f "${TEST_DIR}/node1.log" ]; then
        echo "--- node1.log (last 30 lines) ---"
        tail -30 "${TEST_DIR}/node1.log" 2>/dev/null || true
    fi
}

# --- Write config ---
cat > "${HOME1}/.wormhole/config" <<CONF
relay_host=wormholerelay.com
relay_port=443
dht_enabled=1
dht_port=14568
ec_enabled=1
max_storage_gb=1
replication_target=3
min_storage_ratio=0
health_check_interval_sec=60
CONF

# --- Wait for readiness via IPC status ---
wait_for_daemon() {
    local port="$1"
    local home_dir="$2"
    local label="$3"
    local retries=30

    for i in $(seq 1 $retries); do
        HOME="$home_dir" "${SRC_DIR}/build/wormhole" --daemon "$port" status >/dev/null 2>&1 && return 0
        sleep 1
    done

    echo "ERROR: ${label} failed to start within ${retries}s"
    return 1
}

# --- Start daemon ---
echo "Starting daemon (QUIC:4567, DHT:14568)..."
HOME="$HOME1" "$DAEMON" --port 4567 > "${TEST_DIR}/node1.log" 2>&1 &
PIDS="$! $PIDS"
sleep 2

if ! wait_for_daemon 4567 "$HOME1" "Daemon"; then
    echo "--- node1.log ---"
    cat "${TEST_DIR}/node1.log" 2>/dev/null
    exit 1
fi
echo "Daemon running"

# --- Generate test file ---
dd if=/dev/urandom of="${TEST_DIR}/test_1mb.bin" bs=1024 count=1024 2>/dev/null
echo ""
echo "Test file: ${TEST_DIR}/test_1mb.bin (1 MB)"

# --- Run the test ---
echo ""
echo "--- Running test binary ---"
echo ""

HOME="$HOME1" "$TEST_BIN" \
    --test-file "${TEST_DIR}/test_1mb.bin" \
    --socket "${HOME1}/.wormhole/wormhole_4567.sock" \
    || EXIT_CODE=$?

echo ""
if [ $EXIT_CODE -eq 0 ]; then
    echo "=== ALL TESTS PASSED ==="
else
    echo "=== SOME TESTS FAILED (exit code $EXIT_CODE) ==="
    show_logs
fi

exit $EXIT_CODE
