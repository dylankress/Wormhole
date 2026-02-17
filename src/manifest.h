//
// manifest.h
// Content-addressed file manifest for chunk-based transfers.
// by Dylan Kress
//

#pragma once

#include <stdint.h>
#include <stdlib.h>

#define WH_CHUNK_SIZE       (256 * 1024)   // 256KB
#define WH_HASH_SIZE        32             // Blake3 output bytes
#define WH_MANIFEST_MAGIC   0x4C4D4857     // "WHML" little-endian
#define WH_MANIFEST_VERSION   1
#define WH_MANIFEST_VERSION_2 2

typedef struct {
    uint8_t  hash[WH_HASH_SIZE];
    uint32_t chunk_size;            // last chunk may be smaller
} CHUNK_INFO;

// Version 2: per-file entry within a multi-file manifest
typedef struct {
    char      *relative_path;       // UTF-8, '/' separator in wire format
    uint16_t   path_length;
    uint64_t   file_size;
    uint32_t   chunk_start;         // first chunk index for this file
    uint32_t   chunk_count;         // number of chunks for this file
} FILE_ENTRY;

typedef struct {
    uint8_t    manifest_hash[WH_HASH_SIZE];  // Blake3 of serialized body
    uint8_t    version;             // 1 = single file, 2 = multi-file
    uint64_t   file_size;           // total size (sum of all files for v2)
    uint32_t   chunk_size;          // WH_CHUNK_SIZE
    uint32_t   chunk_count;         // total chunk count
    char      *filename;            // filename (v1) or directory name (v2)
    uint16_t   filename_length;
    CHUNK_INFO *chunks;

    // Version 2 multi-file fields (NULL/0 for v1)
    uint32_t    file_count;
    FILE_ENTRY *files;
} FILE_MANIFEST;

// Create an empty v1 manifest with chunk_count computed from file_size.
// filename is copied internally.
FILE_MANIFEST *Manifest_Create(const char *filename, uint64_t file_size);

// Create a v2 multi-file manifest. Computes chunk ranges from file sizes.
// relative_paths and file_sizes are arrays of file_count elements.
// dirname and paths are copied internally.
FILE_MANIFEST *Manifest_CreateMultiFile(const char *dirname,
                                        const char **relative_paths,
                                        const uint64_t *file_sizes,
                                        uint32_t file_count);

// Free all heap memory owned by the manifest.
void Manifest_Destroy(FILE_MANIFEST *manifest);

// Populate a single chunk entry.
void Manifest_AddChunk(FILE_MANIFEST *manifest, uint32_t index,
                       const uint8_t hash[WH_HASH_SIZE], uint32_t size);

// Compute manifest_hash = Blake3(serialized body after the hash field).
void Manifest_ComputeHash(FILE_MANIFEST *manifest);

// Serialize manifest to a heap-allocated buffer. Caller must free().
// Returns NULL on failure.
uint8_t *Manifest_Serialize(const FILE_MANIFEST *manifest, size_t *out_size);

// Deserialize a manifest from a buffer. Validates magic, version, and hash.
// Returns NULL on failure.
FILE_MANIFEST *Manifest_Deserialize(const uint8_t *data, size_t size);
