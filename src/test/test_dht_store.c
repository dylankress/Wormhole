//
// test_dht_store.c
// Unit tests for dht_store.c — DHT local value store.
//

#include "../dht/dht_store.h"
#include "greatest.h"
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
// Helpers
// ============================================================================

static void fill_key(uint8_t key[32], uint8_t value)
{
    memset(key, value, 32);
}

static void make_location(DHT_LOCATION *loc, uint8_t id_byte, uint16_t port)
{
    memset(loc, 0, sizeof(DHT_LOCATION));
    memset(loc->node_id, id_byte, 32);
    loc->addr_type = 0x04;
    loc->addr[0] = 10;
    loc->addr[1] = 0;
    loc->addr[2] = 0;
    loc->addr[3] = id_byte;
    loc->port = port;
}

static char test_dir[256];

static void setup_test_dir(void)
{
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = ".";
    snprintf(test_dir, sizeof(test_dir), "%s\\wormhole_test_dhtstore_%u",
             tmp, (unsigned)GetCurrentProcessId());
    _mkdir(test_dir);
#else
    snprintf(test_dir, sizeof(test_dir), "/tmp/wormhole_test_dhtstore_%d",
             (int)getpid());
    mkdir(test_dir, 0755);
#endif
}

// ============================================================================
// Tests
// ============================================================================

TEST test_store_init(void)
{
    static DHT_VALUE_STORE store;
    DhtStore_Init(&store);
    ASSERT_EQ(store.count, 0u);
    ASSERT_EQ(DhtStore_GetCount(&store), 0u);
    PASS();
}

TEST test_store_put_get(void)
{
    static DHT_VALUE_STORE store;
    DhtStore_Init(&store);

    uint8_t key[32];
    fill_key(key, 0xAA);

    DHT_LOCATION loc;
    make_location(&loc, 0x01, 4568);

    ASSERT(DhtStore_Put(&store, key, &loc, 1));
    ASSERT_EQ(DhtStore_GetCount(&store), 1u);
    ASSERT(DhtStore_Has(&store, key));

    DHT_LOCATION out[4];
    uint32_t count = DhtStore_Get(&store, key, out, 4);
    ASSERT_EQ(count, 1u);
    ASSERT(memcmp(out[0].node_id, loc.node_id, 32) == 0);
    ASSERT_EQ(out[0].port, 4568);

    PASS();
}

TEST test_store_merge_locations(void)
{
    static DHT_VALUE_STORE store;
    DhtStore_Init(&store);

    uint8_t key[32];
    fill_key(key, 0xBB);

    // Add first location
    DHT_LOCATION loc1;
    make_location(&loc1, 0x01, 4568);
    ASSERT(DhtStore_Put(&store, key, &loc1, 1));

    // Add second location for same key
    DHT_LOCATION loc2;
    make_location(&loc2, 0x02, 4569);
    ASSERT(DhtStore_Put(&store, key, &loc2, 1));

    // Should still be 1 entry, but with 2 locations
    ASSERT_EQ(DhtStore_GetCount(&store), 1u);

    DHT_LOCATION out[4];
    uint32_t count = DhtStore_Get(&store, key, out, 4);
    ASSERT_EQ(count, 2u);

    PASS();
}

TEST test_store_dedup_locations(void)
{
    static DHT_VALUE_STORE store;
    DhtStore_Init(&store);

    uint8_t key[32];
    fill_key(key, 0xCC);

    DHT_LOCATION loc;
    make_location(&loc, 0x01, 4568);

    // Add same location twice
    ASSERT(DhtStore_Put(&store, key, &loc, 1));
    ASSERT(DhtStore_Put(&store, key, &loc, 1));

    DHT_LOCATION out[4];
    uint32_t count = DhtStore_Get(&store, key, out, 4);
    ASSERT_EQ(count, 1u);  // Deduped

    PASS();
}

TEST test_store_not_found(void)
{
    static DHT_VALUE_STORE store;
    DhtStore_Init(&store);

    uint8_t key[32];
    fill_key(key, 0xDD);

    ASSERT_FALSE(DhtStore_Has(&store, key));

    DHT_LOCATION out[4];
    uint32_t count = DhtStore_Get(&store, key, out, 4);
    ASSERT_EQ(count, 0u);

    PASS();
}

