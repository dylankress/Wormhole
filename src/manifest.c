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
// Manifest Wire Format (Version 1 — single file)
//=============================================================================
//
// [4B]  magic            (WH_MANIFEST_MAGIC)
// [1B]  version          (1)
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
//=============================================================================
// Manifest Wire Format (Version 2 — multi-file / directory)
//=============================================================================
//
// [4B]  magic            (WH_MANIFEST_MAGIC)
// [1B]  version          (2)
// [32B] manifest_hash    (Blake3 of everything after this field)
// --- body (hashed region) ---
// [8B]  file_size         (total across all files)
// [4B]  chunk_size        (WH_CHUNK_SIZE)
// [4B]  chunk_count       (total)
// [2B]  filename_length   (directory name)
// [NB]  filename          (directory name)
// [4B]  file_count
// Per file (file_count times):
//   [2B]  path_length
//   [NB]  relative_path   (UTF-8, '/' separator)
//   [8B]  file_size
//   [4B]  chunk_start
//   [4B]  chunk_count
// Per chunk (total chunk_count times):
//   [32B] chunk_hash
//   [4B]  chunk_size

#define MANIFEST_HEADER_SIZE  (4 + 1 + WH_HASH_SIZE)  // magic + version + hash
#define MANIFEST_BODY_FIXED   (8 + 4 + 4 + 2)         // file_size + chunk_size + chunk_count + filename_len
#define MANIFEST_PER_CHUNK    (WH_HASH_SIZE + 4)       // hash + chunk_size
#define MANIFEST_FILE_ENTRY_FIXED (2 + 8 + 4 + 4)     // path_len + file_size + chunk_start + chunk_count

//=============================================================================
// Helper: compute body size for a manifest
//=============================================================================

static size_t ComputeBodySize(const FILE_MANIFEST *m)
{
    size_t size = MANIFEST_BODY_FIXED + m->filename_length +
                  (size_t)m->chunk_count * MANIFEST_PER_CHUNK;

    if (m->version == WH_MANIFEST_VERSION_2)
    {
        size += 4;  // file_count field
        for (uint32_t i = 0; i < m->file_count; i++)
        {
            size += MANIFEST_FILE_ENTRY_FIXED + m->files[i].path_length;
        }
    }

    return size;
}

//=============================================================================
// Helper: serialize body into buffer (for hashing and serialization)
//=============================================================================

static void SerializeBody(const FILE_MANIFEST *m, uint8_t *body)
{
    uint8_t *p = body;

    WriteUint64LE(p, m->file_size);   p += 8;
    WriteUint32LE(p, m->chunk_size);  p += 4;
    WriteUint32LE(p, m->chunk_count); p += 4;
    WriteUint16LE(p, m->filename_length); p += 2;
    memcpy(p, m->filename, m->filename_length);
    p += m->filename_length;

    // Version 2: file entries
    if (m->version == WH_MANIFEST_VERSION_2)
    {
        WriteUint32LE(p, m->file_count); p += 4;
        for (uint32_t i = 0; i < m->file_count; i++)
        {
            const FILE_ENTRY *fe = &m->files[i];
            WriteUint16LE(p, fe->path_length);  p += 2;
            memcpy(p, fe->relative_path, fe->path_length);
            p += fe->path_length;
            WriteUint64LE(p, fe->file_size);    p += 8;
            WriteUint32LE(p, fe->chunk_start);  p += 4;
            WriteUint32LE(p, fe->chunk_count);  p += 4;
        }
    }

    // Chunk hashes
    for (uint32_t i = 0; i < m->chunk_count; i++)
    {
        memcpy(p, m->chunks[i].hash, WH_HASH_SIZE);
        p += WH_HASH_SIZE;
        WriteUint32LE(p, m->chunks[i].chunk_size);
        p += 4;
    }
}

//=============================================================================
// Public API — creation
//=============================================================================

