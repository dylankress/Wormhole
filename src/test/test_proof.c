//
// test_proof.c
// Unit tests for proof.c — proof-of-storage computation and caching.
//

#include "../proof.h"
#include "greatest.h"
#include <string.h>
#include <stdio.h>
#include <sodium.h>

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
    snprintf(g_test_home, sizeof(g_test_home), "%swh_test_proof_%lu",
             temp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(g_test_home, NULL);

    snprintf(g_env_buf, sizeof(g_env_buf), "USERPROFILE=%s", g_test_home);
    _putenv(g_env_buf);
#else
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/wh_test_proof_%d", (int)getpid());
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

// ============================================================================
// Tests
// ============================================================================

TEST test_proof_compute_deterministic(void)
{
    uint8_t data[] = "Hello, proof-of-storage!";
    uint32_t data_size = (uint32_t)(sizeof(data) - 1);

    uint8_t seed[WH_HASH_SIZE];
    make_hash(seed, 0x42);

    uint8_t proof1[WH_HASH_SIZE];
    uint8_t proof2[WH_HASH_SIZE];

    Proof_Compute(data, data_size, seed, proof1);
    Proof_Compute(data, data_size, seed, proof2);

    ASSERT_MEM_EQ(proof1, proof2, WH_HASH_SIZE);
    PASS();
}

TEST test_proof_different_seed(void)
{
    uint8_t data[] = "Same data, different seeds";
    uint32_t data_size = (uint32_t)(sizeof(data) - 1);

    uint8_t seed1[WH_HASH_SIZE];
    uint8_t seed2[WH_HASH_SIZE];
    make_hash(seed1, 0x01);
    make_hash(seed2, 0x02);

    uint8_t proof1[WH_HASH_SIZE];
    uint8_t proof2[WH_HASH_SIZE];

    Proof_Compute(data, data_size, seed1, proof1);
    Proof_Compute(data, data_size, seed2, proof2);

    // Different seeds must produce different proofs
    ASSERT_FALSE(memcmp(proof1, proof2, WH_HASH_SIZE) == 0);
    PASS();
}

TEST test_proof_different_data(void)
{
    uint8_t data1[] = "First chunk data";
    uint8_t data2[] = "Second chunk data";

    uint8_t seed[WH_HASH_SIZE];
    make_hash(seed, 0xAA);

    uint8_t proof1[WH_HASH_SIZE];
    uint8_t proof2[WH_HASH_SIZE];

    Proof_Compute(data1, (uint32_t)(sizeof(data1) - 1), seed, proof1);
    Proof_Compute(data2, (uint32_t)(sizeof(data2) - 1), seed, proof2);

    // Different data must produce different proofs
    ASSERT_FALSE(memcmp(proof1, proof2, WH_HASH_SIZE) == 0);
    PASS();
}

TEST test_proof_precompute_and_lookup(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0xB1);

    uint8_t data[] = "Chunk data for precompute test";
    uint32_t data_size = (uint32_t)(sizeof(data) - 1);

    ASSERT(Proof_PrecomputeAndCache(hash, data, data_size));

    // Read back the cache file to extract a stored seed
    // Build the cache file path manually to read seeds
    char path[512];
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\.wormhole\\proofs\\", g_test_home);
#else
    snprintf(path, sizeof(path), "%s/.wormhole/proofs/", g_test_home);
#endif

    // The cache exists — now read the first seed from it
    char cache_path[MAX_PATH];
    {
        // Build hex hash to construct path
        static const char digits[] = "0123456789abcdef";
        char hex[65];
        for (int i = 0; i < WH_HASH_SIZE; i++)
        {
            hex[i * 2]     = digits[(hash[i] >> 4) & 0x0F];
            hex[i * 2 + 1] = digits[hash[i] & 0x0F];
        }
        hex[64] = '\0';
#ifdef _WIN32
        snprintf(cache_path, sizeof(cache_path), "%s\\.wormhole\\proofs\\%s", g_test_home, hex);
#else
        snprintf(cache_path, sizeof(cache_path), "%s/.wormhole/proofs/%s", g_test_home, hex);
#endif
    }

    FILE *fh = fopen(cache_path, "rb");
    ASSERT(fh != NULL);

    // Skip count (4 bytes)
    uint8_t count_buf[4];
    ASSERT_EQ(fread(count_buf, 1, 4, fh), 4u);

    // Read first seed
    uint8_t first_seed[WH_HASH_SIZE];
    ASSERT_EQ(fread(first_seed, 1, WH_HASH_SIZE, fh), (size_t)WH_HASH_SIZE);
    fclose(fh);

    // Lookup the cached proof with the extracted seed
    uint8_t cached_proof[WH_HASH_SIZE];
    ASSERT(Proof_LookupCached(hash, first_seed, cached_proof));

    // Verify the cached proof matches a fresh computation
    uint8_t expected_proof[WH_HASH_SIZE];
    Proof_Compute(data, data_size, first_seed, expected_proof);
    ASSERT_MEM_EQ(cached_proof, expected_proof, WH_HASH_SIZE);

    PASS();
}