TEST test_store_multiple_keys(void)
{
    static DHT_VALUE_STORE store;
    DhtStore_Init(&store);

    for (uint8_t i = 0; i < 10; i++)
    {
        uint8_t key[32];
        fill_key(key, i);

        DHT_LOCATION loc;
        make_location(&loc, i, 4568 + i);

        ASSERT(DhtStore_Put(&store, key, &loc, 1));
    }

    ASSERT_EQ(DhtStore_GetCount(&store), 10u);

    // Verify each key
    for (uint8_t i = 0; i < 10; i++)
    {
        uint8_t key[32];
        fill_key(key, i);
        ASSERT(DhtStore_Has(&store, key));
    }

    PASS();
}

TEST test_store_expire_old(void)
{
    static DHT_VALUE_STORE store;
    DhtStore_Init(&store);

    uint8_t key[32];
    fill_key(key, 0xEE);

    DHT_LOCATION loc;
    make_location(&loc, 0x01, 4568);
    ASSERT(DhtStore_Put(&store, key, &loc, 1));

    // Manually set last_refreshed to long ago
    store.entries[0].last_refreshed = time(NULL) - DHT_STORE_EXPIRY_SEC - 100;

    DhtStore_ExpireOld(&store);
    ASSERT_EQ(DhtStore_GetCount(&store), 0u);

    PASS();
}

TEST test_store_expire_keeps_fresh(void)
{
    static DHT_VALUE_STORE store;
    DhtStore_Init(&store);

    uint8_t key[32];
    fill_key(key, 0xFF);

    DHT_LOCATION loc;
    make_location(&loc, 0x01, 4568);
    ASSERT(DhtStore_Put(&store, key, &loc, 1));

    // Fresh entry should not be expired
    DhtStore_ExpireOld(&store);
    ASSERT_EQ(DhtStore_GetCount(&store), 1u);

    PASS();
}

TEST test_store_remove(void)
{
    static DHT_VALUE_STORE store;
    DhtStore_Init(&store);

    uint8_t key[32];
    fill_key(key, 0xAB);

    DHT_LOCATION loc;
    make_location(&loc, 0x01, 4568);

    ASSERT(DhtStore_Put(&store, key, &loc, 1));
    ASSERT_EQ(DhtStore_GetCount(&store), 1u);
    ASSERT(DhtStore_Has(&store, key));

    // Remove it
    ASSERT(DhtStore_Remove(&store, key));
    ASSERT_EQ(DhtStore_GetCount(&store), 0u);
    ASSERT_FALSE(DhtStore_Has(&store, key));

    // Removing again should return FALSE
    ASSERT_FALSE(DhtStore_Remove(&store, key));

    PASS();
}

TEST test_store_remove_middle(void)
{
    static DHT_VALUE_STORE store;
    DhtStore_Init(&store);

    uint8_t key1[32], key2[32], key3[32];
    fill_key(key1, 0x01);
    fill_key(key2, 0x02);
    fill_key(key3, 0x03);

    DHT_LOCATION loc1, loc2, loc3;
    make_location(&loc1, 0x01, 4568);
    make_location(&loc2, 0x02, 4569);
    make_location(&loc3, 0x03, 4570);

    DhtStore_Put(&store, key1, &loc1, 1);
    DhtStore_Put(&store, key2, &loc2, 1);
    DhtStore_Put(&store, key3, &loc3, 1);
    ASSERT_EQ(DhtStore_GetCount(&store), 3u);

    // Remove middle entry
    ASSERT(DhtStore_Remove(&store, key2));
    ASSERT_EQ(DhtStore_GetCount(&store), 2u);
    ASSERT(DhtStore_Has(&store, key1));
    ASSERT_FALSE(DhtStore_Has(&store, key2));
    ASSERT(DhtStore_Has(&store, key3));

    PASS();
}