FILE_MANIFEST *Manifest_Create(const char *filename, uint64_t file_size)
{
    if (!filename) return NULL;

    FILE_MANIFEST *m = (FILE_MANIFEST *)calloc(1, sizeof(FILE_MANIFEST));
    if (!m) return NULL;

    m->version = WH_MANIFEST_VERSION;
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

FILE_MANIFEST *Manifest_CreateMultiFile(const char *dirname,
                                        const char **relative_paths,
                                        const uint64_t *file_sizes,
                                        uint32_t file_count)
{
    if (!dirname || !relative_paths || !file_sizes || file_count == 0)
        return NULL;

    FILE_MANIFEST *m = (FILE_MANIFEST *)calloc(1, sizeof(FILE_MANIFEST));
    if (!m) return NULL;

    m->version = WH_MANIFEST_VERSION_2;
    m->chunk_size = WH_CHUNK_SIZE;

    // Copy directory name
    m->filename_length = (uint16_t)strlen(dirname);
    m->filename = (char *)malloc(m->filename_length + 1);
    if (!m->filename) { free(m); return NULL; }
    memcpy(m->filename, dirname, m->filename_length);
    m->filename[m->filename_length] = '\0';

    // Allocate file entries
    m->file_count = file_count;
    m->files = (FILE_ENTRY *)calloc(file_count, sizeof(FILE_ENTRY));
    if (!m->files)
    {
        free(m->filename);
        free(m);
        return NULL;
    }

    // Compute chunk ranges and total size
    uint32_t total_chunks = 0;
    uint64_t total_size = 0;

    for (uint32_t i = 0; i < file_count; i++)
    {
        FILE_ENTRY *fe = &m->files[i];

        // Copy relative path
        fe->path_length = (uint16_t)strlen(relative_paths[i]);
        fe->relative_path = (char *)malloc(fe->path_length + 1);
        if (!fe->relative_path)
        {
            Manifest_Destroy(m);
            return NULL;
        }
        memcpy(fe->relative_path, relative_paths[i], fe->path_length);
        fe->relative_path[fe->path_length] = '\0';

        fe->file_size = file_sizes[i];
        fe->chunk_start = total_chunks;

        if (file_sizes[i] == 0)
        {
            fe->chunk_count = 0;
        }
        else
        {
            fe->chunk_count = (uint32_t)((file_sizes[i] + WH_CHUNK_SIZE - 1) / WH_CHUNK_SIZE);
        }

        total_chunks += fe->chunk_count;
        total_size += file_sizes[i];
    }

    m->chunk_count = total_chunks;
    m->file_size = total_size;

    // Allocate chunk info array
    if (total_chunks > 0)
    {
        m->chunks = (CHUNK_INFO *)calloc(total_chunks, sizeof(CHUNK_INFO));
        if (!m->chunks)
        {
            Manifest_Destroy(m);
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
    if (manifest->files)
    {
        for (uint32_t i = 0; i < manifest->file_count; i++)
        {
            if (manifest->files[i].relative_path)
                free(manifest->files[i].relative_path);
        }
        free(manifest->files);
    }
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

    size_t body_size = ComputeBodySize(manifest);
    uint8_t *body = (uint8_t *)malloc(body_size);
    if (!body) return;

    SerializeBody(manifest, body);

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, body, body_size);
    blake3_hasher_finalize(&hasher, manifest->manifest_hash, WH_HASH_SIZE);

    free(body);
}

uint8_t *Manifest_Serialize(const FILE_MANIFEST *manifest, size_t *out_size)
{
    if (!manifest || !out_size) return NULL;

    size_t body_size = ComputeBodySize(manifest);
    size_t total_size = MANIFEST_HEADER_SIZE + body_size;

    uint8_t *buf = (uint8_t *)malloc(total_size);
    if (!buf) return NULL;

    uint8_t *p = buf;

    // Header
    WriteUint32LE(p, WH_MANIFEST_MAGIC);    p += 4;
    *p = manifest->version;                 p += 1;
    memcpy(p, manifest->manifest_hash, WH_HASH_SIZE);
    p += WH_HASH_SIZE;

    // Body
    SerializeBody(manifest, p);

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
    if (version != WH_MANIFEST_VERSION && version != WH_MANIFEST_VERSION_2)
        return NULL;

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

    if (filename_length == 0) return NULL;

    // Check we have enough data for the filename
    if ((size_t)(p - data) + filename_length > size) return NULL;

    // Read filename
    char *filename = (char *)malloc(filename_length + 1);
    if (!filename) return NULL;
    memcpy(filename, p, filename_length);
    filename[filename_length] = '\0';
    p += filename_length;

    // Version 2: parse file entries
    uint32_t fc = 0;
    FILE_ENTRY *files = NULL;

    if (version == WH_MANIFEST_VERSION_2)
    {
        if ((size_t)(p - data) + 4 > size) { free(filename); return NULL; }
        fc = ReadUint32LE(p); p += 4;

        if (fc > 0)
        {
            files = (FILE_ENTRY *)calloc(fc, sizeof(FILE_ENTRY));
            if (!files) { free(filename); return NULL; }

            for (uint32_t i = 0; i < fc; i++)
            {
                if ((size_t)(p - data) + MANIFEST_FILE_ENTRY_FIXED > size)
                {
                    // Cleanup partially parsed entries
                    for (uint32_t j = 0; j < i; j++)
                        if (files[j].relative_path) free(files[j].relative_path);
                    free(files);
                    free(filename);
                    return NULL;
                }

                files[i].path_length = ReadUint16LE(p); p += 2;

                if ((size_t)(p - data) + files[i].path_length + 8 + 4 + 4 > size)
                {
                    for (uint32_t j = 0; j < i; j++)
                        if (files[j].relative_path) free(files[j].relative_path);
                    free(files);
                    free(filename);
                    return NULL;
                }

                files[i].relative_path = (char *)malloc(files[i].path_length + 1);
                if (!files[i].relative_path)
                {
                    for (uint32_t j = 0; j < i; j++)
                        if (files[j].relative_path) free(files[j].relative_path);
                    free(files);
                    free(filename);
                    return NULL;
                }
                memcpy(files[i].relative_path, p, files[i].path_length);
                files[i].relative_path[files[i].path_length] = '\0';
                p += files[i].path_length;

                files[i].file_size = ReadUint64LE(p);    p += 8;
                files[i].chunk_start = ReadUint32LE(p);  p += 4;
                files[i].chunk_count = ReadUint32LE(p);  p += 4;
            }
        }
    }

    // Compute expected body size and validate
    // We need to calculate it manually here since we don't have a manifest struct yet
    size_t expected_body = MANIFEST_BODY_FIXED + filename_length +
                           (size_t)chunk_count * MANIFEST_PER_CHUNK;
    if (version == WH_MANIFEST_VERSION_2)
    {
        expected_body += 4;  // file_count
        for (uint32_t i = 0; i < fc; i++)
            expected_body += MANIFEST_FILE_ENTRY_FIXED + files[i].path_length;
    }

    if (size < MANIFEST_HEADER_SIZE + expected_body)
    {
        if (files)
        {
            for (uint32_t j = 0; j < fc; j++)
                if (files[j].relative_path) free(files[j].relative_path);
            free(files);
        }
        free(filename);
        return NULL;
    }

    // Verify hash of body
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, body_start, expected_body);
    uint8_t computed_hash[WH_HASH_SIZE];
    blake3_hasher_finalize(&hasher, computed_hash, WH_HASH_SIZE);

    if (memcmp(stored_hash, computed_hash, WH_HASH_SIZE) != 0)
    {
        if (files)
        {
            for (uint32_t j = 0; j < fc; j++)
                if (files[j].relative_path) free(files[j].relative_path);
            free(files);
        }
        free(filename);
        return NULL;
    }

    // Build manifest
    FILE_MANIFEST *m = (FILE_MANIFEST *)calloc(1, sizeof(FILE_MANIFEST));
    if (!m)
    {
        if (files)
        {
            for (uint32_t j = 0; j < fc; j++)
                if (files[j].relative_path) free(files[j].relative_path);
            free(files);
        }
        free(filename);
        return NULL;
    }

    memcpy(m->manifest_hash, stored_hash, WH_HASH_SIZE);
    m->version = version;
    m->file_size = file_size;
    m->chunk_size = chunk_size;
    m->chunk_count = chunk_count;
    m->filename = filename;
    m->filename_length = filename_length;
    m->file_count = fc;
    m->files = files;

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
