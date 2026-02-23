//
// chunk_store.c
// Content-addressed chunk storage: ~/.wormhole/store/XX/YYYYYY...
// by Dylan Kress
//

#include "chunk_store.h"
#include "config.h"
#include "file_io.h"
#include "proof.h"
#include <blake3.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#endif

static WH_MUTEX g_store_lock;

#ifdef _WIN32
static INIT_ONCE g_store_lock_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK InitStoreLock(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *lpContext)
{
    (void)InitOnce;
    (void)Parameter;
    (void)lpContext;
    WH_MUTEX_INIT(g_store_lock);
    return TRUE;
}

static void EnsureStoreLock(void)
{
    InitOnceExecuteOnce(&g_store_lock_once, InitStoreLock, NULL, NULL);
}
#else
static pthread_once_t g_store_lock_once = PTHREAD_ONCE_INIT;

static void InitStoreLock(void)
{
    WH_MUTEX_INIT(g_store_lock);
}

static void EnsureStoreLock(void)
{
    pthread_once(&g_store_lock_once, InitStoreLock);
}
#endif

//=============================================================================
// Storage quota tracking
//=============================================================================

static uint64_t g_total_store_bytes = 0;
static BOOLEAN  g_quota_initialized = FALSE;

static uint64_t g_quota_override = 0;  // 0 = use config, >0 = override (for testing)

// Lazily initialize the total store size counter by scanning the store directory.
// Must be called under g_store_lock or before concurrent access.
static void EnsureQuotaInitialized(void)
{
    if (g_quota_initialized) return;
    g_total_store_bytes = ChunkStore_GetTotalSize();
    g_quota_initialized = TRUE;
}

uint64_t ChunkStore_GetQuotaBytes(void)
{
    if (g_quota_override > 0) return g_quota_override;

    // Read from default config
    WORMHOLE_CONFIG *config = Config_LoadDefault();
    if (!config) return (uint64_t)CONFIG_DEFAULT_MAX_STORAGE_GB * 1024ULL * 1024ULL * 1024ULL;

    uint64_t gb = Config_GetUint64(config, "max_storage_gb", CONFIG_DEFAULT_MAX_STORAGE_GB);
    Config_Destroy(config);
    return gb * 1024ULL * 1024ULL * 1024ULL;
}

void ChunkStore_SetQuotaBytes(uint64_t bytes)
{
    g_quota_override = bytes;
}

void ChunkStore_ResetQuotaTracking(void)
{
    g_total_store_bytes = ChunkStore_GetTotalSize();
    g_quota_initialized = TRUE;
    g_quota_override = 0;
}

//=============================================================================
// Internal helpers
//=============================================================================

// Forward declarations (defined later in the file)
static BOOLEAN GetReplicaBasePath(char *path, size_t path_len);
static BOOLEAN GetAccessTimePath(char *path, size_t path_len);
void ChunkStore_SetAccessTime(const uint8_t hash[WH_HASH_SIZE]);

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
    if (mkdir(dir, 0700) == 0)
        return TRUE;
    return (errno == EEXIST);
#endif
}

//=============================================================================
// Public API
//=============================================================================

BOOLEAN ChunkStore_Init(void)
{
    EnsureStoreLock();
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

    // Create replicas directory for replica metadata
    char replicas_dir[MAX_PATH];
    if (GetReplicaBasePath(replicas_dir, sizeof(replicas_dir)))
    {
        EnsureDir(replicas_dir);
    }

    return TRUE;
}

