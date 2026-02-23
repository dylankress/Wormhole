//
// chunker.h
// Splits files into Blake3-hashed chunks and produces a FILE_MANIFEST.
// by Dylan Kress
//

#pragma once

#include <stdint.h>
#include "manifest.h"

// Read a file, split into WH_CHUNK_SIZE chunks, Blake3-hash each chunk,
// and return a fully populated FILE_MANIFEST (including manifest_hash).
// Returns NULL on failure.
FILE_MANIFEST *Chunker_BuildManifest(const char *file_path);

// Recursively enumerate a directory, chunk all files, and return a
// version 2 (multi-file) FILE_MANIFEST with computed hashes.
// Returns NULL on failure.
FILE_MANIFEST *Chunker_BuildManifestFromDirectory(const char *dir_path);

// Single-pass: read file, hash each chunk, store in ChunkStore, build manifest.
// Eliminates the double-read of BuildManifest + separate store loop.
// *stored_count is set to the number of chunks actually stored (new, not deduped).
// Returns NULL on failure.
FILE_MANIFEST *Chunker_BuildManifestAndStore(const char *file_path,
                                              uint32_t *stored_count);

// Progress callback for chunker operations.
// chunk_index: 0-based index of chunk just processed
// total_chunks: total number of chunks
// bytes_done: cumulative bytes processed so far
// bytes_total: total file size
// user_context: opaque pointer passed through
// Returns 0 to cancel the operation, non-zero to continue.
typedef int (*ChunkerProgressCallback)(
    uint32_t chunk_index, uint32_t total_chunks,
    uint64_t bytes_done, uint64_t bytes_total,
    void *user_context
);

// Same as Chunker_BuildManifestAndStore but with progress callback.
// If progress_cb returns 0, operation is cancelled and NULL is returned.
FILE_MANIFEST *Chunker_BuildManifestAndStoreWithProgress(
    const char *file_path, uint32_t *stored_count,
    ChunkerProgressCallback progress_cb, void *progress_ctx);
