#!/bin/bash
# test_gui_ipc.sh — Docker-based GUI IPC test (full 22-test multi-node E2E)
#
# Builds a Docker image with daemon + test_gui_ipc binary, starts 3 daemon
# nodes + 1 test-runner, then runs the full 22-test suite with real network
# isolation and replication. Uses wormholerelay.com for relay connectivity.
#
# Usage:
#   cd docker && ./test_gui_ipc.sh
#
# Requires: Docker + Docker Compose

set -e
cd "$(dirname "$0")"

COMPOSE="docker compose -f docker-compose.gui-test.yml"

echo "============================================"
echo "  GUI IPC Test (Docker, 22 tests)"
echo "============================================"
echo ""

# ─── Build ────────────────────────────────────────────────────────────
echo "Building Docker images..."
$COMPOSE build
echo ""

# ─── Start containers ─────────────────────────────────────────────────
echo "Starting containers..."
$COMPOSE up -d
echo ""

# Wait for containers to be running
sleep 3

# ─── Write configs ────────────────────────────────────────────────────
echo "Writing daemon configs..."

# Node 1 — no bootstrap (seed node)
$COMPOSE exec -T node1 sh -c 'mkdir -p /root/.wormhole && cat > /root/.wormhole/config << CONF
relay_host=wormholerelay.com
relay_port=443
dht_enabled=1
dht_port=4568
ec_enabled=1
max_storage_gb=1
replication_target=3
min_storage_ratio=0
health_check_interval_sec=60
CONF'

# Node 2 — bootstraps to node1
$COMPOSE exec -T node2 sh -c 'mkdir -p /root/.wormhole && cat > /root/.wormhole/config << CONF
relay_host=wormholerelay.com
relay_port=443
dht_enabled=1
dht_port=4568
ec_enabled=1
max_storage_gb=1
replication_target=3
min_storage_ratio=0
health_check_interval_sec=60
dht_bootstrap_nodes=node1:4568
CONF'

# Node 3 — bootstraps to node1
$COMPOSE exec -T node3 sh -c 'mkdir -p /root/.wormhole && cat > /root/.wormhole/config << CONF
relay_host=wormholerelay.com
relay_port=443
dht_enabled=1
dht_port=4568
ec_enabled=1
max_storage_gb=1
replication_target=3
min_storage_ratio=0
health_check_interval_sec=60
dht_bootstrap_nodes=node1:4568
CONF'

echo "Configs written"
echo ""

# ─── Start daemons ────────────────────────────────────────────────────
echo "Starting daemons..."

$COMPOSE exec -T -d node1 sh -c 'wormholed --port 4567 > /tmp/wormholed.log 2>&1'
sleep 2
$COMPOSE exec -T -d node2 sh -c 'wormholed --port 4567 > /tmp/wormholed.log 2>&1'
$COMPOSE exec -T -d node3 sh -c 'wormholed --port 4567 > /tmp/wormholed.log 2>&1'

# ─── Wait for readiness ──────────────────────────────────────────────
wait_for_node() {
    local node="$1"
    local max_wait=30
    echo -n "  Waiting for ${node}..."
    for i in $(seq 1 $max_wait); do
        if $COMPOSE exec -T "$node" wormhole --daemon 4567 status >/dev/null 2>&1; then
            echo " ready (${i}s)"
            return 0
        fi
        sleep 1
    done
    echo " TIMEOUT"
    echo "  --- ${node} daemon log ---"
    $COMPOSE exec -T "$node" cat /tmp/wormholed.log 2>/dev/null | tail -20 || true
    return 1
}

ALL_UP=true
for node in node1 node2 node3; do
    if ! wait_for_node "$node"; then
        ALL_UP=false
    fi
done

if ! $ALL_UP; then
    echo ""
    echo "ERROR: Not all daemons started. Aborting."
    $COMPOSE down -v
    exit 1
fi

echo ""

# ─── Generate test file on shared volume ──────────────────────────────
echo "Generating test file..."
$COMPOSE exec -T test-runner sh -c 'dd if=/dev/urandom of=/test-data/test_1mb.bin bs=1024 count=1024 2>/dev/null'
$COMPOSE exec -T test-runner mkdir -p /test-data/received
echo ""

# ─── Run the test ─────────────────────────────────────────────────────
echo "--- Running test binary ---"
echo ""

EXIT_CODE=0
$COMPOSE exec -T test-runner test_gui_ipc \
    --test-file /test-data/test_1mb.bin \
    --socket /ipc/node1/wormhole_4567.sock \
    --socket2 /ipc/node2/wormhole_4567.sock \
    --socket3 /ipc/node3/wormhole_4567.sock \
    --output-dir /test-data/received \
    || EXIT_CODE=$?

echo ""

# ─── Show logs on failure ─────────────────────────────────────────────
if [ $EXIT_CODE -ne 0 ]; then
    echo "=== Daemon Logs ==="
    for node in node1 node2 node3; do
        echo "--- ${node} log (last 30 lines) ---"
        $COMPOSE exec -T "$node" cat /tmp/wormholed.log 2>/dev/null | tail -30 || true
    done
    echo ""
fi

# ─── Cleanup ──────────────────────────────────────────────────────────
echo "Cleaning up containers..."
$COMPOSE down -v

if [ $EXIT_CODE -eq 0 ]; then
    echo ""
    echo "=== ALL 22 TESTS PASSED ==="
else
    echo ""
    echo "=== SOME TESTS FAILED (exit code $EXIT_CODE) ==="
fi

exit $EXIT_CODE
