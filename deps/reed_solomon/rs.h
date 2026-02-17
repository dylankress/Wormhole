//
// rs.h
// Reed-Solomon GF(2^8) erasure coding codec.
// Standalone library — no Wormhole dependencies.
// by Dylan Kress
//

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifndef RS_TRUE
#define RS_TRUE  1
#define RS_FALSE 0
#endif

typedef struct {
    uint8_t  k;              // data shards
    uint8_t  m;              // parity shards
    uint8_t  n;              // total (k + m)
    uint8_t *encode_matrix;  // n*k matrix (systematic Vandermonde)
} RS_CODEC;

// Create RS codec with k data shards and m parity shards.
// Returns NULL on failure. Caller must call RS_Destroy().
RS_CODEC *RS_Create(uint8_t k, uint8_t m);

// Destroy codec and free memory.
void RS_Destroy(RS_CODEC *codec);

// Encode: generate m parity shards from k data shards.
// data_shards: array of k pointers to shard data (each shard_size bytes)
// parity_out: array of m pointers to pre-allocated output buffers (each shard_size bytes)
// Returns 1 on success, 0 on failure.
int RS_Encode(const RS_CODEC *codec, const uint8_t *const *data_shards,
              uint8_t **parity_out, size_t shard_size);

// Decode: reconstruct missing shards (need k of n present).
// shards: array of n pointers. Missing shards should be NULL; they will be
//         reconstructed in-place. Caller must pre-allocate buffers for missing shards.
// present: array of n booleans (1=present, 0=missing)
// Returns 1 on success, 0 on failure (too many missing).
int RS_Decode(const RS_CODEC *codec, uint8_t **shards, const uint8_t *present,
              size_t shard_size);
