//
// test_erasure.c
// Unit tests for erasure.c — EC encoding, reconstruction, serialization.
//

#include "../erasure.h"
#include "../chunk_store.h"
#include "../manifest.h"
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
    snprintf(g_test_home, sizeof(g_test_home), "%swh_ec_test_%lu",
             temp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(g_test_home, NULL);

    snprintf(g_env_buf, sizeof(g_env_buf), "USERPROFILE=%s", g_test_home);
    _putenv(g_env_buf);
#else
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/wh_ec_test_%d", (int)getpid());
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
    FILE_MANIFEST *m = Manifest_Create("test_ec.bin", file_size);
    if (!m) return NULL;

    // Allocate and store chunk data
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
    // We can't easily delete via the ChunkStore API, so use file operations
    // Build the path manually using the same scheme as chunk_store.c
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
// Tests
// ============================================================================

TEST test_ec_encode_basic(void)
{
    uint8_t *chunk_data[8];
    FILE_MANIFEST *m = create_test_manifest(8, chunk_data);
    ASSERT(m != NULL);
    ASSERT_EQ(m->chunk_count, 8u);

    // Encode with RS(4,2)
    EC_GROUP *group = ErasureCoding_Encode(m, 4, 2);
    ASSERT(group != NULL);
    ASSERT_EQ(group->k, 4);
    ASSERT_EQ(group->m, 2);
    ASSERT_EQ(group->stripe_count, 2u);  // 8 chunks / 4 = 2 stripes

    // Verify stripe structure
    ASSERT_EQ(group->stripes[0].data_chunk_start, 0u);
    ASSERT_EQ(group->stripes[0].data_chunk_count, 4u);
    ASSERT_EQ(group->stripes[1].data_chunk_start, 4u);
    ASSERT_EQ(group->stripes[1].data_chunk_count, 4u);

    // Verify parity chunks exist in store
    for (uint32_t s = 0; s < group->stripe_count; s++)
    {
        for (uint8_t p = 0; p < group->m; p++)
        {
            ASSERT(ChunkStore_Has(group->stripes[s].parity_hashes[p]));
            ASSERT_EQ(group->stripes[s].parity_sizes[p], (uint32_t)WH_CHUNK_SIZE);
        }
    }

    for (int i = 0; i < 8; i++) free(chunk_data[i]);
    ErasureCoding_DestroyGroup(group);
    Manifest_Destroy(m);
    PASS();
}

TEST test_ec_reconstruct_chunk(void)
{
    uint8_t *chunk_data[8];
    FILE_MANIFEST *m = create_test_manifest(8, chunk_data);
    ASSERT(m != NULL);

    EC_GROUP *group = ErasureCoding_Encode(m, 4, 2);
    ASSERT(group != NULL);

    // Save original data for chunk 1
    uint8_t *original = (uint8_t *)malloc(WH_CHUNK_SIZE);
    ASSERT(original != NULL);
    memcpy(original, chunk_data[1], WH_CHUNK_SIZE);

    // Delete chunk 1 from store
    delete_chunk_from_store(m->chunks[1].hash);
    ASSERT_FALSE(ChunkStore_Has(m->chunks[1].hash));

    // Reconstruct
    ASSERT(ErasureCoding_ReconstructChunk(1, group, m));

    // Verify reconstruction matches original
    ASSERT(ChunkStore_Has(m->chunks[1].hash));
    uint8_t *recovered = (uint8_t *)malloc(WH_CHUNK_SIZE);
    uint32_t recovered_size = 0;
    ASSERT(ChunkStore_Get(m->chunks[1].hash, recovered, &recovered_size));
    ASSERT_EQ(recovered_size, (uint32_t)WH_CHUNK_SIZE);
    ASSERT_MEM_EQ(recovered, original, WH_CHUNK_SIZE);

    free(original);
    free(recovered);
    for (int i = 0; i < 8; i++) free(chunk_data[i]);
    ErasureCoding_DestroyGroup(group);
    Manifest_Destroy(m);
    PASS();
}

TEST test_ec_serialize_deserialize(void)
{
    uint8_t *chunk_data[8];
    FILE_MANIFEST *m = create_test_manifest(8, chunk_data);
    ASSERT(m != NULL);

    EC_GROUP *group = ErasureCoding_Encode(m, 4, 2);
    ASSERT(group != NULL);

    // Serialize
    size_t sz = 0;
    uint8_t *buf = ErasureCoding_SerializeGroup(group, &sz);
    ASSERT(buf != NULL);
    ASSERT(sz > 0);

    // Deserialize
    EC_GROUP *group2 = ErasureCoding_DeserializeGroup(buf, sz);
    ASSERT(group2 != NULL);

    // Verify fields match
    ASSERT_EQ(group2->k, group->k);
    ASSERT_EQ(group2->m, group->m);
    ASSERT_EQ(group2->stripe_count, group->stripe_count);

    for (uint32_t s = 0; s < group->stripe_count; s++)
    {
        ASSERT_EQ(group2->stripes[s].data_chunk_start,
                   group->stripes[s].data_chunk_start);
        ASSERT_EQ(group2->stripes[s].data_chunk_count,
                   group->stripes[s].data_chunk_count);
        for (uint8_t p = 0; p < group->m; p++)
        {
            ASSERT_MEM_EQ(group2->stripes[s].parity_hashes[p],
                           group->stripes[s].parity_hashes[p], WH_HASH_SIZE);
            ASSERT_EQ(group2->stripes[s].parity_sizes[p],
                       group->stripes[s].parity_sizes[p]);
        }
    }

    free(buf);
    for (int i = 0; i < 8; i++) free(chunk_data[i]);
    ErasureCoding_DestroyGroup(group);
    ErasureCoding_DestroyGroup(group2);
    Manifest_Destroy(m);
    PASS();
}

TEST test_ec_partial_last_stripe(void)
{
    // 5 chunks: 1 full stripe of 4 + 1 partial stripe of 1
    uint8_t *chunk_data[5];
    FILE_MANIFEST *m = create_test_manifest(5, chunk_data);
    ASSERT(m != NULL);
    ASSERT_EQ(m->chunk_count, 5u);

    EC_GROUP *group = ErasureCoding_Encode(m, 4, 2);
    ASSERT(group != NULL);
    ASSERT_EQ(group->stripe_count, 2u);

    // First stripe: full
    ASSERT_EQ(group->stripes[0].data_chunk_start, 0u);
    ASSERT_EQ(group->stripes[0].data_chunk_count, 4u);

    // Second stripe: partial (1 data chunk, 3 zero-padded)
    ASSERT_EQ(group->stripes[1].data_chunk_start, 4u);
    ASSERT_EQ(group->stripes[1].data_chunk_count, 1u);

    // Parity still exists for partial stripe
    for (uint8_t p = 0; p < group->m; p++)
    {
        ASSERT(ChunkStore_Has(group->stripes[1].parity_hashes[p]));
    }

    for (int i = 0; i < 5; i++) free(chunk_data[i]);
    ErasureCoding_DestroyGroup(group);
    Manifest_Destroy(m);
    PASS();
}

TEST test_ec_destroy_group(void)
{
    // Create and immediately destroy — verify no crash
    EC_GROUP *group = (EC_GROUP *)calloc(1, sizeof(EC_GROUP));
    ASSERT(group != NULL);
    group->k = 4;
    group->m = 2;
    group->stripe_count = 0;
    group->stripes = NULL;
    ErasureCoding_DestroyGroup(group);

    // Also test NULL
    ErasureCoding_DestroyGroup(NULL);
    PASS();
}

TEST test_ec_deserialize_invalid(void)
{
    // Too short
    uint8_t tiny[3] = {4, 2, 0};
    ASSERT(ErasureCoding_DeserializeGroup(tiny, sizeof(tiny)) == NULL);

    // NULL data
    ASSERT(ErasureCoding_DeserializeGroup(NULL, 100) == NULL);

    // k=0
    uint8_t bad_k[6] = {0, 2, 0, 0, 0, 0};
    ASSERT(ErasureCoding_DeserializeGroup(bad_k, sizeof(bad_k)) == NULL);

    PASS();
}

// ============================================================================
// Metadata save/load tests
// ============================================================================

TEST test_ec_save_load_metadata(void)
{
    // Create a manifest with 8 chunks, encode with RS(4,2)
    uint8_t *chunk_data[8];
    FILE_MANIFEST *m = create_test_manifest(8, chunk_data);
    ASSERT(m != NULL);
    ASSERT_EQ(m->chunk_count, 8u);

    Manifest_ComputeHash(m);

    EC_GROUP *group = ErasureCoding_Encode(m, 4, 2);
    ASSERT(group != NULL);
    ASSERT_EQ(group->stripe_count, 2u);

    // Build path in test home
    char ec_path[600];
#ifdef _WIN32
    snprintf(ec_path, sizeof(ec_path), "%s\\test_meta.ec", g_test_home);
#else
    snprintf(ec_path, sizeof(ec_path), "%s/test_meta.ec", g_test_home);
#endif

    // Save
    ASSERT(ErasureCoding_SaveMetadata(ec_path, group, m));

    // Load
    FILE_MANIFEST *loaded_manifest = NULL;
    EC_GROUP *loaded_group = ErasureCoding_LoadMetadata(ec_path, &loaded_manifest);
    ASSERT(loaded_group != NULL);
    ASSERT(loaded_manifest != NULL);

    // Verify EC_GROUP fields
    ASSERT_EQ(loaded_group->k, group->k);
    ASSERT_EQ(loaded_group->m, group->m);
    ASSERT_EQ(loaded_group->stripe_count, group->stripe_count);

    for (uint32_t s = 0; s < group->stripe_count; s++)
    {
        ASSERT_EQ(loaded_group->stripes[s].data_chunk_start,
                   group->stripes[s].data_chunk_start);
        ASSERT_EQ(loaded_group->stripes[s].data_chunk_count,
                   group->stripes[s].data_chunk_count);
        for (uint8_t p = 0; p < group->m; p++)
        {
            ASSERT_MEM_EQ(loaded_group->stripes[s].parity_hashes[p],
                           group->stripes[s].parity_hashes[p], WH_HASH_SIZE);
            ASSERT_EQ(loaded_group->stripes[s].parity_sizes[p],
                       group->stripes[s].parity_sizes[p]);
        }
    }

    // Verify manifest fields
    ASSERT_EQ(loaded_manifest->chunk_count, m->chunk_count);
    ASSERT_EQ(loaded_manifest->file_size, m->file_size);
    ASSERT_MEM_EQ(loaded_manifest->manifest_hash, m->manifest_hash, WH_HASH_SIZE);

    for (uint32_t i = 0; i < m->chunk_count; i++)
    {
        ASSERT_MEM_EQ(loaded_manifest->chunks[i].hash, m->chunks[i].hash, WH_HASH_SIZE);
        ASSERT_EQ(loaded_manifest->chunks[i].chunk_size, m->chunks[i].chunk_size);
    }

    for (int i = 0; i < 8; i++) free(chunk_data[i]);
    ErasureCoding_DestroyGroup(group);
    ErasureCoding_DestroyGroup(loaded_group);
    Manifest_Destroy(m);
    Manifest_Destroy(loaded_manifest);
    PASS();
}

TEST test_ec_load_metadata_bad_file(void)
{
    FILE_MANIFEST *out_manifest = NULL;

    // Non-existent path → returns NULL
    EC_GROUP *g = ErasureCoding_LoadMetadata("nonexistent_file_12345.ec", &out_manifest);
    ASSERT(g == NULL);
    ASSERT(out_manifest == NULL);

    // Truncated file (too short to contain valid metadata)
    char trunc_path[600];
#ifdef _WIN32
    snprintf(trunc_path, sizeof(trunc_path), "%s\\truncated.ec", g_test_home);
#else
    snprintf(trunc_path, sizeof(trunc_path), "%s/truncated.ec", g_test_home);
#endif
    FILE *fp = fopen(trunc_path, "wb");
    ASSERT(fp != NULL);
    uint8_t garbage[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    fwrite(garbage, 1, sizeof(garbage), fp);
    fclose(fp);

    g = ErasureCoding_LoadMetadata(trunc_path, &out_manifest);
    ASSERT(g == NULL);
    ASSERT(out_manifest == NULL);

    // NULL path
    g = ErasureCoding_LoadMetadata(NULL, &out_manifest);
    ASSERT(g == NULL);

    PASS();
}

// ============================================================================
// Suite
// ============================================================================

SUITE(erasure_suite)
{
    RUN_TEST(test_ec_encode_basic);
    RUN_TEST(test_ec_reconstruct_chunk);
    RUN_TEST(test_ec_serialize_deserialize);
    RUN_TEST(test_ec_partial_last_stripe);
    RUN_TEST(test_ec_destroy_group);
    RUN_TEST(test_ec_deserialize_invalid);
}

SUITE(metadata_suite)
{
    RUN_TEST(test_ec_save_load_metadata);
    RUN_TEST(test_ec_load_metadata_bad_file);
}

int main(void)
{
    setup_test_home();
    atexit(cleanup_test_home);

    GREATEST_MAIN_BEGIN();
    RUN_SUITE(erasure_suite);
    RUN_SUITE(metadata_suite);
    GREATEST_MAIN_END();
}
