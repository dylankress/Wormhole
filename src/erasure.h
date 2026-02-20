//
// erasure.h
// Erasure coding integration — Reed-Solomon protection for chunk storage.
// by Dylan Kress
//

#pragma once

#include "manifest.h"
#include "common.h"
#include <stdint.h>

// Default erasure coding parameters
#define EC_DEFAULT_DATA_SHARDS   8
#define EC_DEFAULT_PARITY_SHARDS 4

// Maximum parity shards per stripe
#define EC_MAX_PARITY 16

// Erasure coding stripe — one group of data + parity chunks
typedef struct {
    uint32_t data_chunk_start;                           // First data chunk index in manifest
    uint32_t data_chunk_count;                           // Actual data chunks (<= k, last stripe may be less)
    uint8_t  parity_hashes[EC_MAX_PARITY][WH_HASH_SIZE]; // Blake3 hashes of parity shards
    uint32_t parity_sizes[EC_MAX_PARITY];                // Size of each parity shard
} EC_STRIPE;

// Erasure coding group for an entire file/manifest
typedef struct EC_GROUP {
    uint8_t    k;              // Data shards per stripe
    uint8_t    m;              // Parity shards per stripe
    uint32_t   stripe_count;
    EC_STRIPE *stripes;
} EC_GROUP;

// Encode a manifest's chunks with erasure coding.
// Reads chunk data from ChunkStore, generates parity, stores parity chunks.
// Returns EC_GROUP (caller must free with ErasureCoding_DestroyGroup), or NULL on failure.
EC_GROUP *ErasureCoding_Encode(const FILE_MANIFEST *manifest, uint8_t k, uint8_t m);

// Reconstruct a missing data chunk using erasure coding.
// chunk_index: the missing chunk's index in manifest
// group: the EC group containing stripe info
// manifest: the file manifest
// Returns TRUE if chunk was reconstructed and stored, FALSE on failure.
BOOLEAN ErasureCoding_ReconstructChunk(uint32_t chunk_index, const EC_GROUP *group,
                                        const FILE_MANIFEST *manifest);

// Serialize EC_GROUP to a buffer. Caller must free().
uint8_t *ErasureCoding_SerializeGroup(const EC_GROUP *group, size_t *out_size);

// Deserialize EC_GROUP from a buffer. Returns NULL on failure.
EC_GROUP *ErasureCoding_DeserializeGroup(const uint8_t *data, size_t size);

// Free an EC_GROUP.
void ErasureCoding_DestroyGroup(EC_GROUP *group);

// Save EC metadata file (EC_GROUP + manifest) to path.
// Format: [4B ec_size][ec_data][4B manifest_size][manifest_data]
BOOLEAN ErasureCoding_SaveMetadata(const char *path, const EC_GROUP *group,
                                     const FILE_MANIFEST *manifest);

// Load EC metadata file. Returns allocated EC_GROUP; caller must free with DestroyGroup.
// *out_manifest is allocated; caller must free with Manifest_Destroy.
// Returns NULL on failure.
EC_GROUP *ErasureCoding_LoadMetadata(const char *path, FILE_MANIFEST **out_manifest);
