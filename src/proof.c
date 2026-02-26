//
// proof.c
// Proof-of-storage: Blake3(seed || chunk_data) proves possession.
// by Dylan Kress
//

#include "proof.h"
#include <blake3.h>
#include <sodium.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <errno.h>
#endif

//=============================================================================
// Internal helpers
//=============================================================================

// Ensure a directory exists (create if needed).
static BOOLEAN EnsureDir(const char *dir)
{
#ifdef _WIN32
    if (CreateDirectoryA(dir, NULL))
        return TRUE;
    return (GetLastError() == ERROR_ALREADY_EXISTS);
#else
    if (mkdir(dir, 0700) == 0)
        return TRUE;
    return (errno == EEXIST);
#endif
}

// Build the proofs base path: ~/.wormhole/proofs
static BOOLEAN GetProofsBasePath(char *path, size_t path_len)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home) return FALSE;
    snprintf(path, path_len, "%s\\.wormhole\\proofs", home);
#else
    const char *home = getenv("HOME");
    if (!home) return FALSE;
    snprintf(path, path_len, "%s/.wormhole/proofs", home);
#endif
    return TRUE;
}

// Build the full cache file path for a chunk: ~/.wormhole/proofs/<hex_hash>
static BOOLEAN GetProofCachePath(const uint8_t hash[WH_HASH_SIZE],
                                  char *path, size_t path_len)
{
    char base[MAX_PATH];
    if (!GetProofsBasePath(base, sizeof(base))) return FALSE;

    char hex[65];
    WH_HashToHex(hash, hex);

#ifdef _WIN32
    snprintf(path, path_len, "%s\\%s", base, hex);
#else
    snprintf(path, path_len, "%s/%s", base, hex);
#endif
    return TRUE;
}

// Ensure the proofs directory exists, creating parent dirs as needed.
static BOOLEAN EnsureProofsDir(void)
{
    char wormhole_dir[MAX_PATH];
    char proofs_dir[MAX_PATH];

#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home) return FALSE;
    snprintf(wormhole_dir, sizeof(wormhole_dir), "%s\\.wormhole", home);
#else
    const char *home = getenv("HOME");
    if (!home) return FALSE;
    snprintf(wormhole_dir, sizeof(wormhole_dir), "%s/.wormhole", home);
#endif

    if (!EnsureDir(wormhole_dir)) return FALSE;
    if (!GetProofsBasePath(proofs_dir, sizeof(proofs_dir))) return FALSE;
    return EnsureDir(proofs_dir);
}

// Cache file format: [4B count][count * {32B seed, 32B proof}]
#define PROOF_ENTRY_SIZE (WH_HASH_SIZE + WH_HASH_SIZE)  // 64 bytes per entry

//=============================================================================
// Public API
//=============================================================================

void Proof_Compute(const uint8_t *chunk_data, uint32_t size,
                   const uint8_t seed[WH_HASH_SIZE],
                   uint8_t proof_out[WH_HASH_SIZE])
{
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, seed, WH_HASH_SIZE);
    blake3_hasher_update(&hasher, chunk_data, size);
    blake3_hasher_finalize(&hasher, proof_out, WH_HASH_SIZE);
}

BOOLEAN Proof_PrecomputeAndCache(const uint8_t hash[WH_HASH_SIZE],
                                  const uint8_t *chunk_data, uint32_t size)
{
    if (!chunk_data || size == 0) return FALSE;

    if (!EnsureProofsDir()) return FALSE;

    char path[MAX_PATH];
    if (!GetProofCachePath(hash, path, sizeof(path))) return FALSE;

    FILE *fh = fopen(path, "wb");
    if (!fh) return FALSE;

    // Write count
    uint32_t count = PROOF_CACHE_COUNT;
    uint8_t count_buf[4];
    count_buf[0] = (uint8_t)(count);
    count_buf[1] = (uint8_t)(count >> 8);
    count_buf[2] = (uint8_t)(count >> 16);
    count_buf[3] = (uint8_t)(count >> 24);
    if (fwrite(count_buf, 1, 4, fh) != 4)
    {
        fclose(fh);
        return FALSE;
    }

    // Generate seeds and compute proofs
    for (uint32_t i = 0; i < PROOF_CACHE_COUNT; i++)
    {
        uint8_t seed[WH_HASH_SIZE];
        uint8_t proof[WH_HASH_SIZE];

        randombytes_buf(seed, WH_HASH_SIZE);
        Proof_Compute(chunk_data, size, seed, proof);

        if (fwrite(seed, 1, WH_HASH_SIZE, fh) != WH_HASH_SIZE ||
            fwrite(proof, 1, WH_HASH_SIZE, fh) != WH_HASH_SIZE)
        {
            fclose(fh);
            return FALSE;
        }
    }

    fclose(fh);
    return TRUE;
}

BOOLEAN Proof_LookupCached(const uint8_t hash[WH_HASH_SIZE],
                            const uint8_t seed[WH_HASH_SIZE],
                            uint8_t proof_out[WH_HASH_SIZE])
{
    char path[MAX_PATH];
    if (!GetProofCachePath(hash, path, sizeof(path))) return FALSE;

    FILE *fh = fopen(path, "rb");
    if (!fh) return FALSE;

    // Read count
    uint8_t count_buf[4];
    if (fread(count_buf, 1, 4, fh) != 4)
    {
        fclose(fh);
        return FALSE;
    }

    uint32_t count = (uint32_t)count_buf[0] |
                     ((uint32_t)count_buf[1] << 8) |
                     ((uint32_t)count_buf[2] << 16) |
                     ((uint32_t)count_buf[3] << 24);

    // Cap to prevent unbounded reads from corrupted files
    if (count > 256)
    {
        fclose(fh);
        return FALSE;
    }

    // Linear scan for matching seed
    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t cached_seed[WH_HASH_SIZE];
        uint8_t cached_proof[WH_HASH_SIZE];

        if (fread(cached_seed, 1, WH_HASH_SIZE, fh) != WH_HASH_SIZE ||
            fread(cached_proof, 1, WH_HASH_SIZE, fh) != WH_HASH_SIZE)
        {
            fclose(fh);
            return FALSE;
        }

        if (memcmp(cached_seed, seed, WH_HASH_SIZE) == 0)
        {
            memcpy(proof_out, cached_proof, WH_HASH_SIZE);
            fclose(fh);
            return TRUE;
        }
    }

    fclose(fh);
    return FALSE;
}

void Proof_DeleteCache(const uint8_t hash[WH_HASH_SIZE])
{
    char path[MAX_PATH];
    if (!GetProofCachePath(hash, path, sizeof(path))) return;
    remove(path);
}
