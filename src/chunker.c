//
// chunker.c
// File chunking with Blake3 hashing to build a FILE_MANIFEST.
// by Dylan Kress
//

#include "chunker.h"
#include "file_io.h"
#include <blake3.h>
#include <stdio.h>

FILE_MANIFEST *Chunker_BuildManifest(const char *file_path)
{
    if (!file_path) return NULL;

    // Get file size
    uint64_t file_size = 0;
    if (!GetWormholeFileSize(file_path, &file_size))
    {
        LOG_ERROR("[Chunker] ERROR: Cannot get file size: %s\n", file_path);
        return NULL;
    }

    // Extract filename
    char *filename = NULL;
    uint32_t filename_length = 0;
    ExtractFilename(file_path, &filename, &filename_length);
    if (!filename)
    {
        LOG_ERROR("[Chunker] ERROR: Cannot extract filename: %s\n", file_path);
        return NULL;
    }

    // Create manifest
    FILE_MANIFEST *manifest = Manifest_Create(filename, file_size);
    free(filename);
    if (!manifest)
    {
        LOG_ERROR("[Chunker] ERROR: Failed to create manifest\n");
        return NULL;
    }

    // Handle empty file (no chunks to process)
    if (file_size == 0)
    {
        Manifest_ComputeHash(manifest);
        return manifest;
    }

    // Open file
    FILE *fh = NULL;
    if (!OpenFileForRead(file_path, &fh))
    {
        LOG_ERROR("[Chunker] ERROR: Cannot open file: %s\n", file_path);
        Manifest_Destroy(manifest);
        return NULL;
    }

    // Read and hash each chunk
    uint8_t *chunk_buf = (uint8_t *)malloc(WH_CHUNK_SIZE);
    if (!chunk_buf)
    {
        LOG_ERROR("[Chunker] ERROR: Failed to allocate chunk buffer\n");
        CloseFile(fh);
        Manifest_Destroy(manifest);
        return NULL;
    }

    for (uint32_t i = 0; i < manifest->chunk_count; i++)
    {
        // Calculate how much to read for this chunk
        uint64_t offset = (uint64_t)i * WH_CHUNK_SIZE;
        uint64_t remaining = file_size - offset;
        size_t to_read = (remaining > WH_CHUNK_SIZE) ? WH_CHUNK_SIZE : (size_t)remaining;

        size_t bytes_read = 0;
        if (!ReadFileChunk(fh, chunk_buf, to_read, &bytes_read) || bytes_read != to_read)
        {
            LOG_ERROR("[Chunker] ERROR: Failed to read chunk %u\n", i);
            free(chunk_buf);
            CloseFile(fh);
            Manifest_Destroy(manifest);
            return NULL;
        }

        // Blake3 hash the chunk
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, chunk_buf, bytes_read);
        uint8_t hash[WH_HASH_SIZE];
        blake3_hasher_finalize(&hasher, hash, WH_HASH_SIZE);

        Manifest_AddChunk(manifest, i, hash, (uint32_t)bytes_read);
    }

    free(chunk_buf);
    CloseFile(fh);

    // Compute overall manifest hash
    Manifest_ComputeHash(manifest);

    return manifest;
}
