//
// file_registry.h
// File-level tracking for stored files — maps file IDs to their chunks.
// by Dylan Kress
//

#pragma once

#include "manifest.h"
#include "common.h"
#include <stdint.h>

// File lifecycle status
typedef enum {
    FILE_STATUS_STORING     = 0x01,  // Chunks being written to local store
    FILE_STATUS_REPLICATING = 0x02,  // Stored locally, replication in progress
    FILE_STATUS_SAFE        = 0x03,  // All chunks replicated to target
    FILE_STATUS_OFFLOADED   = 0x04,  // Local chunks evicted, only on network
} FILE_STATUS;

// Per-file metadata (returned by list/load operations)
typedef struct {
    uint8_t     manifest_hash[WH_HASH_SIZE];  // File ID (manifest hash)
    char        filename[260];                  // Original filename
    uint64_t    file_size;                      // Total file size in bytes
    uint32_t    chunk_count;                    // Total chunks (data only)
    uint32_t    replicated_count;               // Chunks at replication target
    uint64_t    store_time;                     // Unix timestamp when stored
    FILE_STATUS status;                         // Current lifecycle status
    uint8_t     encryption_key[32];             // Per-file encryption key (local only, never sent)
    BOOLEAN     encrypted;                      // Whether file is encrypted
} FILE_REG_ENTRY;

// Create the ~/.wormhole/files/ directory.
// Returns TRUE on success.
BOOLEAN FileRegistry_Init(void);

// Save a file entry with its manifest (for chunk retrieval later).
// Writes: ~/.wormhole/files/<hex8>.file
// manifest: the full manifest (serialized and stored for later retrieval)
// filename: original filename
// status: initial status (typically FILE_STATUS_STORING)
// Returns TRUE on success.
BOOLEAN FileRegistry_Save(const FILE_MANIFEST *manifest, const char *filename,
                           FILE_STATUS status);

// Save a file entry with encryption key.
// Same as FileRegistry_Save, but also stores the 32-byte encryption key.
BOOLEAN FileRegistry_SaveEncrypted(const FILE_MANIFEST *manifest, const char *filename,
                                     FILE_STATUS status,
                                     const uint8_t encryption_key[32]);

// Load a file entry by its full manifest hash.
// entry_out: filled with metadata on success
// manifest_out: if non-NULL, set to deserialized manifest (caller must Manifest_Destroy)
// Returns TRUE on success.
BOOLEAN FileRegistry_Load(const uint8_t manifest_hash[WH_HASH_SIZE],
                           FILE_REG_ENTRY *entry_out,
                           FILE_MANIFEST **manifest_out);

// Update file status and replicated_count.
// Returns TRUE on success.
BOOLEAN FileRegistry_UpdateStatus(const uint8_t manifest_hash[WH_HASH_SIZE],
                                    FILE_STATUS status, uint32_t replicated_count);

// List all stored files.
// entries_out: caller-allocated array
// max_entries: capacity of entries_out
// Returns number of entries written.
uint32_t FileRegistry_List(FILE_REG_ENTRY *entries_out, uint32_t max_entries);

// Delete a file entry by its manifest hash.
// Removes: ~/.wormhole/files/<hex64>.file
// Returns TRUE if deleted (or didn't exist).
BOOLEAN FileRegistry_Delete(const uint8_t manifest_hash[WH_HASH_SIZE]);

// Find a file entry by hex prefix (like git short hashes, min 8 chars).
// entry_out: filled with metadata on success
// manifest_out: if non-NULL, set to deserialized manifest
// Returns TRUE if exactly one match found, FALSE on no match or ambiguous.
BOOLEAN FileRegistry_FindByPrefix(const char *hex_prefix,
                                    FILE_REG_ENTRY *entry_out,
                                    FILE_MANIFEST **manifest_out);