TEST test_store_save_load(void)
{
    setup_test_dir();

    char path[512];
    snprintf(path, sizeof(path), "%s%cvalue_store.bin",
             test_dir,
#ifdef _WIN32
             '\\'
#else
             '/'
#endif
    );

    // Create and save
    static DHT_VALUE_STORE store;
    DhtStore_Init(&store);

    uint8_t key1[32], key2[32];
    fill_key(key1, 0x11);
    fill_key(key2, 0x22);

    DHT_LOCATION loc1, loc2;
    make_location(&loc1, 0x01, 4568);
    make_location(&loc2, 0x02, 4569);

    DhtStore_Put(&store, key1, &loc1, 1);
    DhtStore_Put(&store, key2, &loc2, 1);

    ASSERT(DhtStore_Save(&store, path));

    // Load into fresh store
    static DHT_VALUE_STORE loaded;
    DhtStore_Init(&loaded);
    ASSERT(DhtStore_Load(&loaded, path));

    ASSERT_EQ(DhtStore_GetCount(&loaded), 2u);
    ASSERT(DhtStore_Has(&loaded, key1));
    ASSERT(DhtStore_Has(&loaded, key2));

    DHT_LOCATION out[4];
    uint32_t count = DhtStore_Get(&loaded, key1, out, 4);
    ASSERT_EQ(count, 1u);
    ASSERT_EQ(out[0].port, 4568);

    // Cleanup
    remove(path);
#ifdef _WIN32
    _rmdir(test_dir);
#else
    rmdir(test_dir);
#endif

    PASS();
}

TEST test_store_lru_eviction_at_capacity(void)
{
    static DHT_VALUE_STORE store;
    DhtStore_Init(&store);

    // Fill store to capacity
    for (uint32_t i = 0; i < DHT_STORE_CAPACITY; i++)
    {
        uint8_t key[32];
        memset(key, 0, 32);
        key[0] = (uint8_t)(i & 0xFF);
        key[1] = (uint8_t)((i >> 8) & 0xFF);

        DHT_LOCATION loc;
        make_location(&loc, (uint8_t)(i & 0xFF), (uint16_t)(4000 + (i % 1000)));

        ASSERT(DhtStore_Put(&store, key, &loc, 1));
    }
    ASSERT_EQ(DhtStore_GetCount(&store), (uint32_t)DHT_STORE_CAPACITY);

    // Make entry 0 the oldest by backdating its last_refreshed
    store.entries[0].last_refreshed = time(NULL) - 99999;

    // Remember the oldest entry's key
    uint8_t oldest_key[32];
    memcpy(oldest_key, store.entries[0].key, 32);

    // Insert a new entry — should evict the oldest
    uint8_t new_key[32];
    memset(new_key, 0xFF, 32);

    DHT_LOCATION new_loc;
    make_location(&new_loc, 0xFE, 9999);

    ASSERT(DhtStore_Put(&store, new_key, &new_loc, 1));

    // Count should still be at capacity
    ASSERT_EQ(DhtStore_GetCount(&store), (uint32_t)DHT_STORE_CAPACITY);

    // New entry should be present
    ASSERT(DhtStore_Has(&store, new_key));

    // Oldest entry should have been evicted
    ASSERT_FALSE(DhtStore_Has(&store, oldest_key));

    PASS();
}