BOOLEAN ChunkStore_Put(const uint8_t hash[WH_HASH_SIZE],
                       const uint8_t *data, uint32_t size)
{
    if (!data || size == 0) return FALSE;

    // Skip if already stored
    if (ChunkStore_Has(hash)) return TRUE;

    // Enforce storage quota
    EnsureStoreLock();
    WH_MUTEX_LOCK(g_store_lock);
    EnsureQuotaInitialized();

    uint64_t max_bytes = ChunkStore_GetQuotaBytes();
    if (max_bytes > 0 && g_total_store_bytes + size > max_bytes)
    {
        LOG_ERROR("[chunk_store] Storage quota exceeded (%llu + %u > %llu bytes)\n",
                  (unsigned long long)g_total_store_bytes, size,
                  (unsigned long long)max_bytes);
        WH_MUTEX_UNLOCK(g_store_lock);
        return FALSE;
    }
    WH_MUTEX_UNLOCK(g_store_lock);

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

    if (written == size)
    {
        WH_MUTEX_LOCK(g_store_lock);
        g_total_store_bytes += size;
        WH_MUTEX_UNLOCK(g_store_lock);
        return TRUE;
    }

    return FALSE;
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

    // Verify Blake3 hash to detect on-disk corruption
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data_out, read);
    uint8_t computed_hash[WH_HASH_SIZE];
    blake3_hasher_finalize(&hasher, computed_hash, WH_HASH_SIZE);
    if (memcmp(computed_hash, hash, WH_HASH_SIZE) != 0)
    {
        LOG_ERROR("[ChunkStore] ERROR: Chunk data corrupted on disk (hash mismatch)\n");
        return FALSE;
    }

    *size_out = (uint32_t)read;

    // Update access time for LRU tracking
    ChunkStore_SetAccessTime(hash);

    return TRUE;
}

//=============================================================================
// Replica Metadata
//=============================================================================

// Build the replica metadata directory path: ~/.wormhole/replicas
static BOOLEAN GetReplicaBasePath(char *path, size_t path_len)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home) return FALSE;
    snprintf(path, path_len, "%s\\.wormhole\\replicas", home);
#else
    const char *home = getenv("HOME");
    if (!home) return FALSE;
    snprintf(path, path_len, "%s/.wormhole/replicas", home);
#endif
    return TRUE;
}

