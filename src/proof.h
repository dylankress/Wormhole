//
// proof.h
// Proof-of-storage: Blake3(seed || chunk_data) proves possession without transferring the chunk.
// by Dylan Kress
//

#pragma once

#include <stdint.h>
#include "common.h"
#include "manifest.h"

// Number of pre-cached proofs per chunk
#define PROOF_CACHE_COUNT 8

// Compute a proof-of-storage: Blake3(seed || chunk_data)
void Proof_Compute(const uint8_t *chunk_data, uint32_t size,
                   const uint8_t seed[WH_HASH_SIZE],
                   uint8_t proof_out[WH_HASH_SIZE]);

// Pre-compute and cache proofs for a chunk.
// Generates PROOF_CACHE_COUNT random seeds, computes proofs, writes to
// ~/.wormhole/proofs/<hex_hash>
// Returns TRUE on success.
BOOLEAN Proof_PrecomputeAndCache(const uint8_t hash[WH_HASH_SIZE],
                                  const uint8_t *chunk_data, uint32_t size);

// Look up a cached proof for the given seed.
// Returns TRUE if a matching cached proof was found, FALSE otherwise.
BOOLEAN Proof_LookupCached(const uint8_t hash[WH_HASH_SIZE],
                            const uint8_t seed[WH_HASH_SIZE],
                            uint8_t proof_out[WH_HASH_SIZE]);

// Delete cached proofs for a chunk.
void Proof_DeleteCache(const uint8_t hash[WH_HASH_SIZE]);
