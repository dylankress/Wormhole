//
// test_chunk_store.c
// Unit tests for chunk_store.c — init, put, has, get, dedup.
//

#include "../chunk_store.h"
#include "greatest.h"
#include <blake3.h>
#include <string.h>
#include <stdio.h>

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
static char g_env_buf[544];  // "USERPROFILE=" + path
#endif

static void setup_test_home(void)
{
#ifdef _WIN32
    char temp[512];
    GetTempPathA(sizeof(temp), temp);
    snprintf(g_test_home, sizeof(g_test_home), "%swh_test_%lu",
             temp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(g_test_home, NULL);

    snprintf(g_env_buf, sizeof(g_env_buf), "USERPROFILE=%s", g_test_home);
    _putenv(g_env_buf);
#else
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/wh_test_%d", (int)getpid());
    mkdir(g_test_home, 0755);
    setenv("HOME", g_test_home, 1);
#endif
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

// Helper: fill a 32-byte hash with a single value.
static void make_hash(uint8_t hash[WH_HASH_SIZE], uint8_t value)
{
    memset(hash, value, WH_HASH_SIZE);
}

// Helper: compute real Blake3 hash from data.
static void compute_hash(const uint8_t *data, uint32_t size, uint8_t hash[WH_HASH_SIZE])
{
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data, size);
    blake3_hasher_finalize(&hasher, hash, WH_HASH_SIZE);
}

// ============================================================================
// Tests
// ============================================================================

TEST test_init(void)
{
    ASSERT(ChunkStore_Init());
    PASS();
}

TEST test_put_has(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0xA1);

    uint8_t data[] = "Hello, chunk store!";
    uint32_t data_size = (uint32_t)(sizeof(data) - 1);

    ASSERT(ChunkStore_Put(hash, data, data_size));
    ASSERT(ChunkStore_Has(hash));
    PASS();
}

TEST test_put_get_roundtrip(void)
{
    const char *msg = "Round-trip test data for chunk store!";
    uint32_t msg_len = (uint32_t)strlen(msg);

    uint8_t hash[WH_HASH_SIZE];
    compute_hash((const uint8_t *)msg, msg_len, hash);

    ASSERT(ChunkStore_Put(hash, (const uint8_t *)msg, msg_len));

    uint8_t buf[WH_CHUNK_SIZE];
    uint32_t read_size = 0;
    ASSERT(ChunkStore_Get(hash, buf, &read_size));
    ASSERT_EQ(read_size, msg_len);
    ASSERT_MEM_EQ(buf, msg, msg_len);
    PASS();
}

TEST test_dedup(void)
{
    uint8_t data[] = "Deduplicated chunk data";
    uint32_t size = (uint32_t)(sizeof(data) - 1);

    uint8_t hash[WH_HASH_SIZE];
    compute_hash(data, size, hash);

    // Put the same hash twice — both should succeed
    ASSERT(ChunkStore_Put(hash, data, size));
    ASSERT(ChunkStore_Put(hash, data, size));
    ASSERT(ChunkStore_Has(hash));

    // Verify data is still intact
    uint8_t buf[WH_CHUNK_SIZE];
    uint32_t read_size = 0;
    ASSERT(ChunkStore_Get(hash, buf, &read_size));
    ASSERT_EQ(read_size, size);
    ASSERT_MEM_EQ(buf, data, size);
    PASS();
}

TEST test_has_nonexistent(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0xD4);

    // Never stored — should not exist
    ASSERT_FALSE(ChunkStore_Has(hash));
    PASS();
}

TEST test_get_nonexistent(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0xE5);

    uint8_t buf[WH_CHUNK_SIZE];
    uint32_t read_size = 0;

    // Should fail gracefully
    ASSERT_FALSE(ChunkStore_Get(hash, buf, &read_size));
    PASS();
}

TEST test_large_chunk(void)
{
    // Full 256 KB chunk
    uint8_t *data = (uint8_t *)malloc(WH_CHUNK_SIZE);
    ASSERT(data != NULL);
    memset(data, 0x42, WH_CHUNK_SIZE);

    uint8_t hash[WH_HASH_SIZE];
    compute_hash(data, WH_CHUNK_SIZE, hash);

    ASSERT(ChunkStore_Put(hash, data, WH_CHUNK_SIZE));
    ASSERT(ChunkStore_Has(hash));

    uint8_t *buf = (uint8_t *)malloc(WH_CHUNK_SIZE);
    ASSERT(buf != NULL);
    uint32_t read_size = 0;
    ASSERT(ChunkStore_Get(hash, buf, &read_size));
    ASSERT_EQ(read_size, (uint32_t)WH_CHUNK_SIZE);
    ASSERT_MEM_EQ(buf, data, WH_CHUNK_SIZE);

    free(data);
    free(buf);
    PASS();
}