TEST test_store_load_overflow_locations(void)
{
    // Bug: if an entry has location_count > DHT_STORE_MAX_LOCATIONS on disk,
    // the load code must skip excess locations so subsequent entries aren't corrupted.
    setup_test_dir();

    char path[512];
    snprintf(path, sizeof(path), "%s%coverflow_store.bin",
             test_dir,
#ifdef _WIN32
             '\\'
#else
             '/'
#endif
    );

    // Manually write a store file with an entry that has 20 locations (exceeds max 16),
    // followed by a second normal entry. Verify both load correctly.
    FILE *fh = fopen(path, "wb");
    ASSERT(fh != NULL);

    // Header: [4B magic][4B version][4B count=2]
    uint8_t header[12];
    header[0] = 0x44; header[1] = 0x48; header[2] = 0x56; header[3] = 0x53; // "DHVS" LE
    header[4] = 1; header[5] = 0; header[6] = 0; header[7] = 0; // version 1
    header[8] = 2; header[9] = 0; header[10] = 0; header[11] = 0; // count 2
    fwrite(header, 1, 12, fh);

    // Entry 1: key=0xAA..., 20 locations
    uint8_t key1[32]; memset(key1, 0xAA, 32);
    fwrite(key1, 1, 32, fh);
    // stored_at (8 bytes LE)
    uint8_t ts[8]; memset(ts, 0, 8); ts[0] = 100;
    fwrite(ts, 1, 8, fh);
    // last_refreshed (8 bytes LE) — set to recent time so it doesn't expire
    uint64_t recent = (uint64_t)time(NULL);
    for (int b = 0; b < 8; b++) ts[b] = (uint8_t)(recent >> (b * 8));
    fwrite(ts, 1, 8, fh);
    // location_count = 20
    uint8_t lc[4] = {20, 0, 0, 0};
    fwrite(lc, 1, 4, fh);
    // 20 locations, each 51 bytes: [32B node_id][1B addr_type][16B addr][2B port]
    for (int j = 0; j < 20; j++)
    {
        uint8_t loc[51];
        memset(loc, 0, 51);
        memset(loc, (uint8_t)(j + 1), 32); // node_id
        loc[32] = 0x04; // addr_type IPv4
        loc[33] = 10; loc[34] = 0; loc[35] = 0; loc[36] = (uint8_t)(j + 1); // addr
        uint16_t port = (uint16_t)(5000 + j);
        loc[49] = (uint8_t)(port & 0xFF);
        loc[50] = (uint8_t)(port >> 8);
        fwrite(loc, 1, 51, fh);
    }

    // Entry 2: key=0xBB..., 1 location (normal entry that must survive)
    uint8_t key2[32]; memset(key2, 0xBB, 32);
    fwrite(key2, 1, 32, fh);
    // stored_at
    memset(ts, 0, 8); ts[0] = 200;
    fwrite(ts, 1, 8, fh);
    // last_refreshed
    for (int b = 0; b < 8; b++) ts[b] = (uint8_t)(recent >> (b * 8));
    fwrite(ts, 1, 8, fh);
    // location_count = 1
    uint8_t lc2[4] = {1, 0, 0, 0};
    fwrite(lc2, 1, 4, fh);
    // 1 location
    uint8_t loc2[51];
    memset(loc2, 0, 51);
    memset(loc2, 0x99, 32); // node_id
    loc2[32] = 0x04;
    loc2[33] = 192; loc2[34] = 168; loc2[35] = 1; loc2[36] = 1;
    loc2[49] = (uint8_t)(7777 & 0xFF);
    loc2[50] = (uint8_t)(7777 >> 8);
    fwrite(loc2, 1, 51, fh);

    fclose(fh);

    // Load and verify
    static DHT_VALUE_STORE store;
    DhtStore_Init(&store);
    ASSERT(DhtStore_Load(&store, path));

    // Both entries should be loaded
    ASSERT_EQ(DhtStore_GetCount(&store), 2u);

    // Entry 1: capped to 16 locations
    ASSERT(DhtStore_Has(&store, key1));
    DHT_LOCATION out[DHT_STORE_MAX_LOCATIONS];
    uint32_t count = DhtStore_Get(&store, key1, out, DHT_STORE_MAX_LOCATIONS);
    ASSERT_EQ(count, (uint32_t)DHT_STORE_MAX_LOCATIONS);

    // Entry 2: must be intact (not corrupted by overflow)
    ASSERT(DhtStore_Has(&store, key2));
    DHT_LOCATION out2[4];
    uint32_t count2 = DhtStore_Get(&store, key2, out2, 4);
    ASSERT_EQ(count2, 1u);
    ASSERT_EQ(out2[0].port, 7777);
    // Verify node_id
    uint8_t expected_node[32];
    memset(expected_node, 0x99, 32);
    ASSERT_MEM_EQ(out2[0].node_id, expected_node, 32);

    // Cleanup
    remove(path);
#ifdef _WIN32
    _rmdir(test_dir);
#else
    rmdir(test_dir);
#endif

    PASS();
}

// ============================================================================
// Test suite
// ============================================================================

SUITE(dht_store_suite)
{
    RUN_TEST(test_store_init);
    RUN_TEST(test_store_put_get);
    RUN_TEST(test_store_merge_locations);
    RUN_TEST(test_store_dedup_locations);
    RUN_TEST(test_store_not_found);
    RUN_TEST(test_store_multiple_keys);
    RUN_TEST(test_store_expire_old);
    RUN_TEST(test_store_expire_keeps_fresh);
    RUN_TEST(test_store_remove);
    RUN_TEST(test_store_remove_middle);
    RUN_TEST(test_store_save_load);
    RUN_TEST(test_store_lru_eviction_at_capacity);
    RUN_TEST(test_store_load_overflow_locations);
}

int main(int argc, char **argv)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(dht_store_suite);
    GREATEST_MAIN_END();
}
