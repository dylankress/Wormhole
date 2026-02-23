//
// transfer_state.c
// Persist and resume partial transfer state at ~/.wormhole/transfers/.
// by Dylan Kress
//

#include "transfer_state.h"
#include "wire_format.h"

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <errno.h>
#endif

//=============================================================================
// Internal helpers
//=============================================================================

static void HashToHex(const uint8_t hash[32], char hex[64])
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++)
    {
        hex[i * 2]     = digits[(hash[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = digits[hash[i] & 0x0F];
    }
}

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

// Build path: ~/.wormhole/transfers/<manifest_hash_hex>.state
// Also ensures ~/.wormhole/ and ~/.wormhole/transfers/ directories exist.
static BOOLEAN GetStatePath(const uint8_t manifest_hash[32],
                            char *path, size_t path_len)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home) return FALSE;

    char wormhole_dir[MAX_PATH];
    char transfers_dir[MAX_PATH];
    snprintf(wormhole_dir, sizeof(wormhole_dir), "%s\\.wormhole", home);
    snprintf(transfers_dir, sizeof(transfers_dir), "%s\\.wormhole\\transfers", home);
#else
    const char *home = getenv("HOME");
    if (!home) return FALSE;

    char wormhole_dir[MAX_PATH];
    char transfers_dir[MAX_PATH];
    snprintf(wormhole_dir, sizeof(wormhole_dir), "%s/.wormhole", home);
    snprintf(transfers_dir, sizeof(transfers_dir), "%s/.wormhole/transfers", home);
#endif

    if (!EnsureDir(wormhole_dir)) return FALSE;
    if (!EnsureDir(transfers_dir)) return FALSE;

    char hex[65];
    HashToHex(manifest_hash, hex);
    hex[64] = '\0';

#ifdef _WIN32
    snprintf(path, path_len, "%s\\%s.state", transfers_dir, hex);
#else
    snprintf(path, path_len, "%s/%s.state", transfers_dir, hex);
#endif
    return TRUE;
}

//=============================================================================
// Public API
//=============================================================================

BOOLEAN TransferState_Save(const uint8_t manifest_hash[32],
                           const BOOLEAN *chunks_received, uint32_t total_chunks)
{
    if (!chunks_received || total_chunks == 0) return FALSE;
    if (total_chunks > 0x1FFFFFFF) return FALSE;  // ~500M chunks = ~128TB at 256KB

    char path[MAX_PATH];
    if (!GetStatePath(manifest_hash, path, sizeof(path))) return FALSE;

    // Pack BOOLEAN array into compact bitmask
    uint32_t bitmask_bytes = (total_chunks + 7) / 8;
    uint32_t file_size = 4 + bitmask_bytes;

    uint8_t *buffer = (uint8_t *)calloc(1, file_size);
    if (!buffer) return FALSE;

    WriteUint32LE(buffer, total_chunks);
    for (uint32_t i = 0; i < total_chunks; i++)
    {
        if (chunks_received[i])
            buffer[4 + i / 8] |= (1 << (i % 8));
    }

    FILE *fh = fopen(path, "wb");
    if (!fh)
    {
        LOG_ERROR("[TransferState] ERROR: Cannot open %s for writing\n", path);
        free(buffer);
        return FALSE;
    }

    size_t written = fwrite(buffer, 1, file_size, fh);
    fclose(fh);
    free(buffer);

    return (written == file_size);
}

BOOLEAN TransferState_Load(const uint8_t manifest_hash[32],
                           BOOLEAN **chunks_received_out, uint32_t *total_chunks_out)
{
    if (!chunks_received_out || !total_chunks_out) return FALSE;

    char path[MAX_PATH];
    if (!GetStatePath(manifest_hash, path, sizeof(path))) return FALSE;

    FILE *fh = fopen(path, "rb");
    if (!fh) return FALSE;

    // Read header: [4B total_chunks]
    uint8_t header[4];
    if (fread(header, 1, 4, fh) != 4)
    {
        fclose(fh);
        return FALSE;
    }

    uint32_t total_chunks = ReadUint32LE(header);
    if (total_chunks == 0)
    {
        fclose(fh);
        return FALSE;
    }
    if (total_chunks > 0x1FFFFFFF)  // ~500M chunks = ~128TB at 256KB
    {
        fclose(fh);
        return FALSE;
    }

    // Read bitmask
    uint32_t bitmask_bytes = (total_chunks + 7) / 8;
    uint8_t *bitmask = (uint8_t *)malloc(bitmask_bytes);
    if (!bitmask)
    {
        fclose(fh);
        return FALSE;
    }

    if (fread(bitmask, 1, bitmask_bytes, fh) != bitmask_bytes)
    {
        free(bitmask);
        fclose(fh);
        return FALSE;
    }
    fclose(fh);

    // Unpack bitmask into BOOLEAN array
    BOOLEAN *received = (BOOLEAN *)calloc(total_chunks, sizeof(BOOLEAN));
    if (!received)
    {
        free(bitmask);
        return FALSE;
    }

    for (uint32_t i = 0; i < total_chunks; i++)
    {
        if (bitmask[i / 8] & (1 << (i % 8)))
            received[i] = TRUE;
    }

    free(bitmask);
    *chunks_received_out = received;
    *total_chunks_out = total_chunks;
    return TRUE;
}

void TransferState_Delete(const uint8_t manifest_hash[32])
{
    char path[MAX_PATH];
    if (GetStatePath(manifest_hash, path, sizeof(path)))
    {
        remove(path);
        LOG("[TransferState] Deleted state file\n");
    }
}