TEST test_multiple_chunks(void)
{
    // Store several chunks with different hashes
    for (uint8_t i = 0; i < 5; i++)
    {
        uint8_t data[64];
        memset(data, i, sizeof(data));

        uint8_t hash[WH_HASH_SIZE];
        compute_hash(data, sizeof(data), hash);

        ASSERT(ChunkStore_Put(hash, data, sizeof(data)));
    }

    // Verify all exist and contain correct data
    for (uint8_t i = 0; i < 5; i++)
    {
        uint8_t data[64];
        memset(data, i, sizeof(data));

        uint8_t hash[WH_HASH_SIZE];
        compute_hash(data, sizeof(data), hash);

        ASSERT(ChunkStore_Has(hash));

        uint8_t buf[WH_CHUNK_SIZE];
        uint32_t read_size = 0;
        ASSERT(ChunkStore_Get(hash, buf, &read_size));
        ASSERT_EQ(read_size, 64u);

        ASSERT_MEM_EQ(buf, data, 64);
    }
    PASS();
}

// ============================================================================
// Suite
// ============================================================================

SUITE(chunk_store_suite)
{
    RUN_TEST(test_init);
    RUN_TEST(test_put_has);
    RUN_TEST(test_put_get_roundtrip);
    RUN_TEST(test_dedup);
    RUN_TEST(test_has_nonexistent);
    RUN_TEST(test_get_nonexistent);
    RUN_TEST(test_large_chunk);
    RUN_TEST(test_multiple_chunks);
}

// ============================================================================
// Replica metadata tests
// ============================================================================

TEST test_put_with_meta(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0x20);

    uint8_t peer_id[32];
    memset(peer_id, 0xAA, 32);

    uint8_t data[] = "chunk with meta";
    uint32_t size = (uint32_t)(sizeof(data) - 1);

    ASSERT(ChunkStore_PutWithMeta(hash, data, size, peer_id));
    ASSERT(ChunkStore_Has(hash));
    ASSERT_EQ(ChunkStore_GetReplicaCount(hash), 1u);
    PASS();
}

TEST test_put_with_meta_null_peer(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0x21);

    uint8_t data[] = "chunk no peer";
    uint32_t size = (uint32_t)(sizeof(data) - 1);

    ASSERT(ChunkStore_PutWithMeta(hash, data, size, NULL));
    ASSERT(ChunkStore_Has(hash));
    ASSERT_EQ(ChunkStore_GetReplicaCount(hash), 0u);
    PASS();
}

TEST test_replica_count_none(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0x22);

    // Never stored — replica count should be 0
    ASSERT_EQ(ChunkStore_GetReplicaCount(hash), 0u);
    PASS();
}

TEST test_set_replica_locations(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0x23);

    // Store a chunk first
    uint8_t data[] = "replica test data";
    ASSERT(ChunkStore_Put(hash, (const uint8_t *)data, (uint32_t)(sizeof(data) - 1)));

    // Set 3 different peer IDs
    uint8_t peer1[32], peer2[32], peer3[32];
    memset(peer1, 0x01, 32);
    memset(peer2, 0x02, 32);
    memset(peer3, 0x03, 32);

    ASSERT(ChunkStore_SetReplicaLocation(hash, peer1));
    ASSERT(ChunkStore_SetReplicaLocation(hash, peer2));
    ASSERT(ChunkStore_SetReplicaLocation(hash, peer3));

    ASSERT_EQ(ChunkStore_GetReplicaCount(hash), 3u);
    PASS();
}

TEST test_replica_dedup(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0x24);

    uint8_t data[] = "dedup replica test";
    ASSERT(ChunkStore_Put(hash, (const uint8_t *)data, (uint32_t)(sizeof(data) - 1)));

    uint8_t peer[32];
    memset(peer, 0xDD, 32);

    ASSERT(ChunkStore_SetReplicaLocation(hash, peer));
    ASSERT(ChunkStore_SetReplicaLocation(hash, peer));  // same peer again

    ASSERT_EQ(ChunkStore_GetReplicaCount(hash), 1u);
    PASS();
}