// Build the full path for a chunk's replica file: replicas/XX/YYYYYY...
static BOOLEAN GetReplicaPath(const uint8_t hash[WH_HASH_SIZE],
                               char *path, size_t path_len)
{
    char base[MAX_PATH];
    if (!GetReplicaBasePath(base, sizeof(base))) return FALSE;

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

// Ensure the replica directory structure exists
static BOOLEAN EnsureReplicaDirs(const uint8_t hash[WH_HASH_SIZE])
{
    char base[MAX_PATH];
    if (!GetReplicaBasePath(base, sizeof(base))) return FALSE;

    // Ensure ~/.wormhole/replicas/
    if (!EnsureDir(base)) return FALSE;

    // Ensure prefix subdir
    char hex[65];
    HashToHex(hash, hex);
    hex[64] = '\0';

    char prefix_dir[MAX_PATH];
#ifdef _WIN32
    snprintf(prefix_dir, sizeof(prefix_dir), "%s\\%.2s", base, hex);
#else
    snprintf(prefix_dir, sizeof(prefix_dir), "%s/%.2s", base, hex);
#endif
    return EnsureDir(prefix_dir);
}

// Replica file format: [4B peer_count][peer_count × 32B peer_id]

BOOLEAN ChunkStore_PutWithMeta(const uint8_t hash[WH_HASH_SIZE],
                                const uint8_t *data, uint32_t size,
                                const uint8_t source_peer_id[32])
{
    // Store the chunk data using existing Put
    if (!ChunkStore_Put(hash, data, size))
    {
        return FALSE;
    }

    // Record source peer as a replica location
    if (source_peer_id)
    {
        ChunkStore_SetReplicaLocation(hash, source_peer_id);
    }

    return TRUE;
}

uint32_t ChunkStore_GetReplicaCount(const uint8_t hash[WH_HASH_SIZE])
{
    char path[MAX_PATH];
    if (!GetReplicaPath(hash, path, sizeof(path))) return 0;

    FILE *fh = fopen(path, "rb");
    if (!fh) return 0;

    uint8_t count_buf[4];
    size_t read = fread(count_buf, 1, 4, fh);
    fclose(fh);

    if (read != 4) return 0;

    uint32_t count = (uint32_t)count_buf[0] |
                     ((uint32_t)count_buf[1] << 8) |
                     ((uint32_t)count_buf[2] << 16) |
                     ((uint32_t)count_buf[3] << 24);

    return count;
}

BOOLEAN ChunkStore_SetReplicaLocation(const uint8_t hash[WH_HASH_SIZE],
                                       const uint8_t peer_id[32])
{
    if (!peer_id) return FALSE;

    EnsureStoreLock();
    WH_MUTEX_LOCK(g_store_lock);

    if (!EnsureReplicaDirs(hash))
    {
        LOG_ERROR("[chunk_store] EnsureReplicaDirs failed for hash\n");
        WH_MUTEX_UNLOCK(g_store_lock);
        return FALSE;
    }

    char path[MAX_PATH];
    if (!GetReplicaPath(hash, path, sizeof(path)))
    {
        LOG_ERROR("[chunk_store] GetReplicaPath failed\n");
        WH_MUTEX_UNLOCK(g_store_lock);
        return FALSE;
    }

    // Read existing replicas
    uint8_t existing_peers[MAX_REPLICAS][32];
    uint32_t count = 0;

    FILE *fh = fopen(path, "rb");
    if (fh)
    {
        uint8_t count_buf[4];
        if (fread(count_buf, 1, 4, fh) == 4)
        {
            count = (uint32_t)count_buf[0] |
                    ((uint32_t)count_buf[1] << 8) |
                    ((uint32_t)count_buf[2] << 16) |
                    ((uint32_t)count_buf[3] << 24);

            if (count > MAX_REPLICAS) count = MAX_REPLICAS;
            fread(existing_peers, 32, count, fh);
        }
        fclose(fh);

        // Check if peer already recorded
        for (uint32_t i = 0; i < count; i++)
        {
            if (memcmp(existing_peers[i], peer_id, 32) == 0)
            {
                WH_MUTEX_UNLOCK(g_store_lock);
                return TRUE;  // Already tracked
            }
        }
    }

    // Add the new peer
    if (count >= MAX_REPLICAS)
    {
        LOG_ERROR("[chunk_store] Replica at capacity (%u)\n", MAX_REPLICAS);
        WH_MUTEX_UNLOCK(g_store_lock);
        return FALSE;  // At capacity
    }

    memcpy(existing_peers[count], peer_id, 32);
    count++;

    // Write updated file
    fh = fopen(path, "wb");
    if (!fh)
    {
        LOG_ERROR("[chunk_store] Failed to open replica file: %s\n", path);
        WH_MUTEX_UNLOCK(g_store_lock);
        return FALSE;
    }

    uint8_t count_buf[4];
    count_buf[0] = (uint8_t)(count);
    count_buf[1] = (uint8_t)(count >> 8);
    count_buf[2] = (uint8_t)(count >> 16);
    count_buf[3] = (uint8_t)(count >> 24);

    fwrite(count_buf, 1, 4, fh);
    fwrite(existing_peers, 32, count, fh);
    fclose(fh);

    WH_MUTEX_UNLOCK(g_store_lock);
    return TRUE;
}

uint32_t ChunkStore_GetReplicaLocations(const uint8_t hash[WH_HASH_SIZE],
                                         uint8_t peer_ids_out[][32],
                                         uint32_t max_peers)
{
    if (!peer_ids_out || max_peers == 0) return 0;

    char path[MAX_PATH];
    if (!GetReplicaPath(hash, path, sizeof(path))) return 0;

    FILE *fh = fopen(path, "rb");
    if (!fh) return 0;

    uint8_t count_buf[4];
    if (fread(count_buf, 1, 4, fh) != 4)
    {
        fclose(fh);
        return 0;
    }

    uint32_t count = (uint32_t)count_buf[0] |
                     ((uint32_t)count_buf[1] << 8) |
                     ((uint32_t)count_buf[2] << 16) |
                     ((uint32_t)count_buf[3] << 24);

    if (count > max_peers) count = max_peers;
    if (count > MAX_REPLICAS) count = MAX_REPLICAS;

    size_t read = fread(peer_ids_out, 32, count, fh);
    fclose(fh);

    return (uint32_t)read;
}

// Access time entry: [32B hash][8B unix_timestamp]
#define ACCESS_ENTRY_SIZE (WH_HASH_SIZE + 8)

//=============================================================================
// Replica Metadata Clearing
//=============================================================================

BOOLEAN ChunkStore_RemoveReplicaLocation(const uint8_t hash[WH_HASH_SIZE],
                                           const uint8_t peer_id[32])
{
    if (!peer_id) return FALSE;

    EnsureStoreLock();
    WH_MUTEX_LOCK(g_store_lock);

    char path[MAX_PATH];
    if (!GetReplicaPath(hash, path, sizeof(path)))
    {
        WH_MUTEX_UNLOCK(g_store_lock);
        return FALSE;
    }

    // Read existing replicas
    uint8_t existing_peers[MAX_REPLICAS][32];
    uint32_t count = 0;

    FILE *fh = fopen(path, "rb");
    if (!fh)
    {
        WH_MUTEX_UNLOCK(g_store_lock);
        return FALSE;  // No replica file = peer not found
    }

    uint8_t count_buf[4];
    if (fread(count_buf, 1, 4, fh) == 4)
    {
        count = (uint32_t)count_buf[0] |
                ((uint32_t)count_buf[1] << 8) |
                ((uint32_t)count_buf[2] << 16) |
                ((uint32_t)count_buf[3] << 24);
        if (count > MAX_REPLICAS) count = MAX_REPLICAS;
        fread(existing_peers, 32, count, fh);
    }
    fclose(fh);

    // Find and remove the peer
    BOOLEAN found = FALSE;
    for (uint32_t i = 0; i < count; i++)
    {
        if (memcmp(existing_peers[i], peer_id, 32) == 0)
        {
            // Shift remaining entries down
            for (uint32_t j = i; j < count - 1; j++)
                memcpy(existing_peers[j], existing_peers[j + 1], 32);
            count--;
            found = TRUE;
            break;
        }
    }

    if (!found)
    {
        WH_MUTEX_UNLOCK(g_store_lock);
        return FALSE;
    }

    if (count == 0)
    {
        // No replicas left — remove the file
        remove(path);
    }
    else
    {
        // Rewrite with updated list
        fh = fopen(path, "wb");
        if (fh)
        {
            count_buf[0] = (uint8_t)(count);
            count_buf[1] = (uint8_t)(count >> 8);
            count_buf[2] = (uint8_t)(count >> 16);
            count_buf[3] = (uint8_t)(count >> 24);
            fwrite(count_buf, 1, 4, fh);
            fwrite(existing_peers, 32, count, fh);
            fclose(fh);
        }
    }

    WH_MUTEX_UNLOCK(g_store_lock);
    return TRUE;
}

void ChunkStore_ClearReplicas(const uint8_t hash[WH_HASH_SIZE])
{
    char path[MAX_PATH];
    if (GetReplicaPath(hash, path, sizeof(path)))
        remove(path);
}

//=============================================================================
// Chunk Deletion
//=============================================================================

BOOLEAN ChunkStore_Delete(const uint8_t hash[WH_HASH_SIZE])
{
    EnsureStoreLock();
    WH_MUTEX_LOCK(g_store_lock);

    // Remove chunk data file and update quota counter
    char chunk_path[MAX_PATH];
    if (GetChunkPath(hash, chunk_path, sizeof(chunk_path)))
    {
        // Get file size before deleting for quota tracking
        FILE *sz_fh = fopen(chunk_path, "rb");
        if (sz_fh)
        {
#ifdef _WIN32
            _fseeki64(sz_fh, 0, SEEK_END);
            uint64_t chunk_sz = (uint64_t)_ftelli64(sz_fh);
#else
            fseeko(sz_fh, 0, SEEK_END);
            uint64_t chunk_sz = (uint64_t)ftello(sz_fh);
#endif
            fclose(sz_fh);

            if (remove(chunk_path) == 0 && g_quota_initialized && chunk_sz <= g_total_store_bytes)
            {
                g_total_store_bytes -= chunk_sz;
            }
        }
        else
        {
            remove(chunk_path);
        }
    }

    // Remove replica metadata
    char replica_path[MAX_PATH];
    if (GetReplicaPath(hash, replica_path, sizeof(replica_path)))
    {
        remove(replica_path);
    }

    // Remove proof cache
    Proof_DeleteCache(hash);

    // Remove from access_times.dat
    char at_path[MAX_PATH];
    if (GetAccessTimePath(at_path, sizeof(at_path)))
    {
        FILE *old_fh = fopen(at_path, "rb");
        if (old_fh)
        {
            char tmp_path[MAX_PATH];
            snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", at_path);
            FILE *new_fh = fopen(tmp_path, "wb");
            if (new_fh)
            {
                uint8_t at_entry[ACCESS_ENTRY_SIZE];
                while (fread(at_entry, 1, ACCESS_ENTRY_SIZE, old_fh) == ACCESS_ENTRY_SIZE)
                {
                    if (memcmp(at_entry, hash, WH_HASH_SIZE) != 0)
                    {
                        fwrite(at_entry, 1, ACCESS_ENTRY_SIZE, new_fh);
                    }
                }
                fclose(new_fh);
                fclose(old_fh);
                remove(at_path);
                rename(tmp_path, at_path);
            }
            else
            {
                fclose(old_fh);
            }
        }
    }

    WH_MUTEX_UNLOCK(g_store_lock);
    return TRUE;
}

//=============================================================================
// Storage Quota and LRU Eviction
//=============================================================================

// Access time file path: ~/.wormhole/store/access_times.dat
static BOOLEAN GetAccessTimePath(char *path, size_t path_len)
{
    char base[MAX_PATH];
    if (!GetStoreBasePath(base, sizeof(base))) return FALSE;

#ifdef _WIN32
    snprintf(path, path_len, "%s\\access_times.dat", base);
#else
    snprintf(path, path_len, "%s/access_times.dat", base);
#endif
    return TRUE;
}

void ChunkStore_SetAccessTime(const uint8_t hash[WH_HASH_SIZE])
{
    EnsureStoreLock();
    WH_MUTEX_LOCK(g_store_lock);

    char path[MAX_PATH];
    if (!GetAccessTimePath(path, sizeof(path)))
    {
        WH_MUTEX_UNLOCK(g_store_lock);
        return;
    }

    // Read existing entries, update or append
    FILE *fh = fopen(path, "r+b");
    if (!fh)
    {
        // Create new file
        fh = fopen(path, "wb");
        if (!fh)
        {
            WH_MUTEX_UNLOCK(g_store_lock);
            return;
        }

        uint8_t entry[ACCESS_ENTRY_SIZE];
        memcpy(entry, hash, WH_HASH_SIZE);
        uint64_t now = (uint64_t)time(NULL);
        entry[WH_HASH_SIZE]     = (uint8_t)(now);
        entry[WH_HASH_SIZE + 1] = (uint8_t)(now >> 8);
        entry[WH_HASH_SIZE + 2] = (uint8_t)(now >> 16);
        entry[WH_HASH_SIZE + 3] = (uint8_t)(now >> 24);
        entry[WH_HASH_SIZE + 4] = (uint8_t)(now >> 32);
        entry[WH_HASH_SIZE + 5] = (uint8_t)(now >> 40);
        entry[WH_HASH_SIZE + 6] = (uint8_t)(now >> 48);
        entry[WH_HASH_SIZE + 7] = (uint8_t)(now >> 56);

        fwrite(entry, 1, ACCESS_ENTRY_SIZE, fh);
        fclose(fh);
        WH_MUTEX_UNLOCK(g_store_lock);
        return;
    }

    // Search for existing entry
    uint8_t entry[ACCESS_ENTRY_SIZE];
    long offset = 0;
    BOOLEAN found = FALSE;

    while (fread(entry, 1, ACCESS_ENTRY_SIZE, fh) == ACCESS_ENTRY_SIZE)
    {
        if (memcmp(entry, hash, WH_HASH_SIZE) == 0)
        {
            // Update timestamp
            uint64_t now = (uint64_t)time(NULL);
            entry[WH_HASH_SIZE]     = (uint8_t)(now);
            entry[WH_HASH_SIZE + 1] = (uint8_t)(now >> 8);
            entry[WH_HASH_SIZE + 2] = (uint8_t)(now >> 16);
            entry[WH_HASH_SIZE + 3] = (uint8_t)(now >> 24);
            entry[WH_HASH_SIZE + 4] = (uint8_t)(now >> 32);
            entry[WH_HASH_SIZE + 5] = (uint8_t)(now >> 40);
            entry[WH_HASH_SIZE + 6] = (uint8_t)(now >> 48);
            entry[WH_HASH_SIZE + 7] = (uint8_t)(now >> 56);

#ifdef _WIN32
            _fseeki64(fh, offset, SEEK_SET);
#else
            fseeko(fh, offset, SEEK_SET);
#endif
            fwrite(entry, 1, ACCESS_ENTRY_SIZE, fh);
            found = TRUE;
            break;
        }
        offset += ACCESS_ENTRY_SIZE;
    }

    if (!found)
    {
        // Append new entry at end
        fseek(fh, 0, SEEK_END);
        uint64_t now = (uint64_t)time(NULL);
        memcpy(entry, hash, WH_HASH_SIZE);
        entry[WH_HASH_SIZE]     = (uint8_t)(now);
        entry[WH_HASH_SIZE + 1] = (uint8_t)(now >> 8);
        entry[WH_HASH_SIZE + 2] = (uint8_t)(now >> 16);
        entry[WH_HASH_SIZE + 3] = (uint8_t)(now >> 24);
        entry[WH_HASH_SIZE + 4] = (uint8_t)(now >> 32);
        entry[WH_HASH_SIZE + 5] = (uint8_t)(now >> 40);
        entry[WH_HASH_SIZE + 6] = (uint8_t)(now >> 48);
        entry[WH_HASH_SIZE + 7] = (uint8_t)(now >> 56);
        fwrite(entry, 1, ACCESS_ENTRY_SIZE, fh);
    }

    fclose(fh);
    WH_MUTEX_UNLOCK(g_store_lock);
}

uint64_t ChunkStore_GetTotalSize(void)
{
    char base[MAX_PATH];
    if (!GetStoreBasePath(base, sizeof(base))) return 0;

    uint64_t total = 0;

#ifdef _WIN32
    // Iterate XX/ subdirectories, then files within
    char search_pattern[MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*", base);

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_pattern, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    do {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (find_data.cFileName[0] == '.') continue;

        // Search within this prefix directory
        char sub_pattern[MAX_PATH];
        snprintf(sub_pattern, sizeof(sub_pattern), "%s\\%s\\*", base, find_data.cFileName);

        WIN32_FIND_DATAA sub_data;
        HANDLE hSubFind = FindFirstFileA(sub_pattern, &sub_data);
        if (hSubFind == INVALID_HANDLE_VALUE) continue;

        do {
            if (sub_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

            uint64_t file_size = ((uint64_t)sub_data.nFileSizeHigh << 32) | sub_data.nFileSizeLow;
            total += file_size;
        } while (FindNextFileA(hSubFind, &sub_data));

        FindClose(hSubFind);
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);
#else
    // POSIX: use opendir/readdir
    DIR *dir = opendir(base);
    if (!dir) return 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (ent->d_name[0] == '.') continue;

        char subdir[MAX_PATH];
        snprintf(subdir, sizeof(subdir), "%s/%s", base, ent->d_name);

        struct stat st;
        if (stat(subdir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        DIR *sub = opendir(subdir);
        if (!sub) continue;

        struct dirent *sub_ent;
        while ((sub_ent = readdir(sub)) != NULL)
        {
            if (sub_ent->d_name[0] == '.') continue;

            char filepath[MAX_PATH];
            snprintf(filepath, sizeof(filepath), "%s/%s", subdir, sub_ent->d_name);

            struct stat fst;
            if (stat(filepath, &fst) == 0 && S_ISREG(fst.st_mode))
            {
                total += (uint64_t)fst.st_size;
            }
        }
        closedir(sub);
    }
    closedir(dir);
#endif

    return total;
}

// Eviction candidate for sorting
typedef struct {
    uint8_t  hash[WH_HASH_SIZE];
    uint64_t access_time;    // Lower = older = evict first
    uint32_t replica_count;  // Higher = safer to evict
    uint64_t file_size;
} EVICTION_CANDIDATE;

// Compare for eviction priority: prefer high replica count, then oldest access
static int EvictionCompare(const void *a, const void *b)
{
    const EVICTION_CANDIDATE *ca = (const EVICTION_CANDIDATE *)a;
    const EVICTION_CANDIDATE *cb = (const EVICTION_CANDIDATE *)b;

    // Higher replica count = evict first (safer)
    if (ca->replica_count != cb->replica_count)
    {
        return (ca->replica_count > cb->replica_count) ? -1 : 1;
    }

    // Older access time = evict first
    if (ca->access_time != cb->access_time)
    {
        return (ca->access_time < cb->access_time) ? -1 : 1;
    }

    return 0;
}

uint64_t ChunkStore_Evict(uint64_t bytes_to_free)
{
    if (bytes_to_free == 0) return 0;

    // Load access times
    char at_path[MAX_PATH];
    if (!GetAccessTimePath(at_path, sizeof(at_path))) return 0;

    FILE *at_fh = fopen(at_path, "rb");

    // Count entries
    uint32_t entry_count = 0;
    if (at_fh)
    {
#ifdef _WIN32
        _fseeki64(at_fh, 0, SEEK_END);
        __int64 fsize = _ftelli64(at_fh);
        _fseeki64(at_fh, 0, SEEK_SET);
#else
        fseeko(at_fh, 0, SEEK_END);
        off_t fsize = ftello(at_fh);
        fseeko(at_fh, 0, SEEK_SET);
#endif
        entry_count = (uint32_t)(fsize / ACCESS_ENTRY_SIZE);
    }

    if (entry_count == 0)
    {
        if (at_fh) fclose(at_fh);
        return 0;
    }

    // Build eviction candidates
    EVICTION_CANDIDATE *candidates = (EVICTION_CANDIDATE *)calloc(entry_count, sizeof(EVICTION_CANDIDATE));
    if (!candidates)
    {
        if (at_fh) fclose(at_fh);
        return 0;
    }

    uint32_t valid = 0;
    uint8_t entry_buf[ACCESS_ENTRY_SIZE];
    for (uint32_t i = 0; i < entry_count; i++)
    {
        if (fread(entry_buf, 1, ACCESS_ENTRY_SIZE, at_fh) != ACCESS_ENTRY_SIZE)
        {
            break;
        }

        // Only include chunks that still exist
        if (!ChunkStore_Has(entry_buf))
        {
            continue;
        }

        memcpy(candidates[valid].hash, entry_buf, WH_HASH_SIZE);

        // Read access time (LE uint64)
        candidates[valid].access_time =
            (uint64_t)entry_buf[WH_HASH_SIZE] |
            ((uint64_t)entry_buf[WH_HASH_SIZE + 1] << 8) |
            ((uint64_t)entry_buf[WH_HASH_SIZE + 2] << 16) |
            ((uint64_t)entry_buf[WH_HASH_SIZE + 3] << 24) |
            ((uint64_t)entry_buf[WH_HASH_SIZE + 4] << 32) |
            ((uint64_t)entry_buf[WH_HASH_SIZE + 5] << 40) |
            ((uint64_t)entry_buf[WH_HASH_SIZE + 6] << 48) |
            ((uint64_t)entry_buf[WH_HASH_SIZE + 7] << 56);

        candidates[valid].replica_count = ChunkStore_GetReplicaCount(entry_buf);

        // Get chunk file size
        char chunk_path[MAX_PATH];
        if (GetChunkPath(entry_buf, chunk_path, sizeof(chunk_path)))
        {
            FILE *cfh = fopen(chunk_path, "rb");
            if (cfh)
            {
#ifdef _WIN32
                _fseeki64(cfh, 0, SEEK_END);
                candidates[valid].file_size = (uint64_t)_ftelli64(cfh);
#else
                fseeko(cfh, 0, SEEK_END);
                candidates[valid].file_size = (uint64_t)ftello(cfh);
#endif
                fclose(cfh);
            }
        }

        valid++;
    }

    fclose(at_fh);

    if (valid == 0)
    {
        free(candidates);
        return 0;
    }

    // Sort by eviction priority
    qsort(candidates, valid, sizeof(EVICTION_CANDIDATE), EvictionCompare);

    // Evict until we've freed enough
    uint64_t freed = 0;
    uint32_t evicted = 0;
    for (uint32_t i = 0; i < valid && freed < bytes_to_free; i++)
    {
        char chunk_path[MAX_PATH];
        if (!GetChunkPath(candidates[i].hash, chunk_path, sizeof(chunk_path)))
        {
            continue;
        }

        if (remove(chunk_path) == 0)
        {
            freed += candidates[i].file_size;
            evicted++;

            // Clean up orphaned replica metadata
            char replica_path[MAX_PATH];
            if (GetReplicaPath(candidates[i].hash, replica_path, sizeof(replica_path)))
            {
                remove(replica_path);
            }

            // Clean up orphaned proof cache
            Proof_DeleteCache(candidates[i].hash);

            char hex[65];
            HashToHex(candidates[i].hash, hex);
            hex[64] = '\0';
            LOG("[evict] Removed chunk %.8s... (%llu bytes, %u replicas)\n",
                hex, (unsigned long long)candidates[i].file_size, candidates[i].replica_count);
        }
    }

    free(candidates);

    // Rewrite access_times.dat to remove entries for evicted/missing chunks
    if (evicted > 0)
    {
        EnsureStoreLock();
        WH_MUTEX_LOCK(g_store_lock);
        FILE *old_fh = fopen(at_path, "rb");
        if (old_fh)
        {
            char tmp_path[MAX_PATH];
            snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", at_path);
            FILE *new_fh = fopen(tmp_path, "wb");
            if (new_fh)
            {
                uint8_t at_entry[ACCESS_ENTRY_SIZE];
                while (fread(at_entry, 1, ACCESS_ENTRY_SIZE, old_fh) == ACCESS_ENTRY_SIZE)
                {
                    // Only keep entries for chunks that still exist in the store
                    if (ChunkStore_Has(at_entry))
                    {
                        fwrite(at_entry, 1, ACCESS_ENTRY_SIZE, new_fh);
                    }
                }
                fclose(new_fh);
                fclose(old_fh);
                // Replace old file with cleaned version
                remove(at_path);
                rename(tmp_path, at_path);
            }
            else
            {
                fclose(old_fh);
            }
        }
        WH_MUTEX_UNLOCK(g_store_lock);
    }

    // Update quota tracking
    if (freed > 0 && g_quota_initialized)
    {
        if (freed <= g_total_store_bytes)
            g_total_store_bytes -= freed;
        else
            g_total_store_bytes = 0;
    }

    LOG("[evict] Evicted %u chunks, freed %llu bytes\n",
        evicted, (unsigned long long)freed);

    return freed;
}
