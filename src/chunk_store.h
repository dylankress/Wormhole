//
// chunk_store.h
// Git-style content-addressed chunk storage at ~/.wormhole/store/.
// by Dylan Kress
//

#pragma once

#include "manifest.h"
#include "common.h"

// Create the store base directory (~/.wormhole/store/).
// Returns TRUE on success.
BOOLEAN ChunkStore_Init(void);

// Write a chunk to disk keyed by its Blake3 hash.
// Skips if the chunk already exists.
// Returns TRUE on success.
BOOLEAN ChunkStore_Put(const uint8_t hash[WH_HASH_SIZE],
                       const uint8_t *data, uint32_t size);

// Check whether a chunk exists in the store.
BOOLEAN ChunkStore_Has(const uint8_t hash[WH_HASH_SIZE]);

// Read a chunk from the store into a caller-supplied buffer.
// *size_out is set to the number of bytes read.
// data_out must be at least WH_CHUNK_SIZE bytes.
// Returns TRUE on success.
BOOLEAN ChunkStore_Get(const uint8_t hash[WH_HASH_SIZE],
                       uint8_t *data_out, uint32_t *size_out);
