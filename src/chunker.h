//
// chunker.h
// Splits files into Blake3-hashed chunks and produces a FILE_MANIFEST.
// by Dylan Kress
//

#pragma once

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
