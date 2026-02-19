//
// test_health.c
// Unit tests for health.c — EC-based recovery, health checking.
//

#include "../health.h"
#include "../erasure.h"
#include "../chunk_store.h"
#include "../manifest.h"
#include "../protocol.h"
#include "greatest.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <blake3.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

GREATEST_MAIN_DEFS();

// ============================================================================
// Test environment — redirect store to a temporary directory
// ============================================================================

static char g_test_home[512];

#ifdef _WIN32
static char g_env_buf[544];
#endif

static void setup_test_home(void)
{
#ifdef _WIN32
    char temp[512];
    GetTempPathA(sizeof(temp), temp);
    snprintf(g_test_home, sizeof(g_test_home), "%swh_health_test_%lu",
             temp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(g_test_home, NULL);

    snprintf(g_env_buf, sizeof(g_env_buf), "USERPROFILE=%s", g_test_home);
    _putenv(g_env_buf);
#else
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/wh_health_test_%d", (int)getpid());
    mkdir(g_test_home, 0755);
    setenv("HOME", g_test_home, 1);
#endif
    ChunkStore_Init();
}

static void cleanup_test_home(void)
{
    char cmd[600];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" >nul 2>&1", g_test_home);
#else
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", g_test_home);
#endif
    system(cmd);
}

// ============================================================================
// Helpers
// ============================================================================

// Create a test manifest with n_chunks, each WH_CHUNK_SIZE, and store chunk data
static FILE_MANIFEST *create_test_manifest(uint32_t n_chunks, uint8_t **chunk_data_out)
{
    uint64_t file_size = (uint64_t)n_chunks * WH_CHUNK_SIZE;
    FILE_MANIFEST *m = Manifest_Create("test_health.bin", file_size);
    if (!m) return NULL;

    for (uint32_t i = 0; i < n_chunks; i++)
    {
        uint8_t *data = (uint8_t *)malloc(WH_CHUNK_SIZE);
        if (!data)
        {
            Manifest_Destroy(m);
            return NULL;
        }

        // Fill with deterministic pattern
        for (uint32_t b = 0; b < WH_CHUNK_SIZE; b++)
        {
            data[b] = (uint8_t)((i * 37 + b) % 256);
        }

        // Compute Blake3 hash
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, data, WH_CHUNK_SIZE);
        uint8_t hash[WH_HASH_SIZE];
        blake3_hasher_finalize(&hasher, hash, WH_HASH_SIZE);

        Manifest_AddChunk(m, i, hash, WH_CHUNK_SIZE);

        // Store in chunk store
        ChunkStore_Put(hash, data, WH_CHUNK_SIZE);

        if (chunk_data_out)
        {
            chunk_data_out[i] = data;
        }
        else
        {
            free(data);
        }
    }

    return m;
}

