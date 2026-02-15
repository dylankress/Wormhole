//
// manifest.c
// Content-addressed file manifest — serialization and lifecycle.
// by Dylan Kress
//

#include "manifest.h"
#include "wire_format.h"
#include <blake3.h>
#include <string.h>
#include <stdio.h>

//=============================================================================
// Manifest Wire Format
//=============================================================================
//
// [4B]  magic            (WH_MANIFEST_MAGIC)
// [1B]  version          (WH_MANIFEST_VERSION)
// [32B] manifest_hash    (Blake3 of everything after this field)
// --- body (hashed region) ---
// [8B]  file_size
// [4B]  chunk_size       (WH_CHUNK_SIZE)
// [4B]  chunk_count
// [2B]  filename_length
// [NB]  filename         (UTF-8, no null terminator)
// Per chunk (chunk_count times):
//   [32B] chunk_hash
//   [4B]  chunk_size
//
// Header size before body = 4 + 1 + 32 = 37 bytes
// Body fixed size = 8 + 4 + 4 + 2 = 18 bytes
// Per-chunk size = 32 + 4 = 36 bytes

#define MANIFEST_HEADER_SIZE  (4 + 1 + WH_HASH_SIZE)  // magic + version + hash
#define MANIFEST_BODY_FIXED   (8 + 4 + 4 + 2)         // file_size + chunk_size + chunk_count + filename_len
#define MANIFEST_PER_CHUNK    (WH_HASH_SIZE + 4)       // hash + chunk_size

FILE_MANIFEST *Manifest_Create(const char *filename, uint64_t file_size)
{
    if (!filename) return NULL;

    FILE_MANIFEST *m = (FILE_MANIFEST *)calloc(1, sizeof(FILE_MANIFEST));
    if (!m) return NULL;

    m->file_size = file_size;
    m->chunk_size = WH_CHUNK_SIZE;

    // Compute chunk count (ceiling division)
    if (file_size == 0)
    {
        m->chunk_count = 0;
    }
    else
    {
        m->chunk_count = (uint32_t)((file_size + WH_CHUNK_SIZE - 1) / WH_CHUNK_SIZE);
    }

    // Copy filename
    m->filename_length = (uint16_t)strlen(filename);
    m->filename = (char *)malloc(m->filename_length + 1);
    if (!m->filename)
    {
        free(m);
        return NULL;
    }
    memcpy(m->filename, filename, m->filename_length);
    m->filename[m->filename_length] = '\0';

    // Allocate chunk info array
    if (m->chunk_count > 0)
    {
        m->chunks = (CHUNK_INFO *)calloc(m->chunk_count, sizeof(CHUNK_INFO));
        if (!m->chunks)
        {
            free(m->filename);
            free(m);
            return NULL;
        }
    }

    return m;
}

void Manifest_Destroy(FILE_MANIFEST *manifest)
{
    if (!manifest) return;
    if (manifest->filename) free(manifest->filename);
    if (manifest->chunks) free(manifest->chunks);
    free(manifest);
}

void Manifest_AddChunk(FILE_MANIFEST *manifest, uint32_t index,
                       const uint8_t hash[WH_HASH_SIZE], uint32_t size)
{
    if (!manifest || index >= manifest->chunk_count) return;
    memcpy(manifest->chunks[index].hash, hash, WH_HASH_SIZE);
    manifest->chunks[index].chunk_size = size;
}

void Manifest_ComputeHash(FILE_MANIFEST *manifest)
{
    if (!manifest) return;

    // Serialize the body portion (everything after the hash field)
    // to compute the manifest hash.
    size_t body_size = MANIFEST_BODY_FIXED + manifest->filename_length +
                       (size_t)manifest->chunk_count * MANIFEST_PER_CHUNK;

    uint8_t *body = (uint8_t *)malloc(body_size);
    if (!body) return;

    uint8_t *p = body;
    WriteUint64LE(p, manifest->file_size);   p += 8;
    WriteUint32LE(p, manifest->chunk_size);  p += 4;
    WriteUint32LE(p, manifest->chunk_count); p += 4;
    WriteUint16LE(p, manifest->filename_length); p += 2;
    memcpy(p, manifest->filename, manifest->filename_length);
    p += manifest->filename_length;

    for (uint32_t i = 0; i < manifest->chunk_count; i++)
    {
        memcpy(p, manifest->chunks[i].hash, WH_HASH_SIZE);
        p += WH_HASH_SIZE;
        WriteUint32LE(p, manifest->chunks[i].chunk_size);
        p += 4;
    }

    // Hash the body
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, body, body_size);
    blake3_hasher_finalize(&hasher, manifest->manifest_hash, WH_HASH_SIZE);

    free(body);
}

