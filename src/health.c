//
// health.c
// Health checking and recovery for P2P stored chunks.
// by Dylan Kress
//

#include "health.h"
#include "chunk_store.h"
#include "protocol.h"
#include "erasure.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

// Minimum replicas before a chunk is considered critical
#define HEALTH_MIN_REPLICAS 1

//=============================================================================
// Internal helpers
//=============================================================================

// Convert a hex character to its nibble value.
static int HexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

// Convert a 64-char hex string to a 32-byte hash. Returns FALSE on invalid input.
static BOOLEAN HexToHash(const char *hex, uint8_t hash[WH_HASH_SIZE])
{
    for (int i = 0; i < WH_HASH_SIZE; i++)
    {
        int hi = HexNibble(hex[i * 2]);
        int lo = HexNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return FALSE;
        hash[i] = (uint8_t)((hi << 4) | lo);
    }
    return TRUE;
}

// Build the store base path
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

//=============================================================================
// Internal: generic store scanner
//=============================================================================

// Visitor callback: receives each chunk hash found. Return TRUE to continue, FALSE to stop.
typedef BOOLEAN (*ChunkVisitor)(const uint8_t hash[WH_HASH_SIZE], void *ctx);

// Scan the chunk store directory (2-level prefix layout), calling visitor for
// each chunk hash found, up to max_chunks. Returns total chunks visited.
static uint32_t ScanStoreChunks(uint32_t max_chunks, ChunkVisitor visitor, void *ctx)
{
    char base[MAX_PATH];
    if (!GetStoreBasePath(base, sizeof(base))) return 0;

    uint32_t visited = 0;
    BOOLEAN stopped = FALSE;

#ifdef _WIN32
    char search_pattern[MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*", base);

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_pattern, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    do {
        if (stopped) break;
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (find_data.cFileName[0] == '.') continue;

        char prefix[3];
        snprintf(prefix, sizeof(prefix), "%s", find_data.cFileName);

        char sub_pattern[MAX_PATH];
        snprintf(sub_pattern, sizeof(sub_pattern), "%s\\%s\\*", base, prefix);

        WIN32_FIND_DATAA sub_data;
        HANDLE hSubFind = FindFirstFileA(sub_pattern, &sub_data);
        if (hSubFind == INVALID_HANDLE_VALUE) continue;

        do {
            if (sub_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (max_chunks > 0 && visited >= max_chunks) break;

            char hex[65];
            snprintf(hex, sizeof(hex), "%s%s", prefix, sub_data.cFileName);
            if (strlen(hex) != 64) continue;

            uint8_t hash[WH_HASH_SIZE];
            if (!HexToHash(hex, hash)) continue;

            visited++;
            if (!visitor(hash, ctx)) { stopped = TRUE; break; }

        } while (FindNextFileA(hSubFind, &sub_data));

        FindClose(hSubFind);

        if (max_chunks > 0 && visited >= max_chunks) break;
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);
#else
    DIR *dir = opendir(base);
    if (!dir) return 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (stopped) break;
        if (ent->d_name[0] == '.') continue;

        char subdir[MAX_PATH];
        snprintf(subdir, sizeof(subdir), "%s/%s", base, ent->d_name);

        struct stat st;
        if (stat(subdir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        char prefix[3];
        snprintf(prefix, sizeof(prefix), "%s", ent->d_name);

        DIR *sub = opendir(subdir);
        if (!sub) continue;

        struct dirent *sub_ent;
        while ((sub_ent = readdir(sub)) != NULL)
        {
            if (sub_ent->d_name[0] == '.') continue;
            if (max_chunks > 0 && visited >= max_chunks) break;

            char hex[65];
            snprintf(hex, sizeof(hex), "%s%s", prefix, sub_ent->d_name);
            if (strlen(hex) != 64) continue;

            uint8_t hash[WH_HASH_SIZE];
            if (!HexToHash(hex, hash)) continue;

            visited++;
            if (!visitor(hash, ctx)) { stopped = TRUE; break; }
        }

        closedir(sub);
        if (max_chunks > 0 && visited >= max_chunks) break;
    }

    closedir(dir);
#endif

    return visited;
}

//=============================================================================
// Visitor: Health_CheckChunks
//=============================================================================

static BOOLEAN HealthCheckVisitor(const uint8_t hash[WH_HASH_SIZE], void *ctx)
{
    HEALTH_STATS *stats = (HEALTH_STATS *)ctx;

    uint32_t replicas = ChunkStore_GetReplicaCount(hash);
    stats->chunks_checked++;

    if (replicas >= REPLICATION_TARGET)
        stats->chunks_healthy++;
    else if (replicas >= HEALTH_MIN_REPLICAS)
        stats->chunks_degraded++;
    else
        stats->chunks_critical++;

    return TRUE;
}

//=============================================================================
// Visitor: Health_GetDegradedChunks
//=============================================================================

typedef struct {
    uint8_t (*out_hashes)[WH_HASH_SIZE];
    uint32_t max_count;
    uint32_t found;
} DEGRADED_CTX;

static BOOLEAN DegradedVisitor(const uint8_t hash[WH_HASH_SIZE], void *ctx)
{
    DEGRADED_CTX *dc = (DEGRADED_CTX *)ctx;

    uint32_t replicas = ChunkStore_GetReplicaCount(hash);
    if (replicas < REPLICATION_TARGET)
    {
        memcpy(dc->out_hashes[dc->found], hash, WH_HASH_SIZE);
        dc->found++;
        if (dc->found >= dc->max_count)
            return FALSE;  // stop scanning
    }
    return TRUE;
}

//=============================================================================
// Public API
//=============================================================================

HEALTH_STATS Health_CheckChunks(uint32_t max_chunks)
{
    HEALTH_STATS stats;
    memset(&stats, 0, sizeof(stats));

    ScanStoreChunks(max_chunks, HealthCheckVisitor, &stats);
    return stats;
}

uint32_t Health_GetDegradedChunks(uint8_t (*out_hashes)[WH_HASH_SIZE], uint32_t max_count)
{
    if (!out_hashes || max_count == 0) return 0;

    DEGRADED_CTX ctx;
    ctx.out_hashes = out_hashes;
    ctx.max_count = max_count;
    ctx.found = 0;

    ScanStoreChunks(0, DegradedVisitor, &ctx);  // 0 = scan all
    return ctx.found;
}

// Visitor: Health_GetReplicatedChunks
typedef struct {
    uint8_t (*out_hashes)[WH_HASH_SIZE];
    uint32_t max_count;
    uint32_t found;
} REPLICATED_CTX;

static BOOLEAN ReplicatedVisitor(const uint8_t hash[WH_HASH_SIZE], void *ctx)
{
    REPLICATED_CTX *rc = (REPLICATED_CTX *)ctx;

    uint32_t replicas = ChunkStore_GetReplicaCount(hash);
    if (replicas > 0)
    {
        memcpy(rc->out_hashes[rc->found], hash, WH_HASH_SIZE);
        rc->found++;
        if (rc->found >= rc->max_count)
            return FALSE;  // stop scanning
    }
    return TRUE;
}

uint32_t Health_GetReplicatedChunks(uint8_t (*out_hashes)[WH_HASH_SIZE], uint32_t max_count)
{
    if (!out_hashes || max_count == 0) return 0;

    REPLICATED_CTX ctx;
    ctx.out_hashes = out_hashes;
    ctx.max_count = max_count;
    ctx.found = 0;

    ScanStoreChunks(0, ReplicatedVisitor, &ctx);  // 0 = scan all
    return ctx.found;
}

BOOLEAN Health_RecoverChunk(const uint8_t hash[WH_HASH_SIZE],
                             const EC_GROUP *ec_group,
                             const FILE_MANIFEST *manifest)
{
    if (!hash || !ec_group || !manifest || manifest->chunk_count == 0)
        return FALSE;

    // Already have it?
    if (ChunkStore_Has(hash)) return TRUE;

    // Find chunk index in manifest
    uint32_t chunk_index = UINT32_MAX;
    for (uint32_t i = 0; i < manifest->chunk_count; i++)
    {
        if (memcmp(manifest->chunks[i].hash, hash, WH_HASH_SIZE) == 0)
        {
            chunk_index = i;
            break;
        }
    }
    if (chunk_index == UINT32_MAX)
    {
        LOG("[health] Hash not found in manifest, cannot recover\n");
        return FALSE;
    }

    // Attempt local EC reconstruction
    LOG("[health] Attempting EC reconstruction for chunk %u...\n", chunk_index);
    if (ErasureCoding_ReconstructChunk(chunk_index, ec_group, manifest))
    {
        LOG("[health] Chunk %u reconstructed successfully\n", chunk_index);
        return TRUE;
    }

    LOG("[health] EC reconstruction failed (insufficient local shards)\n");
    return FALSE;
}

uint32_t Health_ReplicateChunk(const uint8_t hash[WH_HASH_SIZE],
                                uint32_t current_replicas, uint32_t target)
{
    if (current_replicas >= target) return 0;

    uint32_t needed = target - current_replicas;

    // Verify we have the chunk locally
    if (!ChunkStore_Has(hash))
    {
        LOG("[health] Cannot replicate chunk — not in local store\n");
        return 0;
    }

    // Placeholder: actual replication requires QUIC connections to peers,
    // which is handled by wormholed.c. We just log the intent here.
    LOG("[health] Chunk needs %u more replicas (have %u, target %u)\n",
        needed, current_replicas, target);

    return needed;
}

// ============================================================================
// Visitor: Health_GetChunksOnPeer
// ============================================================================

typedef struct {
    const uint8_t *peer_id;
    uint8_t (*out_hashes)[WH_HASH_SIZE];
    uint32_t max_count;
    uint32_t found;
} PEER_CHUNKS_CTX;

static BOOLEAN PeerChunksVisitor(const uint8_t hash[WH_HASH_SIZE], void *ctx)
{
    PEER_CHUNKS_CTX *pc = (PEER_CHUNKS_CTX *)ctx;

    // Check if this chunk has a replica on the target peer
    uint8_t peer_ids[16][32];
    uint32_t peer_count = ChunkStore_GetReplicaLocations(hash, peer_ids, 16);

    for (uint32_t i = 0; i < peer_count; i++)
    {
        if (memcmp(peer_ids[i], pc->peer_id, 32) == 0)
        {
            memcpy(pc->out_hashes[pc->found], hash, WH_HASH_SIZE);
            pc->found++;
            if (pc->found >= pc->max_count)
                return FALSE;  // stop scanning
            break;
        }
    }
    return TRUE;
}

uint32_t Health_GetChunksOnPeer(const uint8_t peer_id[32],
                                  uint8_t (*out_hashes)[WH_HASH_SIZE],
                                  uint32_t max_count)
{
    if (!peer_id || !out_hashes || max_count == 0) return 0;

    PEER_CHUNKS_CTX ctx;
    ctx.peer_id = peer_id;
    ctx.out_hashes = out_hashes;
    ctx.max_count = max_count;
    ctx.found = 0;

    ScanStoreChunks(0, PeerChunksVisitor, &ctx);  // 0 = scan all
    return ctx.found;
}
