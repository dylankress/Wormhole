#!/bin/bash
# test_linux.sh — Build and run Wormhole unit tests on Linux
# Run from src/test/ directory
# Prerequisites: main build must be done first (cd src && make)

set -e
cd "$(dirname "$0")"

CC="${CC:-gcc}"
CFLAGS="-Wall -Wextra -Wno-unused-parameter -O2 -std=gnu11 -D_GNU_SOURCE -DBLAKE3_NO_SSE2 -DBLAKE3_NO_SSE41 -DBLAKE3_NO_AVX2 -DBLAKE3_NO_AVX512 -DQUIC_API_ENABLE_PREVIEW_FEATURES"
MSQUIC_INC="../../msquic/src/inc"
BLAKE3_ROOT="../../deps/blake3"
RS_ROOT="../../deps/reed_solomon"
SODIUM_INC="/usr/include"
BUILD="../build"
INCLUDES="-I${MSQUIC_INC} -I${BLAKE3_ROOT} -I${RS_ROOT} -I${SODIUM_INC} -I.."

# Check main build exists
if [ ! -f "${BUILD}/wormhole" ] && [ ! -f "${BUILD}/wormholed" ]; then
    echo "ERROR: Main build not found. Run 'cd src && make' first."
    exit 1
fi

# Blake3 objects - rebuild from source for tests
BLAKE3_SRC="${BLAKE3_ROOT}/blake3.c ${BLAKE3_ROOT}/blake3_dispatch.c ${BLAKE3_ROOT}/blake3_portable.c"
RS_SRC="${RS_ROOT}/rs.c"

TOTAL=0
PASS=0
FAIL=0

run_test() {
    local name="$1"
    shift
    TOTAL=$((TOTAL + 1))
    echo ""
    echo "--- ${name} ---"

    # Compile
    if ! $CC $CFLAGS $INCLUDES "$@" -o "${name}" -lsodium -lpthread -lm 2>&1; then
        echo "  COMPILE FAILED"
        FAIL=$((FAIL + 1))
        return
    fi

    # Run
    if ./"${name}"; then
        PASS=$((PASS + 1))
    else
        echo "  TEST FAILED (exit code $?)"
        FAIL=$((FAIL + 1))
    fi
    rm -f "${name}"
}

# Clean previous test artifacts
rm -f test_*.o test_wire_format test_manifest test_chunk_store test_transfer_state \
      test_config test_chunker test_reed_solomon test_erasure test_routing_table \
      test_dht_protocol test_dht_store test_dht_lookup test_proof test_incentives \
      test_health test_file_registry test_file_crypto

echo "============================================"
echo "  Wormhole Unit Tests (Linux)"
echo "============================================"

# 1. test_wire_format (header-only, no link deps)
run_test test_wire_format \
    test_wire_format.c

# 2. test_manifest
run_test test_manifest \
    test_manifest.c ../manifest.c $BLAKE3_SRC

# 3. test_chunk_store
run_test test_chunk_store \
    test_chunk_store.c ../chunk_store.c ../config.c ../file_io.c ../proof.c $BLAKE3_SRC

# 4. test_transfer_state
run_test test_transfer_state \
    test_transfer_state.c ../transfer_state.c ../file_io.c

# 5. test_config
run_test test_config \
    test_config.c ../config.c

# 6. test_chunker
run_test test_chunker \
    test_chunker.c ../chunker.c ../chunk_store.c ../config.c ../manifest.c ../file_io.c ../proof.c $BLAKE3_SRC

# 7. test_reed_solomon
run_test test_reed_solomon \
    test_reed_solomon.c $RS_SRC

# 8. test_erasure
run_test test_erasure \
    test_erasure.c ../erasure.c $RS_SRC ../chunk_store.c ../config.c ../manifest.c ../file_io.c ../proof.c $BLAKE3_SRC

# 9. test_routing_table
run_test test_routing_table \
    test_routing_table.c ../dht/routing_table.c

# 10. test_dht_protocol
run_test test_dht_protocol \
    test_dht_protocol.c ../relay/peer_id.c

# 11. test_dht_store
run_test test_dht_store \
    test_dht_store.c ../dht/dht_store.c

# 12. test_dht_lookup
run_test test_dht_lookup \
    test_dht_lookup.c ../dht/dht_lookup.c ../dht/routing_table.c

# 13. test_proof
run_test test_proof \
    test_proof.c ../proof.c $BLAKE3_SRC

# 14. test_incentives
run_test test_incentives \
    test_incentives.c ../incentives.c

# 15. test_health
run_test test_health \
    test_health.c ../health.c ../erasure.c $RS_SRC ../chunk_store.c ../config.c ../manifest.c ../file_io.c ../proof.c $BLAKE3_SRC

# 16. test_file_registry
run_test test_file_registry \
    test_file_registry.c ../file_registry.c ../manifest.c ../file_io.c $BLAKE3_SRC

# 17. test_file_crypto
run_test test_file_crypto \
    test_file_crypto.c ../file_crypto.c

echo ""
echo "============================================"
echo "  Results: ${PASS}/${TOTAL} passed, ${FAIL} failed"
echo "============================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