uint8_t *Manifest_Serialize(const FILE_MANIFEST *manifest, size_t *out_size)
{
    if (!manifest || !out_size) return NULL;

    size_t body_size = MANIFEST_BODY_FIXED + manifest->filename_length +
                       (size_t)manifest->chunk_count * MANIFEST_PER_CHUNK;
    size_t total_size = MANIFEST_HEADER_SIZE + body_size;

    uint8_t *buf = (uint8_t *)malloc(total_size);
    if (!buf) return NULL;

    uint8_t *p = buf;

    // Header
    WriteUint32LE(p, WH_MANIFEST_MAGIC);    p += 4;
    *p = WH_MANIFEST_VERSION;               p += 1;
    memcpy(p, manifest->manifest_hash, WH_HASH_SIZE);
    p += WH_HASH_SIZE;

    // Body
    WriteUint64LE(p, manifest->file_size);   p += 8;
    WriteUint32LE(p, manifest->chunk_size);  p += 4;
    WriteUint32LE(p, manifest->chunk_count); p += 4;
    WriteUint16LE(p, manifest->filename_length); p += 2;
    memcpy(p, manifest->filename, manifest->filename_length);
    p += manifest->filename_length;

    for (uint32_t i = 0; i < manifest->chunk_count; i++)
    {
        memcpy(p, manifest->chunks[i].hash, WH_HASH_SIZE);
        p += WH_HASH_SIZE;
        WriteUint32LE(p, manifest->chunks[i].chunk_size);
        p += 4;
    }

    *out_size = total_size;
    return buf;
}

FILE_MANIFEST *Manifest_Deserialize(const uint8_t *data, size_t size)
{
    if (!data) return NULL;

    // Need at least header + body fixed fields
    if (size < MANIFEST_HEADER_SIZE + MANIFEST_BODY_FIXED) return NULL;

    const uint8_t *p = data;

    // Validate magic
    uint32_t magic = ReadUint32LE(p); p += 4;
    if (magic != WH_MANIFEST_MAGIC) return NULL;

    // Validate version
    uint8_t version = *p; p += 1;
    if (version != WH_MANIFEST_VERSION) return NULL;

    // Read manifest hash
    uint8_t stored_hash[WH_HASH_SIZE];
    memcpy(stored_hash, p, WH_HASH_SIZE);
    p += WH_HASH_SIZE;

    // Parse body fields
    const uint8_t *body_start = p;

    uint64_t file_size = ReadUint64LE(p);   p += 8;
    uint32_t chunk_size = ReadUint32LE(p);  p += 4;
    uint32_t chunk_count = ReadUint32LE(p); p += 4;
    uint16_t filename_length = ReadUint16LE(p); p += 2;

    // Validate sizes
    size_t expected_body = MANIFEST_BODY_FIXED + filename_length +
                           (size_t)chunk_count * MANIFEST_PER_CHUNK;
    if (size < MANIFEST_HEADER_SIZE + expected_body) return NULL;
    if (filename_length == 0) return NULL;

    // Read filename
    char *filename = (char *)malloc(filename_length + 1);
    if (!filename) return NULL;
    memcpy(filename, p, filename_length);
    filename[filename_length] = '\0';
    p += filename_length;

    // Verify hash of body
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, body_start, expected_body);
    uint8_t computed_hash[WH_HASH_SIZE];
    blake3_hasher_finalize(&hasher, computed_hash, WH_HASH_SIZE);

    if (memcmp(stored_hash, computed_hash, WH_HASH_SIZE) != 0)
    {
        free(filename);
        return NULL;
    }

    // Build manifest
    FILE_MANIFEST *m = (FILE_MANIFEST *)calloc(1, sizeof(FILE_MANIFEST));
    if (!m)
    {
        free(filename);
        return NULL;
    }

    memcpy(m->manifest_hash, stored_hash, WH_HASH_SIZE);
    m->file_size = file_size;
    m->chunk_size = chunk_size;
    m->chunk_count = chunk_count;
    m->filename = filename;
    m->filename_length = filename_length;

    if (chunk_count > 0)
    {
        m->chunks = (CHUNK_INFO *)calloc(chunk_count, sizeof(CHUNK_INFO));
        if (!m->chunks)
        {
            Manifest_Destroy(m);
            return NULL;
        }

        for (uint32_t i = 0; i < chunk_count; i++)
        {
            memcpy(m->chunks[i].hash, p, WH_HASH_SIZE);
            p += WH_HASH_SIZE;
            m->chunks[i].chunk_size = ReadUint32LE(p);
            p += 4;
        }
    }

    return m;
}