TEST test_get_replica_locations(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0x25);

    uint8_t data[] = "get replicas test";
    ASSERT(ChunkStore_Put(hash, (const uint8_t *)data, (uint32_t)(sizeof(data) - 1)));

    uint8_t peers[3][32];
    memset(peers[0], 0x10, 32);
    memset(peers[1], 0x20, 32);
    memset(peers[2], 0x30, 32);

    for (int i = 0; i < 3; i++)
    {
        ASSERT(ChunkStore_SetReplicaLocation(hash, peers[i]));
    }

    uint8_t out[MAX_REPLICAS][32];
    uint32_t count = ChunkStore_GetReplicaLocations(hash, out, MAX_REPLICAS);
    ASSERT_EQ(count, 3u);

    // Verify each peer is present (order may match insertion)
    for (int i = 0; i < 3; i++)
    {
        ASSERT_MEM_EQ(out[i], peers[i], 32);
    }
    PASS();
}

TEST test_replica_max(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0x26);

    uint8_t data[] = "max replicas test";
    ASSERT(ChunkStore_Put(hash, (const uint8_t *)data, (uint32_t)(sizeof(data) - 1)));

    // Set MAX_REPLICAS peer IDs
    for (int i = 0; i < MAX_REPLICAS; i++)
    {
        uint8_t peer[32];
        memset(peer, (uint8_t)(0x40 + i), 32);
        ASSERT(ChunkStore_SetReplicaLocation(hash, peer));
    }

    ASSERT_EQ(ChunkStore_GetReplicaCount(hash), (uint32_t)MAX_REPLICAS);

    // One more should fail
    uint8_t extra_peer[32];
    memset(extra_peer, 0xFF, 32);
    ASSERT_FALSE(ChunkStore_SetReplicaLocation(hash, extra_peer));
    PASS();
}

TEST test_remove_replica_location(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0x27);

    uint8_t data[] = "remove replica test";
    ASSERT(ChunkStore_Put(hash, (const uint8_t *)data, (uint32_t)(sizeof(data) - 1)));

    uint8_t peer1[32], peer2[32], peer3[32];
    memset(peer1, 0x01, 32);
    memset(peer2, 0x02, 32);
    memset(peer3, 0x03, 32);

    ASSERT(ChunkStore_SetReplicaLocation(hash, peer1));
    ASSERT(ChunkStore_SetReplicaLocation(hash, peer2));
    ASSERT(ChunkStore_SetReplicaLocation(hash, peer3));
    ASSERT_EQ(ChunkStore_GetReplicaCount(hash), 3u);

    // Remove peer2
    ASSERT(ChunkStore_RemoveReplicaLocation(hash, peer2));
    ASSERT_EQ(ChunkStore_GetReplicaCount(hash), 2u);

    // Verify remaining peers
    uint8_t out[MAX_REPLICAS][32];
    uint32_t count = ChunkStore_GetReplicaLocations(hash, out, MAX_REPLICAS);
    ASSERT_EQ(count, 2u);
    ASSERT_MEM_EQ(out[0], peer1, 32);
    ASSERT_MEM_EQ(out[1], peer3, 32);

    PASS();
}

TEST test_remove_replica_nonexistent_peer(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0x28);

    uint8_t data[] = "remove nonexistent test";
    ASSERT(ChunkStore_Put(hash, (const uint8_t *)data, (uint32_t)(sizeof(data) - 1)));

    uint8_t peer1[32], fake[32];
    memset(peer1, 0x01, 32);
    memset(fake, 0xEE, 32);

    ASSERT(ChunkStore_SetReplicaLocation(hash, peer1));
    ASSERT_EQ(ChunkStore_GetReplicaCount(hash), 1u);

    // Remove non-existent peer — should return FALSE
    ASSERT_FALSE(ChunkStore_RemoveReplicaLocation(hash, fake));
    ASSERT_EQ(ChunkStore_GetReplicaCount(hash), 1u);

    PASS();
}

