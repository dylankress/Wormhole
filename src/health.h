//
// health.h
// Health checking and recovery for P2P stored chunks.
// by Dylan Kress
//

#pragma once

#include <stdint.h>
#include "common.h"
#include "manifest.h"

// Forward declarations (avoid including full DHT/erasure headers)
typedef struct DHT_NODE DHT_NODE;
typedef struct EC_GROUP EC_GROUP;

// Health check result for a single chunk
typedef struct {
    uint8_t  hash[WH_HASH_SIZE];
    uint32_t verified_replicas;
    uint32_t failed_replicas;
    BOOLEAN  needs_repair;
} CHUNK_HEALTH;

// Health check stats
typedef struct {
    uint32_t chunks_checked;
    uint32_t chunks_healthy;
    uint32_t chunks_degraded;
    uint32_t chunks_critical;   // Below minimum replicas
    uint32_t repairs_attempted;
    uint32_t repairs_succeeded;
} HEALTH_STATS;

// Check health of locally-originated chunks.
// Queries chunk locations, returns stats.
// dht may be NULL (skip DHT-based checks).
HEALTH_STATS Health_CheckChunks(uint32_t max_chunks);

// Attempt to recover a missing chunk using erasure coding.
// Returns TRUE if chunk was successfully reconstructed and stored.
BOOLEAN Health_RecoverChunk(const uint8_t hash[WH_HASH_SIZE],
                             const EC_GROUP *ec_group,
                             const FILE_MANIFEST *manifest);

// Get hashes of degraded chunks (replica_count < REPLICATION_TARGET).
// out_hashes: caller-allocated array of [max_count][WH_HASH_SIZE].
// Returns number of degraded chunk hashes written.
uint32_t Health_GetDegradedChunks(uint8_t (*out_hashes)[WH_HASH_SIZE], uint32_t max_count);

// Get hashes of replicated chunks (replica_count > 0).
// out_hashes: caller-allocated array of [max_count][WH_HASH_SIZE].
// Returns number of replicated chunk hashes written.
uint32_t Health_GetReplicatedChunks(uint8_t (*out_hashes)[WH_HASH_SIZE], uint32_t max_count);

// Check if a chunk needs more replicas and initiate replication.
// current_replicas: known replica count
// target: desired replica count
// Returns the number of new replications initiated.
uint32_t Health_ReplicateChunk(const uint8_t hash[WH_HASH_SIZE],
                                uint32_t current_replicas, uint32_t target);
