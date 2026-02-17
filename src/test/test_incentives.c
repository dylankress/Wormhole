//
// test_incentives.c
// Unit tests for incentives.c — ledger tracking, ratio checks, save/load.
//

#include "../incentives.h"
#include "greatest.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

GREATEST_MAIN_DEFS();

// ============================================================================
// Test environment — redirect paths to a temporary directory
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
    snprintf(g_test_home, sizeof(g_test_home), "%swh_test_incent_%lu",
             temp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(g_test_home, NULL);

    snprintf(g_env_buf, sizeof(g_env_buf), "USERPROFILE=%s", g_test_home);
    _putenv(g_env_buf);
#else
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/wh_test_incent_%d", (int)getpid());
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

// Helper: build a path within the test home directory.
static void make_temp_path(char *out, size_t out_len, const char *filename)
{
#ifdef _WIN32
    snprintf(out, out_len, "%s\\%s", g_test_home, filename);
#else
    snprintf(out, out_len, "%s/%s", g_test_home, filename);
#endif
}

// Helper: fill a peer ID with a single value.
static void make_peer_id(uint8_t peer_id[32], uint8_t value)
{
    memset(peer_id, value, 32);
}

// ============================================================================
// Tests
// ============================================================================

TEST test_ledger_init(void)
{
    STORAGE_LEDGER ledger;
    Ledger_Init(&ledger);

    ASSERT_EQ(ledger.peer_count, 0u);
    ASSERT_EQ(ledger.total_stored_for_us, 0ULL);
    ASSERT_EQ(ledger.total_we_store, 0ULL);
    PASS();
}

TEST test_ledger_record_balanced(void)
{
    STORAGE_LEDGER ledger;
    Ledger_Init(&ledger);

    uint8_t peer[32];
    make_peer_id(peer, 0xAA);

    uint64_t amount = 1024 * 1024;  // 1 MB

    Ledger_RecordStoredForUs(&ledger, peer, amount);
    Ledger_RecordWeStored(&ledger, peer, amount);

    ASSERT_EQ(ledger.peer_count, 1u);
    ASSERT_EQ(ledger.total_stored_for_us, amount);
    ASSERT_EQ(ledger.total_we_store, amount);

    // Ratio should be ~1.0
    double ratio = Ledger_GetPeerRatio(&ledger, peer);
    ASSERT(fabs(ratio - 1.0) < 0.001);

    // Should accept more storage since ratio is balanced
    ASSERT(Ledger_ShouldAcceptStorage(&ledger, peer, amount));
    PASS();
}

TEST test_ledger_record_unbalanced_reject(void)
{
    STORAGE_LEDGER ledger;
    Ledger_Init(&ledger);

    uint8_t peer[32];
    make_peer_id(peer, 0xBB);

    // We store 10 MB for them, they store nothing for us
    uint64_t ten_mb = 10 * 1024 * 1024;
    Ledger_RecordWeStored(&ledger, peer, ten_mb);

    // They store 0 for us. We already store 10MB, which exceeds threshold.
    // Trying to add more should be rejected.
    ASSERT_FALSE(Ledger_ShouldAcceptStorage(&ledger, peer, 1024));
    PASS();
}

TEST test_ledger_new_peer_accepted(void)
{
    STORAGE_LEDGER ledger;
    Ledger_Init(&ledger);

    uint8_t peer[32];
    make_peer_id(peer, 0xCC);

    // Unknown peer's first small storage request should be accepted
    ASSERT(Ledger_ShouldAcceptStorage(&ledger, peer, 256 * 1024));
    PASS();
}

TEST test_ledger_new_peer_rejected_over_threshold(void)
{
    STORAGE_LEDGER ledger;
    Ledger_Init(&ledger);

    uint8_t peer[32];
    make_peer_id(peer, 0xCD);

    // New peer requesting more than threshold should be rejected
    uint64_t over_threshold = LEDGER_NEW_PEER_THRESHOLD + 1;
    ASSERT_FALSE(Ledger_ShouldAcceptStorage(&ledger, peer, over_threshold));
    PASS();
}

TEST test_ledger_ratio_calculation(void)
{
    STORAGE_LEDGER ledger;
    Ledger_Init(&ledger);

    uint8_t peer[32];
    make_peer_id(peer, 0xDD);

    // Peer stores 4 MB for us, we store 2 MB for them → ratio = 2.0
    Ledger_RecordStoredForUs(&ledger, peer, 4 * 1024 * 1024);
    Ledger_RecordWeStored(&ledger, peer, 2 * 1024 * 1024);

    double ratio = Ledger_GetPeerRatio(&ledger, peer);
    ASSERT(fabs(ratio - 2.0) < 0.001);

    // Unknown peer should have neutral ratio
    uint8_t unknown[32];
    make_peer_id(unknown, 0xEE);
    ASSERT(fabs(Ledger_GetPeerRatio(&ledger, unknown) - 1.0) < 0.001);
    PASS();
}

TEST test_ledger_ratio_no_storage_for_them(void)
{
    STORAGE_LEDGER ledger;
    Ledger_Init(&ledger);

    uint8_t peer[32];
    make_peer_id(peer, 0xDE);

    // They store 5 MB for us, we store 0 for them → ratio = 1.0 (neutral)
    Ledger_RecordStoredForUs(&ledger, peer, 5 * 1024 * 1024);

    double ratio = Ledger_GetPeerRatio(&ledger, peer);
    ASSERT(fabs(ratio - 1.0) < 0.001);
    PASS();
}

TEST test_ledger_save_load_roundtrip(void)
{
    STORAGE_LEDGER ledger;
    Ledger_Init(&ledger);

    uint8_t peer1[32], peer2[32];
    make_peer_id(peer1, 0x11);
    make_peer_id(peer2, 0x22);

    Ledger_RecordStoredForUs(&ledger, peer1, 1000);
    Ledger_RecordWeStored(&ledger, peer1, 2000);
    Ledger_RecordStoredForUs(&ledger, peer2, 3000);
    Ledger_RecordWeStored(&ledger, peer2, 4000);

    char path[512];
    make_temp_path(path, sizeof(path), "test_ledger.dat");

    ASSERT(Ledger_Save(&ledger, path));

    // Load into a fresh ledger
    STORAGE_LEDGER loaded;
    Ledger_Init(&loaded);
    ASSERT(Ledger_Load(&loaded, path));

    // Verify counts
    ASSERT_EQ(loaded.peer_count, 2u);
    ASSERT_EQ(loaded.total_stored_for_us, 4000ULL);
    ASSERT_EQ(loaded.total_we_store, 6000ULL);

    // Verify individual peer data
    ASSERT_MEM_EQ(loaded.peers[0].peer_id, peer1, 32);
    ASSERT_EQ(loaded.peers[0].bytes_stored_for_us, 1000ULL);
    ASSERT_EQ(loaded.peers[0].bytes_we_store_for_them, 2000ULL);

    ASSERT_MEM_EQ(loaded.peers[1].peer_id, peer2, 32);
    ASSERT_EQ(loaded.peers[1].bytes_stored_for_us, 3000ULL);
    ASSERT_EQ(loaded.peers[1].bytes_we_store_for_them, 4000ULL);
    PASS();
}

TEST test_ledger_load_bad_magic(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "bad_magic.dat");

    // Write a file with wrong magic
    FILE *fh = fopen(path, "wb");
    ASSERT(fh != NULL);
    uint8_t bad_header[12] = {0};
    fwrite(bad_header, 1, 12, fh);
    fclose(fh);

    STORAGE_LEDGER loaded;
    ASSERT_FALSE(Ledger_Load(&loaded, path));
    PASS();
}

