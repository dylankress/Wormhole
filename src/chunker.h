//
// chunker.h
// Splits a file into Blake3-hashed chunks and produces a FILE_MANIFEST.
// by Dylan Kress
//

#pragma once

#include "manifest.h"

// Read a file, split into WH_CHUNK_SIZE chunks, Blake3-hash each chunk,
// and return a fully populated FILE_MANIFEST (including manifest_hash).
// Returns NULL on failure.
FILE_MANIFEST *Chunker_BuildManifest(const char *file_path);
