//
// test_transfer_state.c
// Unit tests for transfer_state.c — save/load bitfield persistence.
//

#include "../transfer_state.h"
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
// Test environment — redirect state files to a temporary directory
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
    snprintf(g_test_home, sizeof(g_test_home), "%swh_test_ts_%lu",
             temp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(g_test_home, NULL);

    snprintf(g_env_buf, sizeof(g_env_buf), "USERPROFILE=%s", g_test_home);
    _putenv(g_env_buf);
#else
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/wh_test_ts_%d", (int)getpid());
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
static void make_hash(uint8_t hash[32], uint8_t value)
{
    memset(hash, value, 32);
}

// ============================================================================
// save_load_suite — round-trip persistence tests
// ============================================================================

TEST test_save_load_roundtrip(void)
{
    uint8_t hash[32];
    make_hash(hash, 0x01);

    BOOLEAN chunks[10] = {TRUE, FALSE, TRUE, TRUE, FALSE,
                          TRUE, FALSE, FALSE, TRUE, TRUE};

    ASSERT(TransferState_Save(hash, chunks, 10));

    BOOLEAN *loaded = NULL;
    uint32_t count = 0;
    ASSERT(TransferState_Load(hash, &loaded, &count));
    ASSERT_EQ(count, 10u);
    for (uint32_t i = 0; i < 10; i++)
    {
        ASSERT_EQ(loaded[i], chunks[i]);
    }
    free(loaded);
    TransferState_Delete(hash);
    PASS();
}

TEST test_single_chunk(void)
{
    uint8_t hash[32];
    make_hash(hash, 0x02);

    BOOLEAN chunks[1] = {TRUE};
    ASSERT(TransferState_Save(hash, chunks, 1));

    BOOLEAN *loaded = NULL;
    uint32_t count = 0;
    ASSERT(TransferState_Load(hash, &loaded, &count));
    ASSERT_EQ(count, 1u);
    ASSERT_EQ(loaded[0], TRUE);
    free(loaded);
    TransferState_Delete(hash);
    PASS();
}

TEST test_no_chunks_received(void)
{
    uint8_t hash[32];
    make_hash(hash, 0x03);

    BOOLEAN chunks[5] = {FALSE, FALSE, FALSE, FALSE, FALSE};
    ASSERT(TransferState_Save(hash, chunks, 5));

    BOOLEAN *loaded = NULL;
    uint32_t count = 0;
    ASSERT(TransferState_Load(hash, &loaded, &count));
    ASSERT_EQ(count, 5u);
    for (uint32_t i = 0; i < 5; i++)
    {
        ASSERT_EQ(loaded[i], FALSE);
    }
    free(loaded);
    TransferState_Delete(hash);
    PASS();
}

TEST test_all_chunks_received(void)
{
    uint8_t hash[32];
    make_hash(hash, 0x04);

    BOOLEAN chunks[5] = {TRUE, TRUE, TRUE, TRUE, TRUE};
    ASSERT(TransferState_Save(hash, chunks, 5));

    BOOLEAN *loaded = NULL;
    uint32_t count = 0;
    ASSERT(TransferState_Load(hash, &loaded, &count));
    ASSERT_EQ(count, 5u);
    for (uint32_t i = 0; i < 5; i++)
    {
        ASSERT_EQ(loaded[i], TRUE);
    }
    free(loaded);
    TransferState_Delete(hash);
    PASS();
}

TEST test_boundary_8(void)
{
    uint8_t hash[32];
    make_hash(hash, 0x05);

    // Exactly 8 chunks = 1 bitmask byte
    BOOLEAN chunks[8] = {TRUE, FALSE, TRUE, FALSE, TRUE, FALSE, TRUE, FALSE};
    ASSERT(TransferState_Save(hash, chunks, 8));

    BOOLEAN *loaded = NULL;
    uint32_t count = 0;
    ASSERT(TransferState_Load(hash, &loaded, &count));
    ASSERT_EQ(count, 8u);
    for (uint32_t i = 0; i < 8; i++)
    {
        ASSERT_EQ(loaded[i], chunks[i]);
    }
    free(loaded);
    TransferState_Delete(hash);
    PASS();
}

TEST test_boundary_9(void)
{
    uint8_t hash[32];
    make_hash(hash, 0x06);

    // 9 chunks = 2 bitmask bytes, bit 8 is in byte[1]
    BOOLEAN chunks[9] = {TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE};
    ASSERT(TransferState_Save(hash, chunks, 9));

    BOOLEAN *loaded = NULL;
    uint32_t count = 0;
    ASSERT(TransferState_Load(hash, &loaded, &count));
    ASSERT_EQ(count, 9u);
    for (uint32_t i = 0; i < 9; i++)
    {
        ASSERT_EQ(loaded[i], TRUE);
    }
    free(loaded);
    TransferState_Delete(hash);
    PASS();
}

TEST test_large_count(void)
{
    uint8_t hash[32];
    make_hash(hash, 0x07);

    uint32_t n = 1000;
    BOOLEAN *chunks = (BOOLEAN *)calloc(n, sizeof(BOOLEAN));
    ASSERT(chunks != NULL);

    // Scattered pattern: every 3rd chunk received
    for (uint32_t i = 0; i < n; i++)
    {
        chunks[i] = (i % 3 == 0) ? TRUE : FALSE;
    }

    ASSERT(TransferState_Save(hash, chunks, n));

    BOOLEAN *loaded = NULL;
    uint32_t count = 0;
    ASSERT(TransferState_Load(hash, &loaded, &count));
    ASSERT_EQ(count, n);
    for (uint32_t i = 0; i < n; i++)
    {
        ASSERT_EQ(loaded[i], chunks[i]);
    }

    free(loaded);
    free(chunks);
    TransferState_Delete(hash);
    PASS();
}

// ============================================================================
// error_suite — edge cases and error handling
// ============================================================================

TEST test_load_nonexistent(void)
{
    uint8_t hash[32];
    make_hash(hash, 0xFE);

    BOOLEAN *loaded = NULL;
    uint32_t count = 0;
    ASSERT_FALSE(TransferState_Load(hash, &loaded, &count));
    PASS();
}

TEST test_save_zero_chunks(void)
{
    uint8_t hash[32];
    make_hash(hash, 0xFD);

    BOOLEAN chunks[1] = {TRUE};
    ASSERT_FALSE(TransferState_Save(hash, chunks, 0));
    PASS();
}

TEST test_save_null_array(void)
{
    uint8_t hash[32];
    make_hash(hash, 0xFC);

    ASSERT_FALSE(TransferState_Save(hash, NULL, 10));
    PASS();
}

TEST test_delete(void)
{
    uint8_t hash[32];
    make_hash(hash, 0xFB);

    BOOLEAN chunks[3] = {TRUE, FALSE, TRUE};
    ASSERT(TransferState_Save(hash, chunks, 3));

    // Verify it was saved
    BOOLEAN *loaded = NULL;
    uint32_t count = 0;
    ASSERT(TransferState_Load(hash, &loaded, &count));
    free(loaded);

    // Delete and verify it's gone
    TransferState_Delete(hash);

    loaded = NULL;
    count = 0;
    ASSERT_FALSE(TransferState_Load(hash, &loaded, &count));
    PASS();
}

// ============================================================================
// Suites
// ============================================================================

SUITE(save_load_suite)
{
    RUN_TEST(test_save_load_roundtrip);
    RUN_TEST(test_single_chunk);
    RUN_TEST(test_no_chunks_received);
    RUN_TEST(test_all_chunks_received);
    RUN_TEST(test_boundary_8);
    RUN_TEST(test_boundary_9);
    RUN_TEST(test_large_count);
}

SUITE(error_suite)
{
    RUN_TEST(test_load_nonexistent);
    RUN_TEST(test_save_zero_chunks);
    RUN_TEST(test_save_null_array);
    RUN_TEST(test_delete);
}

int main(void)
{
    setup_test_home();
    atexit(cleanup_test_home);

    GREATEST_MAIN_BEGIN();
    RUN_SUITE(save_load_suite);
    RUN_SUITE(error_suite);
    GREATEST_MAIN_END();
}