TEST test_proof_lookup_miss(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0xC2);

    uint8_t data[] = "Data for cache miss test";
    uint32_t data_size = (uint32_t)(sizeof(data) - 1);

    ASSERT(Proof_PrecomputeAndCache(hash, data, data_size));

    // Use a seed that was definitely not generated by PrecomputeAndCache
    uint8_t unknown_seed[WH_HASH_SIZE];
    memset(unknown_seed, 0xFF, WH_HASH_SIZE);

    uint8_t proof_out[WH_HASH_SIZE];
    ASSERT_FALSE(Proof_LookupCached(hash, unknown_seed, proof_out));
    PASS();
}

TEST test_proof_delete_cache(void)
{
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0xD3);

    uint8_t data[] = "Data for delete test";
    uint32_t data_size = (uint32_t)(sizeof(data) - 1);

    ASSERT(Proof_PrecomputeAndCache(hash, data, data_size));

    // Read one seed from cache so we can try to look it up later
    char cache_path[MAX_PATH];
    {
        static const char digits[] = "0123456789abcdef";
        char hex[65];
        for (int i = 0; i < WH_HASH_SIZE; i++)
        {
            hex[i * 2]     = digits[(hash[i] >> 4) & 0x0F];
            hex[i * 2 + 1] = digits[hash[i] & 0x0F];
        }
        hex[64] = '\0';
#ifdef _WIN32
        snprintf(cache_path, sizeof(cache_path), "%s\\.wormhole\\proofs\\%s", g_test_home, hex);
#else
        snprintf(cache_path, sizeof(cache_path), "%s/.wormhole/proofs/%s", g_test_home, hex);
#endif
    }

    FILE *fh = fopen(cache_path, "rb");
    ASSERT(fh != NULL);
    uint8_t count_buf[4];
    fread(count_buf, 1, 4, fh);
    uint8_t seed[WH_HASH_SIZE];
    fread(seed, 1, WH_HASH_SIZE, fh);
    fclose(fh);

    // Verify lookup works before delete
    uint8_t proof_out[WH_HASH_SIZE];
    ASSERT(Proof_LookupCached(hash, seed, proof_out));

    // Delete cache
    Proof_DeleteCache(hash);

    // Lookup should now fail
    ASSERT_FALSE(Proof_LookupCached(hash, seed, proof_out));
    PASS();
}

TEST test_proof_no_cache_file(void)
{
    // Lookup on a hash that was never cached
    uint8_t hash[WH_HASH_SIZE];
    make_hash(hash, 0xE4);

    uint8_t seed[WH_HASH_SIZE];
    make_hash(seed, 0x01);

    uint8_t proof_out[WH_HASH_SIZE];
    ASSERT_FALSE(Proof_LookupCached(hash, seed, proof_out));
    PASS();
}

// ============================================================================
// Suite
// ============================================================================

SUITE(proof_suite)
{
    RUN_TEST(test_proof_compute_deterministic);
    RUN_TEST(test_proof_different_seed);
    RUN_TEST(test_proof_different_data);
    RUN_TEST(test_proof_precompute_and_lookup);
    RUN_TEST(test_proof_lookup_miss);
    RUN_TEST(test_proof_delete_cache);
    RUN_TEST(test_proof_no_cache_file);
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    if (sodium_init() < 0)
    {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return 1;
    }

    setup_test_home();
    atexit(cleanup_test_home);

    GREATEST_MAIN_BEGIN();
    RUN_SUITE(proof_suite);
    GREATEST_MAIN_END();
}
