//
// test_reed_solomon.c
// Unit tests for Reed-Solomon GF(2^8) erasure coding codec.
//

#include "greatest.h"
#include <rs.h>
#include <string.h>
#include <stdlib.h>

GREATEST_MAIN_DEFS();

// ============================================================================
// Helpers
// ============================================================================

// Fill a shard with a repeating pattern based on index
static void fill_shard(uint8_t *shard, size_t size, uint8_t seed)
{
    for (size_t i = 0; i < size; i++)
    {
        shard[i] = (uint8_t)(seed + (i % 251));  // 251 is prime, nice pattern
    }
}

// ============================================================================
// Basic codec lifecycle
// ============================================================================

TEST test_rs_create(void)
{
    RS_CODEC *codec = RS_Create(4, 2);
    ASSERT(codec != NULL);
    ASSERT_EQ(codec->k, 4);
    ASSERT_EQ(codec->m, 2);
    ASSERT_EQ(codec->n, 6);
    ASSERT(codec->encode_matrix != NULL);
    RS_Destroy(codec);
    PASS();
}

TEST test_rs_create_invalid(void)
{
    ASSERT(RS_Create(0, 2) == NULL);
    ASSERT(RS_Create(4, 0) == NULL);
    PASS();
}

// ============================================================================
// Encode / Decode
// ============================================================================

TEST test_rs_encode_decode_no_loss(void)
{
    RS_CODEC *codec = RS_Create(4, 2);
    ASSERT(codec != NULL);

    size_t shard_size = 64;
    uint8_t *data[4];
    uint8_t *parity[2];
    uint8_t *originals[4];

    for (int i = 0; i < 4; i++)
    {
        data[i] = (uint8_t *)malloc(shard_size);
        originals[i] = (uint8_t *)malloc(shard_size);
        ASSERT(data[i] != NULL);
        fill_shard(data[i], shard_size, (uint8_t)(i * 37));
        memcpy(originals[i], data[i], shard_size);
    }
    for (int i = 0; i < 2; i++)
    {
        parity[i] = (uint8_t *)malloc(shard_size);
        ASSERT(parity[i] != NULL);
    }

    ASSERT(RS_Encode(codec, (const uint8_t *const *)data, parity, shard_size));

    // Verify parity shards are non-zero (for non-trivial data)
    int nonzero = 0;
    for (size_t i = 0; i < shard_size; i++)
    {
        if (parity[0][i] != 0) nonzero++;
    }
    ASSERT(nonzero > 0);

    // Assemble all shards
    uint8_t *shards[6];
    uint8_t present[6] = {1, 1, 1, 1, 1, 1};
    for (int i = 0; i < 4; i++) shards[i] = data[i];
    for (int i = 0; i < 2; i++) shards[4 + i] = parity[i];

    ASSERT(RS_Decode(codec, shards, present, shard_size));

    // Data should be unchanged
    for (int i = 0; i < 4; i++)
    {
        ASSERT_MEM_EQ(shards[i], originals[i], shard_size);
    }

    for (int i = 0; i < 4; i++) { free(data[i]); free(originals[i]); }
    for (int i = 0; i < 2; i++) free(parity[i]);
    RS_Destroy(codec);
    PASS();
}

TEST test_rs_decode_one_data_missing(void)
{
    RS_CODEC *codec = RS_Create(4, 2);
    size_t shard_size = 128;

    uint8_t *data[4], *parity[2], *original0;
    for (int i = 0; i < 4; i++)
    {
        data[i] = (uint8_t *)malloc(shard_size);
        fill_shard(data[i], shard_size, (uint8_t)(i * 17 + 5));
    }
    original0 = (uint8_t *)malloc(shard_size);
    memcpy(original0, data[0], shard_size);

    for (int i = 0; i < 2; i++)
    {
        parity[i] = (uint8_t *)malloc(shard_size);
    }

    ASSERT(RS_Encode(codec, (const uint8_t *const *)data, parity, shard_size));

    // Simulate loss of data shard 0
    uint8_t *shards[6];
    uint8_t present[6] = {0, 1, 1, 1, 1, 1};
    memset(data[0], 0, shard_size);  // Clear data
    for (int i = 0; i < 4; i++) shards[i] = data[i];
    for (int i = 0; i < 2; i++) shards[4 + i] = parity[i];

    ASSERT(RS_Decode(codec, shards, present, shard_size));
    ASSERT_MEM_EQ(shards[0], original0, shard_size);

    for (int i = 0; i < 4; i++) free(data[i]);
    for (int i = 0; i < 2; i++) free(parity[i]);
    free(original0);
    RS_Destroy(codec);
    PASS();
}