TEST test_remove_last_replica(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0x29);

    uint8_t data[] = "remove last replica test";
    ASSERT(ChunkStore_Put(hash, (const uint8_t *)data, (uint32_t)(sizeof(data) - 1)));

    uint8_t peer1[32];
    memset(peer1, 0x01, 32);

    ASSERT(ChunkStore_SetReplicaLocation(hash, peer1));
    ASSERT_EQ(ChunkStore_GetReplicaCount(hash), 1u);

    // Remove the only peer
    ASSERT(ChunkStore_RemoveReplicaLocation(hash, peer1));
    ASSERT_EQ(ChunkStore_GetReplicaCount(hash), 0u);

    PASS();
}

SUITE(replica_suite)
{
    RUN_TEST(test_put_with_meta);
    RUN_TEST(test_put_with_meta_null_peer);
    RUN_TEST(test_replica_count_none);
    RUN_TEST(test_set_replica_locations);
    RUN_TEST(test_replica_dedup);
    RUN_TEST(test_get_replica_locations);
    RUN_TEST(test_replica_max);
    RUN_TEST(test_remove_replica_location);
    RUN_TEST(test_remove_replica_nonexistent_peer);
    RUN_TEST(test_remove_last_replica);
}

// ============================================================================
// Eviction tests
// ============================================================================

TEST test_total_size(void)
{
    // Store 3 chunks of known sizes
    uint8_t h1[WH_HASH_SIZE], h2[WH_HASH_SIZE], h3[WH_HASH_SIZE];
    make_hash(h1, 0x30);
    make_hash(h2, 0x31);
    make_hash(h3, 0x32);

    uint8_t d1[100], d2[200], d3[300];
    memset(d1, 0xA0, sizeof(d1));
    memset(d2, 0xA1, sizeof(d2));
    memset(d3, 0xA2, sizeof(d3));

    ASSERT(ChunkStore_Put(h1, d1, sizeof(d1)));
    ASSERT(ChunkStore_Put(h2, d2, sizeof(d2)));
    ASSERT(ChunkStore_Put(h3, d3, sizeof(d3)));

    uint64_t total = ChunkStore_GetTotalSize();
    // Total should include these 3 plus any chunks from earlier tests
    // At minimum, these 3 chunks contribute 600 bytes
    ASSERT(total >= 600);
    PASS();
}

TEST test_evict_frees_chunks(void)
{
    uint8_t d1[512], d2[512];
    memset(d1, 0xB0, sizeof(d1));
    memset(d2, 0xB1, sizeof(d2));

    uint8_t h1[WH_HASH_SIZE], h2[WH_HASH_SIZE];
    compute_hash(d1, sizeof(d1), h1);
    compute_hash(d2, sizeof(d2), h2);

    ASSERT(ChunkStore_Put(h1, d1, sizeof(d1)));
    ASSERT(ChunkStore_Put(h2, d2, sizeof(d2)));

    // Trigger access times via Get
    uint8_t buf[WH_CHUNK_SIZE];
    uint32_t sz = 0;
    ASSERT(ChunkStore_Get(h1, buf, &sz));
    ASSERT(ChunkStore_Get(h2, buf, &sz));

    // Evict everything in the store (previous tests leave other chunks)
    uint64_t total = ChunkStore_GetTotalSize();
    uint64_t freed = ChunkStore_Evict(total);
    ASSERT(freed >= 512);

    // Both should be gone since we evicted the entire store
    ASSERT_FALSE(ChunkStore_Has(h1));
    ASSERT_FALSE(ChunkStore_Has(h2));
    PASS();
}