TEST test_ledger_should_accept_threshold(void)
{
    STORAGE_LEDGER ledger;
    Ledger_Init(&ledger);

    uint8_t peer[32];
    make_peer_id(peer, 0xFF);

    // Peer stores 100 bytes for us, we store 0.
    // We should accept up to 200 bytes (2x their contribution).
    Ledger_RecordStoredForUs(&ledger, peer, 100);

    // Exactly at the boundary: 200 should be accepted (100 * 2 >= 200)
    ASSERT(Ledger_ShouldAcceptStorage(&ledger, peer, 200));

    // Just over: 201 should be rejected (100 * 2 < 201)
    ASSERT_FALSE(Ledger_ShouldAcceptStorage(&ledger, peer, 201));
    PASS();
}

TEST test_ledger_multiple_peers(void)
{
    STORAGE_LEDGER ledger;
    Ledger_Init(&ledger);

    // Create 5 peers with different balances
    for (uint8_t i = 0; i < 5; i++)
    {
        uint8_t peer[32];
        make_peer_id(peer, (uint8_t)(0x50 + i));

        uint64_t amount = (uint64_t)(i + 1) * 1000;
        Ledger_RecordStoredForUs(&ledger, peer, amount);
        Ledger_RecordWeStored(&ledger, peer, amount);
    }

    ASSERT_EQ(ledger.peer_count, 5u);

    // Verify each peer tracked independently
    for (uint8_t i = 0; i < 5; i++)
    {
        uint8_t peer[32];
        make_peer_id(peer, (uint8_t)(0x50 + i));

        double ratio = Ledger_GetPeerRatio(&ledger, peer);
        ASSERT(fabs(ratio - 1.0) < 0.001);
    }

    // Total should be sum: 1000+2000+3000+4000+5000 = 15000
    ASSERT_EQ(ledger.total_stored_for_us, 15000ULL);
    ASSERT_EQ(ledger.total_we_store, 15000ULL);
    PASS();
}

TEST test_ledger_cumulative_recording(void)
{
    STORAGE_LEDGER ledger;
    Ledger_Init(&ledger);

    uint8_t peer[32];
    make_peer_id(peer, 0x99);

    // Record multiple times — should accumulate
    Ledger_RecordStoredForUs(&ledger, peer, 100);
    Ledger_RecordStoredForUs(&ledger, peer, 200);
    Ledger_RecordWeStored(&ledger, peer, 150);

    ASSERT_EQ(ledger.peer_count, 1u);
    ASSERT_EQ(ledger.total_stored_for_us, 300ULL);
    ASSERT_EQ(ledger.total_we_store, 150ULL);
    PASS();
}

// ============================================================================
// Suites
// ============================================================================

SUITE(ledger_suite)
{
    RUN_TEST(test_ledger_init);
    RUN_TEST(test_ledger_record_balanced);
    RUN_TEST(test_ledger_record_unbalanced_reject);
    RUN_TEST(test_ledger_new_peer_accepted);
    RUN_TEST(test_ledger_new_peer_rejected_over_threshold);
    RUN_TEST(test_ledger_ratio_calculation);
    RUN_TEST(test_ledger_ratio_no_storage_for_them);
    RUN_TEST(test_ledger_should_accept_threshold);
    RUN_TEST(test_ledger_multiple_peers);
    RUN_TEST(test_ledger_cumulative_recording);
}

SUITE(persistence_suite)
{
    RUN_TEST(test_ledger_save_load_roundtrip);
    RUN_TEST(test_ledger_load_bad_magic);
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    setup_test_home();
    atexit(cleanup_test_home);

    GREATEST_MAIN_BEGIN();
    RUN_SUITE(ledger_suite);
    RUN_SUITE(persistence_suite);
    GREATEST_MAIN_END();
}