TEST test_rs_decode_two_data_missing(void)
{
    RS_CODEC *codec = RS_Create(4, 2);
    size_t shard_size = 128;

    uint8_t *data[4], *parity[2];
    uint8_t *orig1, *orig3;
    for (int i = 0; i < 4; i++)
    {
        data[i] = (uint8_t *)malloc(shard_size);
        fill_shard(data[i], shard_size, (uint8_t)(i * 43 + 7));
    }
    orig1 = (uint8_t *)malloc(shard_size);
    orig3 = (uint8_t *)malloc(shard_size);
    memcpy(orig1, data[1], shard_size);
    memcpy(orig3, data[3], shard_size);

    for (int i = 0; i < 2; i++)
    {
        parity[i] = (uint8_t *)malloc(shard_size);
    }

    ASSERT(RS_Encode(codec, (const uint8_t *const *)data, parity, shard_size));

    // Lose shards 1 and 3
    uint8_t *shards[6];
    uint8_t present[6] = {1, 0, 1, 0, 1, 1};
    memset(data[1], 0, shard_size);
    memset(data[3], 0, shard_size);
    for (int i = 0; i < 4; i++) shards[i] = data[i];
    for (int i = 0; i < 2; i++) shards[4 + i] = parity[i];

    ASSERT(RS_Decode(codec, shards, present, shard_size));
    ASSERT_MEM_EQ(shards[1], orig1, shard_size);
    ASSERT_MEM_EQ(shards[3], orig3, shard_size);

    for (int i = 0; i < 4; i++) free(data[i]);
    for (int i = 0; i < 2; i++) free(parity[i]);
    free(orig1);
    free(orig3);
    RS_Destroy(codec);
    PASS();
}

TEST test_rs_decode_one_parity_missing(void)
{
    RS_CODEC *codec = RS_Create(4, 2);
    size_t shard_size = 64;

    uint8_t *data[4], *parity[2];
    uint8_t *orig_parity0;
    for (int i = 0; i < 4; i++)
    {
        data[i] = (uint8_t *)malloc(shard_size);
        fill_shard(data[i], shard_size, (uint8_t)(i * 11));
    }
    for (int i = 0; i < 2; i++)
    {
        parity[i] = (uint8_t *)malloc(shard_size);
    }

    ASSERT(RS_Encode(codec, (const uint8_t *const *)data, parity, shard_size));

    orig_parity0 = (uint8_t *)malloc(shard_size);
    memcpy(orig_parity0, parity[0], shard_size);

    // Lose parity shard 0
    uint8_t *shards[6];
    uint8_t present[6] = {1, 1, 1, 1, 0, 1};
    memset(parity[0], 0, shard_size);
    for (int i = 0; i < 4; i++) shards[i] = data[i];
    for (int i = 0; i < 2; i++) shards[4 + i] = parity[i];

    ASSERT(RS_Decode(codec, shards, present, shard_size));
    ASSERT_MEM_EQ(shards[4], orig_parity0, shard_size);

    for (int i = 0; i < 4; i++) free(data[i]);
    for (int i = 0; i < 2; i++) free(parity[i]);
    free(orig_parity0);
    RS_Destroy(codec);
    PASS();
}

