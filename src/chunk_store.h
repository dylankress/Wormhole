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

//=============================================================================
// Replica Metadata (for P2P chunk replication)
//=============================================================================

// Maximum tracked replica locations per chunk
#define MAX_REPLICAS 16

// Store a chunk and record which peer provided it.
// source_peer_id may be NULL for locally-originated chunks.
// Returns TRUE on success.
BOOLEAN ChunkStore_PutWithMeta(const uint8_t hash[WH_HASH_SIZE],
                                const uint8_t *data, uint32_t size,
                                const uint8_t source_peer_id[32]);

// Get the number of known replica locations for a chunk.
// Returns 0 if chunk unknown or on error.
uint32_t ChunkStore_GetReplicaCount(const uint8_t hash[WH_HASH_SIZE]);

// Record that a remote peer has a copy of this chunk.
// Returns TRUE on success.
BOOLEAN ChunkStore_SetReplicaLocation(const uint8_t hash[WH_HASH_SIZE],
                                       const uint8_t peer_id[32]);

// Get all known replica peer IDs for a chunk.
// peer_ids_out: caller-allocated array of [MAX_REPLICAS][32] bytes.
// Returns number of peer IDs written.
uint32_t ChunkStore_GetReplicaLocations(const uint8_t hash[WH_HASH_SIZE],
                                         uint8_t peer_ids_out[][32],
                                         uint32_t max_peers);

//=============================================================================
// Storage Quota and LRU Eviction
//=============================================================================

// Get total size of all stored chunks in bytes.
// Scans the store directory recursively.
uint64_t ChunkStore_GetTotalSize(void);

// Evict least-recently-used chunks to free at least bytes_to_free bytes.
// Prefers chunks with higher replica counts (safer to evict).
// Returns the number of bytes actually freed.
uint64_t ChunkStore_Evict(uint64_t bytes_to_free);

// Delete a single chunk from the store, including its replica metadata,
// access time entry, and proof cache.
// Returns TRUE if the chunk was deleted (or didn't exist).
BOOLEAN ChunkStore_Delete(const uint8_t hash[WH_HASH_SIZE]);

// Remove a specific peer from a chunk's replica location list.
// Returns TRUE if the peer was found and removed.
BOOLEAN ChunkStore_RemoveReplicaLocation(const uint8_t hash[WH_HASH_SIZE],
                                           const uint8_t peer_id[32]);

// Clear replica metadata for a chunk without deleting the chunk itself.
// Used to reset stale replica info on re-store.
void ChunkStore_ClearReplicas(const uint8_t hash[WH_HASH_SIZE]);

// Update the last-access time for a chunk (called on Get).
void ChunkStore_SetAccessTime(const uint8_t hash[WH_HASH_SIZE]);
