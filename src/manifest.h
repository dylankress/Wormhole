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
#define WH_MANIFEST_VERSION 1

typedef struct {
    uint8_t  hash[WH_HASH_SIZE];
    uint32_t chunk_size;            // last chunk may be smaller
} CHUNK_INFO;

typedef struct {
    uint8_t    manifest_hash[WH_HASH_SIZE];  // Blake3 of serialized body
    uint64_t   file_size;
    uint32_t   chunk_size;          // WH_CHUNK_SIZE
    uint32_t   chunk_count;
    char      *filename;
    uint16_t   filename_length;
    CHUNK_INFO *chunks;
} FILE_MANIFEST;

// Create an empty manifest with chunk_count computed from file_size.
// filename is copied internally.
FILE_MANIFEST *Manifest_Create(const char *filename, uint64_t file_size);

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