TEST test_rs_decode_mixed_missing(void)
{
    RS_CODEC *codec = RS_Create(4, 2);
    size_t shard_size = 64;

    uint8_t *data[4], *parity[2];
    uint8_t *orig_data2, *orig_parity1;
    for (int i = 0; i < 4; i++)
    {
        data[i] = (uint8_t *)malloc(shard_size);
        fill_shard(data[i], shard_size, (uint8_t)(i * 29 + 3));
    }
    for (int i = 0; i < 2; i++)
    {
        parity[i] = (uint8_t *)malloc(shard_size);
    }

    ASSERT(RS_Encode(codec, (const uint8_t *const *)data, parity, shard_size));

    orig_data2 = (uint8_t *)malloc(shard_size);
    orig_parity1 = (uint8_t *)malloc(shard_size);
    memcpy(orig_data2, data[2], shard_size);
    memcpy(orig_parity1, parity[1], shard_size);

    // Lose data shard 2 and parity shard 1
    uint8_t *shards[6];
    uint8_t present[6] = {1, 1, 0, 1, 1, 0};
    memset(data[2], 0, shard_size);
    memset(parity[1], 0, shard_size);
    for (int i = 0; i < 4; i++) shards[i] = data[i];
    for (int i = 0; i < 2; i++) shards[4 + i] = parity[i];

    ASSERT(RS_Decode(codec, shards, present, shard_size));
    ASSERT_MEM_EQ(shards[2], orig_data2, shard_size);
    ASSERT_MEM_EQ(shards[5], orig_parity1, shard_size);

    for (int i = 0; i < 4; i++) free(data[i]);
    for (int i = 0; i < 2; i++) free(parity[i]);
    free(orig_data2);
    free(orig_parity1);
    RS_Destroy(codec);
    PASS();
}

TEST test_rs_decode_too_many_missing(void)
{
    RS_CODEC *codec = RS_Create(4, 2);
    size_t shard_size = 64;

    uint8_t *data[4], *parity[2];
    for (int i = 0; i < 4; i++)
    {
        data[i] = (uint8_t *)malloc(shard_size);
        fill_shard(data[i], shard_size, (uint8_t)i);
    }
    for (int i = 0; i < 2; i++)
    {
        parity[i] = (uint8_t *)malloc(shard_size);
    }

    ASSERT(RS_Encode(codec, (const uint8_t *const *)data, parity, shard_size));

    // Lose 3 shards (> m=2) — should fail
    uint8_t *shards[6];
    uint8_t present[6] = {0, 0, 0, 1, 1, 1};
    memset(data[0], 0, shard_size);
    memset(data[1], 0, shard_size);
    memset(data[2], 0, shard_size);
    for (int i = 0; i < 4; i++) shards[i] = data[i];
    for (int i = 0; i < 2; i++) shards[4 + i] = parity[i];

    ASSERT_FALSE(RS_Decode(codec, shards, present, shard_size));

    for (int i = 0; i < 4; i++) free(data[i]);
    for (int i = 0; i < 2; i++) free(parity[i]);
    RS_Destroy(codec);
    PASS();
}

TEST test_rs_partial_stripe(void)
{
    // Create RS(4,2) but only use 2 real data shards, zero-pad others
    RS_CODEC *codec = RS_Create(4, 2);
    size_t shard_size = 64;

    uint8_t *data[4], *parity[2];
    uint8_t *orig0, *orig1;

    for (int i = 0; i < 4; i++)
    {
        data[i] = (uint8_t *)calloc(1, shard_size);
    }
    fill_shard(data[0], shard_size, 0xAA);
    fill_shard(data[1], shard_size, 0xBB);
    // data[2] and data[3] remain zero (padding)

    orig0 = (uint8_t *)malloc(shard_size);
    orig1 = (uint8_t *)malloc(shard_size);
    memcpy(orig0, data[0], shard_size);
    memcpy(orig1, data[1], shard_size);

    for (int i = 0; i < 2; i++)
    {
        parity[i] = (uint8_t *)malloc(shard_size);
    }

    ASSERT(RS_Encode(codec, (const uint8_t *const *)data, parity, shard_size));

    // Lose data shard 0
    uint8_t *shards[6];
    uint8_t present[6] = {0, 1, 1, 1, 1, 1};
    memset(data[0], 0, shard_size);
    for (int i = 0; i < 4; i++) shards[i] = data[i];
    for (int i = 0; i < 2; i++) shards[4 + i] = parity[i];

    ASSERT(RS_Decode(codec, shards, present, shard_size));
    ASSERT_MEM_EQ(shards[0], orig0, shard_size);

    for (int i = 0; i < 4; i++) free(data[i]);
    for (int i = 0; i < 2; i++) free(parity[i]);
    free(orig0);
    free(orig1);
    RS_Destroy(codec);
    PASS();
}