// Delete a chunk from the store by removing the file
static void delete_chunk_from_store(const uint8_t hash[WH_HASH_SIZE])
{
    static const char digits[] = "0123456789abcdef";
    char hex[65];
    for (int i = 0; i < WH_HASH_SIZE; i++)
    {
        hex[i * 2]     = digits[(hash[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = digits[hash[i] & 0x0F];
    }
    hex[64] = '\0';

    char path[600];
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\.wormhole\\store\\%.2s\\%s",
             g_test_home, hex, hex + 2);
#else
    snprintf(path, sizeof(path), "%s/.wormhole/store/%.2s/%s",
             g_test_home, hex, hex + 2);
#endif
    remove(path);
}

// ============================================================================
// Recovery Suite
// ============================================================================

TEST test_health_recover_with_ec(void)
{
    uint8_t *chunk_data[8];
    FILE_MANIFEST *m = create_test_manifest(8, chunk_data);
    ASSERT(m != NULL);

    // Encode with RS(4,2)
    EC_GROUP *group = ErasureCoding_Encode(m, 4, 2);
    ASSERT(group != NULL);

    // Save original data for chunk 2
    uint8_t *original = (uint8_t *)malloc(WH_CHUNK_SIZE);
    ASSERT(original != NULL);
    memcpy(original, chunk_data[2], WH_CHUNK_SIZE);

    // Delete chunk 2 from store
    delete_chunk_from_store(m->chunks[2].hash);
    ASSERT_FALSE(ChunkStore_Has(m->chunks[2].hash));

    // Recover via Health_RecoverChunk
    ASSERT(Health_RecoverChunk(m->chunks[2].hash, group, m));

    // Verify data matches original
    ASSERT(ChunkStore_Has(m->chunks[2].hash));
    uint8_t *recovered = (uint8_t *)malloc(WH_CHUNK_SIZE);
    uint32_t recovered_size = 0;
    ASSERT(ChunkStore_Get(m->chunks[2].hash, recovered, &recovered_size));
    ASSERT_EQ(recovered_size, (uint32_t)WH_CHUNK_SIZE);
    ASSERT_MEM_EQ(recovered, original, WH_CHUNK_SIZE);

    free(original);
    free(recovered);
    for (int i = 0; i < 8; i++) free(chunk_data[i]);
    ErasureCoding_DestroyGroup(group);
    Manifest_Destroy(m);
    PASS();
}

TEST test_health_recover_already_exists(void)
{
    uint8_t *chunk_data[8];
    FILE_MANIFEST *m = create_test_manifest(8, chunk_data);
    ASSERT(m != NULL);

    EC_GROUP *group = ErasureCoding_Encode(m, 4, 2);
    ASSERT(group != NULL);

    // Chunk 0 is already in store — should return TRUE immediately
    ASSERT(ChunkStore_Has(m->chunks[0].hash));
    ASSERT(Health_RecoverChunk(m->chunks[0].hash, group, m));

    for (int i = 0; i < 8; i++) free(chunk_data[i]);
    ErasureCoding_DestroyGroup(group);
    Manifest_Destroy(m);
    PASS();
}

TEST test_health_recover_null_params(void)
{
    uint8_t hash[WH_HASH_SIZE] = {0};
    EC_GROUP group = {0};
    FILE_MANIFEST manifest = {0};

    ASSERT_FALSE(Health_RecoverChunk(NULL, &group, &manifest));
    ASSERT_FALSE(Health_RecoverChunk(hash, NULL, &manifest));
    ASSERT_FALSE(Health_RecoverChunk(hash, &group, NULL));

    // Empty manifest (chunk_count == 0)
    ASSERT_FALSE(Health_RecoverChunk(hash, &group, &manifest));

    PASS();
}

TEST test_health_recover_hash_not_in_manifest(void)
{
    uint8_t *chunk_data[8];
    FILE_MANIFEST *m = create_test_manifest(8, chunk_data);
    ASSERT(m != NULL);

    EC_GROUP *group = ErasureCoding_Encode(m, 4, 2);
    ASSERT(group != NULL);

    // Try recovering a hash that doesn't exist in the manifest
    uint8_t fake_hash[WH_HASH_SIZE];
    memset(fake_hash, 0xFF, WH_HASH_SIZE);
    ASSERT_FALSE(Health_RecoverChunk(fake_hash, group, m));

    for (int i = 0; i < 8; i++) free(chunk_data[i]);
    ErasureCoding_DestroyGroup(group);
    Manifest_Destroy(m);
    PASS();
}

TEST test_health_recover_too_many_missing(void)
{
    uint8_t *chunk_data[8];
    FILE_MANIFEST *m = create_test_manifest(8, chunk_data);
    ASSERT(m != NULL);

    // RS(4,2) — can tolerate up to 2 missing shards per stripe
    EC_GROUP *group = ErasureCoding_Encode(m, 4, 2);
    ASSERT(group != NULL);

    // Delete 3 of 4 data chunks in stripe 0 (exceeds RS(4,2) capacity)
    delete_chunk_from_store(m->chunks[0].hash);
    delete_chunk_from_store(m->chunks[1].hash);
    delete_chunk_from_store(m->chunks[2].hash);

    // Recovery should fail — 3 data shards missing, only 2 parity available
    ASSERT_FALSE(Health_RecoverChunk(m->chunks[0].hash, group, m));

    for (int i = 0; i < 8; i++) free(chunk_data[i]);
    ErasureCoding_DestroyGroup(group);
    Manifest_Destroy(m);
    PASS();
}

// ============================================================================
// Check Suite
// ============================================================================

TEST test_health_check_chunks_basic(void)
{
    // Store some chunks (already done by create_test_manifest in prior tests,
    // but let's create fresh ones)
    FILE_MANIFEST *m = create_test_manifest(4, NULL);
    ASSERT(m != NULL);

    // Health check — all chunks have 0 replicas (no SetReplicaLocation calls)
    // so they should all be critical
    HEALTH_STATS stats = Health_CheckChunks(100);
    ASSERT(stats.chunks_checked >= 4);
    ASSERT(stats.chunks_critical >= 4);

    Manifest_Destroy(m);
    PASS();
}

TEST test_health_replicate_at_target(void)
{
    uint8_t hash[WH_HASH_SIZE] = {0x01};

    // Store a dummy chunk
    uint8_t data[64];
    memset(data, 0xAB, sizeof(data));

    // Compute real hash
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data, sizeof(data));
    blake3_hasher_finalize(&hasher, hash, WH_HASH_SIZE);

    ChunkStore_Put(hash, data, sizeof(data));

    // Already at target — should return 0
    uint32_t needed = Health_ReplicateChunk(hash, REPLICATION_TARGET, REPLICATION_TARGET);
    ASSERT_EQ(needed, 0u);

    PASS();
}

TEST test_health_replicate_needs_more(void)
{
    uint8_t hash[WH_HASH_SIZE] = {0x02};

    // Store a dummy chunk
    uint8_t data[64];
    memset(data, 0xCD, sizeof(data));

    // Compute real hash
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data, sizeof(data));
    blake3_hasher_finalize(&hasher, hash, WH_HASH_SIZE);

    ChunkStore_Put(hash, data, sizeof(data));

    // Has 1 replica, target 3 — needs 2 more
    uint32_t needed = Health_ReplicateChunk(hash, 1, 3);
    ASSERT_EQ(needed, 2u);

    PASS();
}

