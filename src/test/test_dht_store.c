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
    RUN_TEST(test_store_save_load);
}

int main(int argc, char **argv)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(dht_store_suite);
    GREATEST_MAIN_END();
}