TEST test_evict_prefers_replicated(void)
{
    uint8_t data_replicated[256], data_local[256];
    memset(data_replicated, 0xC0, sizeof(data_replicated));
    memset(data_local, 0xC1, sizeof(data_local));

    uint8_t h_replicated[WH_HASH_SIZE], h_local[WH_HASH_SIZE];
    compute_hash(data_replicated, sizeof(data_replicated), h_replicated);
    compute_hash(data_local, sizeof(data_local), h_local);

    // Store both chunks
    ASSERT(ChunkStore_Put(h_replicated, data_replicated, sizeof(data_replicated)));
    ASSERT(ChunkStore_Put(h_local, data_local, sizeof(data_local)));

    // Trigger access times via Get
    uint8_t buf[WH_CHUNK_SIZE];
    uint32_t sz = 0;
    ASSERT(ChunkStore_Get(h_replicated, buf, &sz));
    ASSERT(ChunkStore_Get(h_local, buf, &sz));

    // Set 3 replicas on h_replicated (making it safer to evict)
    uint8_t p1[32], p2[32], p3[32];
    memset(p1, 0xD1, 32);
    memset(p2, 0xD2, 32);
    memset(p3, 0xD3, 32);
    ASSERT(ChunkStore_SetReplicaLocation(h_replicated, p1));
    ASSERT(ChunkStore_SetReplicaLocation(h_replicated, p2));
    ASSERT(ChunkStore_SetReplicaLocation(h_replicated, p3));

    // h_local has 0 replicas — should be kept
    // h_replicated has 3 replicas — should be evicted first

    // Evict just one chunk's worth
    uint64_t freed = ChunkStore_Evict(256);
    ASSERT(freed >= 256);

    // The replicated one should be gone, the local one should remain
    ASSERT_FALSE(ChunkStore_Has(h_replicated));
    ASSERT(ChunkStore_Has(h_local));
    PASS();
}

TEST test_evict_zero(void)
{
    uint64_t freed = ChunkStore_Evict(0);
    ASSERT_EQ(freed, 0ULL);
    PASS();
}

SUITE(eviction_suite)
{
    RUN_TEST(test_total_size);
    RUN_TEST(test_evict_frees_chunks);
    RUN_TEST(test_evict_prefers_replicated);
    RUN_TEST(test_evict_zero);
}

// ============================================================================
// Quota enforcement tests
// ============================================================================

TEST test_quota_enforcement(void)
{
    // Reset tracking to pick up current store size, then set quota
    // to allow only 400 more bytes beyond what's already stored
    ChunkStore_ResetQuotaTracking();
    uint64_t existing = ChunkStore_GetTotalSize();
    ChunkStore_SetQuotaBytes(existing + 400);

    // Store a 200-byte chunk — should succeed
    uint8_t d1[200];
    memset(d1, 0xE0, sizeof(d1));
    uint8_t h1[WH_HASH_SIZE];
    compute_hash(d1, sizeof(d1), h1);
    ASSERT(ChunkStore_Put(h1, d1, sizeof(d1)));

    // Store another 200-byte chunk — should succeed (exactly at limit)
    uint8_t d2[200];
    memset(d2, 0xE1, sizeof(d2));
    uint8_t h2[WH_HASH_SIZE];
    compute_hash(d2, sizeof(d2), h2);
    ASSERT(ChunkStore_Put(h2, d2, sizeof(d2)));

    // Store a 200-byte chunk — should fail (would exceed quota by 200)
    uint8_t d3[200];
    memset(d3, 0xE2, sizeof(d3));
    uint8_t h3[WH_HASH_SIZE];
    compute_hash(d3, sizeof(d3), h3);
    ASSERT_FALSE(ChunkStore_Put(h3, d3, sizeof(d3)));
    ASSERT_FALSE(ChunkStore_Has(h3));

    // Reset quota override
    ChunkStore_SetQuotaBytes(0);
    ChunkStore_ResetQuotaTracking();
    PASS();
}

TEST test_quota_allows_dedup(void)
{
    // Reset tracking and set quota to allow 200 more bytes
    ChunkStore_ResetQuotaTracking();
    uint64_t existing = ChunkStore_GetTotalSize();
    ChunkStore_SetQuotaBytes(existing + 200);

    uint8_t d1[200];
    memset(d1, 0xF0, sizeof(d1));
    uint8_t h1[WH_HASH_SIZE];
    compute_hash(d1, sizeof(d1), h1);
    ASSERT(ChunkStore_Put(h1, d1, sizeof(d1)));

    // Put same chunk again — should succeed (dedup, no new storage)
    ASSERT(ChunkStore_Put(h1, d1, sizeof(d1)));

    ChunkStore_SetQuotaBytes(0);
    ChunkStore_ResetQuotaTracking();
    PASS();
}

SUITE(quota_suite)
{
    RUN_TEST(test_quota_enforcement);
    RUN_TEST(test_quota_allows_dedup);
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    setup_test_home();
    atexit(cleanup_test_home);

    GREATEST_MAIN_BEGIN();
    RUN_SUITE(chunk_store_suite);
    RUN_SUITE(replica_suite);
    RUN_SUITE(eviction_suite);
    RUN_SUITE(quota_suite);
    GREATEST_MAIN_END();
}