TEST test_health_get_degraded_chunks(void)
{
    // Create some chunks — they'll have 0 replicas (degraded)
    FILE_MANIFEST *m = create_test_manifest(3, NULL);
    ASSERT(m != NULL);

    // All chunks should be degraded (replica_count=0 < REPLICATION_TARGET=3)
    uint8_t degraded[10][WH_HASH_SIZE];
    uint32_t count = Health_GetDegradedChunks(degraded, 10);

    // Should find at least 3 degraded (may find more from prior tests)
    ASSERT(count >= 3);

    // Verify returned hashes are from actual chunks (each should exist in store)
    for (uint32_t i = 0; i < count; i++)
    {
        ASSERT(ChunkStore_Has(degraded[i]));
    }

    Manifest_Destroy(m);
    PASS();
}

TEST test_health_get_degraded_chunks_capped(void)
{
    // Request at most 2 — should cap even if more are degraded
    uint8_t degraded[2][WH_HASH_SIZE];
    uint32_t count = Health_GetDegradedChunks(degraded, 2);
    ASSERT(count <= 2);

    PASS();
}

TEST test_health_get_degraded_null_params(void)
{
    // NULL output should return 0
    uint32_t count = Health_GetDegradedChunks(NULL, 10);
    ASSERT_EQ(count, 0u);

    // Zero max_count should return 0
    uint8_t degraded[1][WH_HASH_SIZE];
    count = Health_GetDegradedChunks(degraded, 0);
    ASSERT_EQ(count, 0u);

    PASS();
}

// ============================================================================
// Suites
// ============================================================================

SUITE(recovery_suite)
{
    RUN_TEST(test_health_recover_with_ec);
    RUN_TEST(test_health_recover_already_exists);
    RUN_TEST(test_health_recover_null_params);
    RUN_TEST(test_health_recover_hash_not_in_manifest);
    RUN_TEST(test_health_recover_too_many_missing);
}

TEST test_health_get_replicated_chunks(void)
{
    // Create chunks and set replicas for some
    uint8_t data1[64], data2[64], data3[64];
    memset(data1, 0xF1, sizeof(data1));
    memset(data2, 0xF2, sizeof(data2));
    memset(data3, 0xF3, sizeof(data3));

    uint8_t h1[WH_HASH_SIZE], h2[WH_HASH_SIZE], h3[WH_HASH_SIZE];
    blake3_hasher hasher;

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data1, sizeof(data1));
    blake3_hasher_finalize(&hasher, h1, WH_HASH_SIZE);

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data2, sizeof(data2));
    blake3_hasher_finalize(&hasher, h2, WH_HASH_SIZE);

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data3, sizeof(data3));
    blake3_hasher_finalize(&hasher, h3, WH_HASH_SIZE);

    ChunkStore_Put(h1, data1, sizeof(data1));
    ChunkStore_Put(h2, data2, sizeof(data2));
    ChunkStore_Put(h3, data3, sizeof(data3));

    // Set replicas for h1 and h3 only
    uint8_t peer[32];
    memset(peer, 0xAA, 32);
    ChunkStore_SetReplicaLocation(h1, peer);
    ChunkStore_SetReplicaLocation(h3, peer);

    // Get replicated chunks
    uint8_t repl[10][WH_HASH_SIZE];
    uint32_t count = Health_GetReplicatedChunks(repl, 10);

    // Should find at least h1 and h3
    ASSERT(count >= 2);

    // Verify returned hashes exist in store and have replicas
    for (uint32_t i = 0; i < count; i++)
    {
        ASSERT(ChunkStore_Has(repl[i]));
        ASSERT(ChunkStore_GetReplicaCount(repl[i]) > 0);
    }

    PASS();
}

TEST test_health_get_replicated_null_params(void)
{
    uint32_t count = Health_GetReplicatedChunks(NULL, 10);
    ASSERT_EQ(count, 0u);

    uint8_t repl[1][WH_HASH_SIZE];
    count = Health_GetReplicatedChunks(repl, 0);
    ASSERT_EQ(count, 0u);

    PASS();
}

SUITE(check_suite)
{
    RUN_TEST(test_health_check_chunks_basic);
    RUN_TEST(test_health_replicate_at_target);
    RUN_TEST(test_health_replicate_needs_more);
    RUN_TEST(test_health_get_degraded_chunks);
    RUN_TEST(test_health_get_degraded_chunks_capped);
    RUN_TEST(test_health_get_degraded_null_params);
    RUN_TEST(test_health_get_replicated_chunks);
    RUN_TEST(test_health_get_replicated_null_params);
}

int main(void)
{
    setup_test_home();
    atexit(cleanup_test_home);

    GREATEST_MAIN_BEGIN();
    RUN_SUITE(recovery_suite);
    RUN_SUITE(check_suite);
    GREATEST_MAIN_END();
}
