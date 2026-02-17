//
// incentives.h
// Storage ratio tracking to prevent freeloading in the P2P network.
// by Dylan Kress
//

#pragma once

#include <stdint.h>
#include <time.h>
#include "common.h"

#define LEDGER_MAX_PEERS 256
#define LEDGER_MAGIC     0x4C444752  // "LDGR" little-endian
#define LEDGER_VERSION   1

// Threshold for new peers with no history (10 MB)
#define LEDGER_NEW_PEER_THRESHOLD (10 * 1024 * 1024)

// Per-peer storage balance
typedef struct {
    uint8_t  peer_id[32];
    uint64_t bytes_stored_for_us;     // How much this peer stores for us
    uint64_t bytes_we_store_for_them; // How much we store for this peer
    time_t   last_updated;
} PEER_BALANCE;

// Storage ledger
typedef struct {
    PEER_BALANCE peers[LEDGER_MAX_PEERS];
    uint32_t     peer_count;
    uint64_t     total_stored_for_us;
    uint64_t     total_we_store;
} STORAGE_LEDGER;

// Initialize ledger (zero out)
void Ledger_Init(STORAGE_LEDGER *ledger);

// Record that a peer stored bytes for us
void Ledger_RecordStoredForUs(STORAGE_LEDGER *ledger, const uint8_t peer_id[32], uint64_t bytes);

// Record that we stored bytes for a peer
void Ledger_RecordWeStored(STORAGE_LEDGER *ledger, const uint8_t peer_id[32], uint64_t bytes);

// Check if we should accept a storage request from this peer.
// Returns FALSE if the ratio would be too unbalanced (we'd store > 2x what they store for us).
// New peers (no history) are always accepted up to LEDGER_NEW_PEER_THRESHOLD.
BOOLEAN Ledger_ShouldAcceptStorage(const STORAGE_LEDGER *ledger, const uint8_t peer_id[32],
                                     uint64_t additional_bytes);

// Get the storage ratio for a peer (stored_for_us / we_store_for_them).
// Returns 1.0 if no data stored for them (neutral).
double Ledger_GetPeerRatio(const STORAGE_LEDGER *ledger, const uint8_t peer_id[32]);

// Save ledger to file.
// Format: [4B magic][4B version][4B peer_count][per peer: 32B id, 8B for_us, 8B we_store, 8B last_updated]
BOOLEAN Ledger_Save(const STORAGE_LEDGER *ledger, const char *path);

// Load ledger from file.
BOOLEAN Ledger_Load(STORAGE_LEDGER *ledger, const char *path);
