//
// chunk_store.c
// Content-addressed chunk storage: ~/.wormhole/store/XX/YYYYYY...
// by Dylan Kress
//

#include "chunk_store.h"
#include "file_io.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

//=============================================================================
// Internal helpers
//=============================================================================

// Convert a 32-byte hash to a 64-char hex string (no null terminator added).
static void HashToHex(const uint8_t hash[WH_HASH_SIZE], char hex[64])
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < WH_HASH_SIZE; i++)
    {
        hex[i * 2]     = digits[(hash[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = digits[hash[i] & 0x0F];
    }
}

// Build the store base path: %USERPROFILE%\.wormhole\store (Windows)
// or $HOME/.wormhole/store (POSIX).
static BOOLEAN GetStoreBasePath(char *path, size_t path_len)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home) return FALSE;
    snprintf(path, path_len, "%s\\.wormhole\\store", home);
#else
    const char *home = getenv("HOME");
    if (!home) return FALSE;
    snprintf(path, path_len, "%s/.wormhole/store", home);
#endif
    return TRUE;
}

// Build the full path for a chunk: base/XX/YYYYYY...
// hex must be a 64-char buffer.
static BOOLEAN GetChunkPath(const uint8_t hash[WH_HASH_SIZE],
                            char *path, size_t path_len)
{
    char base[MAX_PATH];
    if (!GetStoreBasePath(base, sizeof(base))) return FALSE;

    char hex[65];
    HashToHex(hash, hex);
    hex[64] = '\0';

#ifdef _WIN32
    snprintf(path, path_len, "%s\\%.2s\\%s", base, hex, hex + 2);
#else
    snprintf(path, path_len, "%s/%.2s/%s", base, hex, hex + 2);
#endif
    return TRUE;
}

// Ensure a directory exists (create if needed).
static BOOLEAN EnsureDir(const char *dir)
{
#ifdef _WIN32
    if (CreateDirectoryA(dir, NULL))
        return TRUE;
    // Already exists is fine
    return (GetLastError() == ERROR_ALREADY_EXISTS);
#else
    if (mkdir(dir, 0755) == 0)
        return TRUE;
    return (errno == EEXIST);
#endif
}

//=============================================================================
// Public API
//=============================================================================

BOOLEAN ChunkStore_Init(void)
{
    char base[MAX_PATH];
    if (!GetStoreBasePath(base, sizeof(base))) return FALSE;

    // Create ~/.wormhole
    char wormhole_dir[MAX_PATH];
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
    if (!EnsureDir(base)) return FALSE;

    return TRUE;
}

BOOLEAN ChunkStore_Put(const uint8_t hash[WH_HASH_SIZE],
                       const uint8_t *data, uint32_t size)
{
    if (!data || size == 0) return FALSE;

    // Skip if already stored
    if (ChunkStore_Has(hash)) return TRUE;

    char path[MAX_PATH];
    if (!GetChunkPath(hash, path, sizeof(path))) return FALSE;

    // Ensure the 2-char prefix directory exists
    char base[MAX_PATH];
    if (!GetStoreBasePath(base, sizeof(base))) return FALSE;

    char hex[65];
    HashToHex(hash, hex);
    hex[64] = '\0';

    char prefix_dir[MAX_PATH];
#ifdef _WIN32
    snprintf(prefix_dir, sizeof(prefix_dir), "%s\\%.2s", base, hex);
#else
    snprintf(prefix_dir, sizeof(prefix_dir), "%s/%.2s", base, hex);
#endif
    if (!EnsureDir(prefix_dir)) return FALSE;

    // Write chunk data
    FILE *fh = fopen(path, "wb");
    if (!fh) return FALSE;

    size_t written = fwrite(data, 1, size, fh);
    fclose(fh);

    return (written == size);
}

BOOLEAN ChunkStore_Has(const uint8_t hash[WH_HASH_SIZE])
{
    char path[MAX_PATH];
    if (!GetChunkPath(hash, path, sizeof(path))) return FALSE;
    return FileExists(path);
}

BOOLEAN ChunkStore_Get(const uint8_t hash[WH_HASH_SIZE],
                       uint8_t *data_out, uint32_t *size_out)
{
    if (!data_out || !size_out) return FALSE;

    char path[MAX_PATH];
    if (!GetChunkPath(hash, path, sizeof(path))) return FALSE;

    FILE *fh = fopen(path, "rb");
    if (!fh) return FALSE;

    // Get file size
#ifdef _WIN32
    _fseeki64(fh, 0, SEEK_END);
    __int64 fsize = _ftelli64(fh);
    _fseeki64(fh, 0, SEEK_SET);
#else
    fseeko(fh, 0, SEEK_END);
    off_t fsize = ftello(fh);
    fseeko(fh, 0, SEEK_SET);
#endif

    if (fsize <= 0 || (uint64_t)fsize > WH_CHUNK_SIZE)
    {
        fclose(fh);
        return FALSE;
    }

    size_t read = fread(data_out, 1, (size_t)fsize, fh);
    fclose(fh);

    if (read != (size_t)fsize) return FALSE;

    *size_out = (uint32_t)read;
    return TRUE;
}