TEST test_rs_large_shard(void)
{
    // 256KB shard size — matches WH_CHUNK_SIZE
    RS_CODEC *codec = RS_Create(4, 2);
    size_t shard_size = 256 * 1024;

    uint8_t *data[4], *parity[2];
    uint8_t *orig0;

    for (int i = 0; i < 4; i++)
    {
        data[i] = (uint8_t *)malloc(shard_size);
        ASSERT(data[i] != NULL);
        fill_shard(data[i], shard_size, (uint8_t)(i * 61));
    }
    orig0 = (uint8_t *)malloc(shard_size);
    memcpy(orig0, data[0], shard_size);

    for (int i = 0; i < 2; i++)
    {
        parity[i] = (uint8_t *)malloc(shard_size);
        ASSERT(parity[i] != NULL);
    }

    ASSERT(RS_Encode(codec, (const uint8_t *const *)data, parity, shard_size));

    // Lose data shard 0, reconstruct
    uint8_t *shards[6];
    uint8_t present[6] = {0, 1, 1, 1, 1, 1};
    memset(data[0], 0, shard_size);
    for (int i = 0; i < 4; i++) shards[i] = data[i];
    for (int i = 0; i < 2; i++) shards[4 + i] = parity[i];

    ASSERT(RS_Decode(codec, shards, present, shard_size));
    ASSERT_MEM_EQ(shards[0], orig0, shard_size);

    for (int i = 0; i < 4; i++) free(data[i]);
    for (int i = 0; i < 2; i++) free(parity[i]);
    free(orig0);
    RS_Destroy(codec);
    PASS();
}

TEST test_gf_multiply(void)
{
    // Test basic GF(2^8) properties via encode/decode with known small data.
    // Create RS(2,1) — simplest non-trivial case
    RS_CODEC *codec = RS_Create(2, 1);
    ASSERT(codec != NULL);
    ASSERT_EQ(codec->k, 2);
    ASSERT_EQ(codec->m, 1);
    ASSERT_EQ(codec->n, 3);

    size_t shard_size = 4;
    uint8_t d0[4] = {0, 1, 2, 255};
    uint8_t d1[4] = {255, 2, 1, 0};
    uint8_t p0[4] = {0};

    const uint8_t *data_ptrs[2] = {d0, d1};
    uint8_t *parity_ptrs[1] = {p0};

    ASSERT(RS_Encode(codec, data_ptrs, parity_ptrs, shard_size));

    // Parity should be non-trivial (not all zeros for non-zero data)
    int has_nonzero = 0;
    for (int i = 0; i < 4; i++)
    {
        if (p0[i] != 0) has_nonzero = 1;
    }
    ASSERT(has_nonzero);

    // Verify that encoding with identity produces data as-is
    // (top k rows of systematic matrix are identity)
    // Lose data shard 0, recover it
    uint8_t orig_d0[4];
    memcpy(orig_d0, d0, 4);
    memset(d0, 0, 4);

    uint8_t *shards[3] = {d0, d1, p0};
    uint8_t present[3] = {0, 1, 1};

    ASSERT(RS_Decode(codec, shards, present, shard_size));
    ASSERT_MEM_EQ(d0, orig_d0, 4);

    RS_Destroy(codec);
    PASS();
}

// ============================================================================
// Suite
// ============================================================================

SUITE(reed_solomon_suite)
{
    RUN_TEST(test_rs_create);
    RUN_TEST(test_rs_create_invalid);
    RUN_TEST(test_rs_encode_decode_no_loss);
    RUN_TEST(test_rs_decode_one_data_missing);
    RUN_TEST(test_rs_decode_two_data_missing);
    RUN_TEST(test_rs_decode_one_parity_missing);
    RUN_TEST(test_rs_decode_mixed_missing);
    RUN_TEST(test_rs_decode_too_many_missing);
    RUN_TEST(test_rs_partial_stripe);
    RUN_TEST(test_rs_large_shard);
    RUN_TEST(test_gf_multiply);
}

int main(void)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(reed_solomon_suite);
    GREATEST_MAIN_END();
}
