//
// test_file_registry.c
// Unit tests for file_registry.c — save/load/update/list/find by prefix.
//

#include "../file_registry.h"
#include "../manifest.h"
#include "greatest.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
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
    snprintf(g_test_home, sizeof(g_test_home), "%swh_test_filereg_%lu",
             temp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(g_test_home, NULL);

    snprintf(g_env_buf, sizeof(g_env_buf), "USERPROFILE=%s", g_test_home);
    _putenv(g_env_buf);
#else
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/wh_test_filereg_%d", (int)getpid());
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

// Helper: create a simple test manifest with known hash
static FILE_MANIFEST *make_test_manifest(uint64_t file_size, const char *filename)
{
    FILE_MANIFEST *m = Manifest_Create(filename, file_size);
    if (!m) return NULL;

    // Fill chunks with deterministic hashes
    for (uint32_t i = 0; i < m->chunk_count; i++)
    {
        uint8_t hash[WH_HASH_SIZE];
        memset(hash, 0, WH_HASH_SIZE);
        hash[0] = (uint8_t)(i & 0xFF);
        hash[1] = (uint8_t)((i >> 8) & 0xFF);
        uint32_t csize = WH_CHUNK_SIZE;
        if (i == m->chunk_count - 1)
        {
            uint64_t remaining = file_size - (uint64_t)i * WH_CHUNK_SIZE;
            csize = (uint32_t)remaining;
        }
        Manifest_AddChunk(m, i, hash, csize);
    }
    Manifest_ComputeHash(m);
    return m;
}

// ============================================================================
// Tests
// ============================================================================

TEST test_init(void)
{
    ASSERT(FileRegistry_Init());
    PASS();
}

TEST test_save_and_load(void)
{
    FileRegistry_Init();

    FILE_MANIFEST *m = make_test_manifest(1024 * 1024, "testfile.bin");
    ASSERT(m != NULL);

    ASSERT(FileRegistry_Save(m, "testfile.bin", FILE_STATUS_STORING));

    // Load back
    FILE_REG_ENTRY entry;
    FILE_MANIFEST *loaded_manifest = NULL;
    ASSERT(FileRegistry_Load(m->manifest_hash, &entry, &loaded_manifest));

    ASSERT(strcmp(entry.filename, "testfile.bin") == 0);
    ASSERT_EQ(entry.file_size, 1024ULL * 1024);
    ASSERT_EQ(entry.status, FILE_STATUS_STORING);
    ASSERT_EQ(entry.replicated_count, 0u);
    ASSERT(entry.store_time > 0);

    // Verify manifest was preserved
    ASSERT(loaded_manifest != NULL);
    ASSERT_EQ(loaded_manifest->chunk_count, m->chunk_count);
    ASSERT_EQ(loaded_manifest->file_size, m->file_size);
    ASSERT_MEM_EQ(loaded_manifest->manifest_hash, m->manifest_hash, WH_HASH_SIZE);

    Manifest_Destroy(loaded_manifest);
    Manifest_Destroy(m);
    PASS();
}

TEST test_load_nonexistent(void)
{
    uint8_t fake_hash[WH_HASH_SIZE];
    memset(fake_hash, 0xFF, WH_HASH_SIZE);

    FILE_REG_ENTRY entry;
    ASSERT_FALSE(FileRegistry_Load(fake_hash, &entry, NULL));
    PASS();
}

TEST test_update_status(void)
{
    FileRegistry_Init();

    FILE_MANIFEST *m = make_test_manifest(512 * 1024, "update_test.bin");
    ASSERT(m != NULL);
    ASSERT(FileRegistry_Save(m, "update_test.bin", FILE_STATUS_STORING));

    // Update to REPLICATING with 1 chunk replicated
    ASSERT(FileRegistry_UpdateStatus(m->manifest_hash, FILE_STATUS_REPLICATING, 1));

    FILE_REG_ENTRY entry;
    ASSERT(FileRegistry_Load(m->manifest_hash, &entry, NULL));
    ASSERT_EQ(entry.status, FILE_STATUS_REPLICATING);
    ASSERT_EQ(entry.replicated_count, 1u);

    // Update to SAFE
    ASSERT(FileRegistry_UpdateStatus(m->manifest_hash, FILE_STATUS_SAFE, 2));

    ASSERT(FileRegistry_Load(m->manifest_hash, &entry, NULL));
    ASSERT_EQ(entry.status, FILE_STATUS_SAFE);
    ASSERT_EQ(entry.replicated_count, 2u);

    // Update to OFFLOADED
    ASSERT(FileRegistry_UpdateStatus(m->manifest_hash, FILE_STATUS_OFFLOADED, 2));

    ASSERT(FileRegistry_Load(m->manifest_hash, &entry, NULL));
    ASSERT_EQ(entry.status, FILE_STATUS_OFFLOADED);

    Manifest_Destroy(m);
    PASS();
}

TEST test_list_files(void)
{
    FileRegistry_Init();

    // Save 3 files
    FILE_MANIFEST *m1 = make_test_manifest(1024, "file_a.txt");
    FILE_MANIFEST *m2 = make_test_manifest(2048, "file_b.txt");
    FILE_MANIFEST *m3 = make_test_manifest(4096, "file_c.txt");

    ASSERT(m1 && m2 && m3);
    ASSERT(FileRegistry_Save(m1, "file_a.txt", FILE_STATUS_SAFE));
    ASSERT(FileRegistry_Save(m2, "file_b.txt", FILE_STATUS_REPLICATING));
    ASSERT(FileRegistry_Save(m3, "file_c.txt", FILE_STATUS_STORING));

    FILE_REG_ENTRY entries[10];
    uint32_t count = FileRegistry_List(entries, 10);

    // Should have at least the 3 we just saved (plus any from earlier tests)
    ASSERT(count >= 3);

    Manifest_Destroy(m1);
    Manifest_Destroy(m2);
    Manifest_Destroy(m3);
    PASS();
}

TEST test_find_by_prefix(void)
{
    FileRegistry_Init();

    FILE_MANIFEST *m = make_test_manifest(8192, "prefix_test.bin");
    ASSERT(m != NULL);
    ASSERT(FileRegistry_Save(m, "prefix_test.bin", FILE_STATUS_SAFE));

    // Build hex prefix from manifest hash (first 8 chars = 4 bytes)
    char prefix[9];
    for (int i = 0; i < 4; i++)
        sprintf(prefix + i * 2, "%02x", m->manifest_hash[i]);
    prefix[8] = '\0';

    FILE_REG_ENTRY entry;
    ASSERT(FileRegistry_FindByPrefix(prefix, &entry, NULL));
    ASSERT(strcmp(entry.filename, "prefix_test.bin") == 0);
    ASSERT_MEM_EQ(entry.manifest_hash, m->manifest_hash, WH_HASH_SIZE);

    Manifest_Destroy(m);
    PASS();
}

TEST test_find_by_prefix_too_short(void)
{
    FILE_REG_ENTRY entry;
    ASSERT_FALSE(FileRegistry_FindByPrefix("abc", &entry, NULL));
    PASS();
}

TEST test_find_by_prefix_no_match(void)
{
    FILE_REG_ENTRY entry;
    ASSERT_FALSE(FileRegistry_FindByPrefix("deadbeefdeadbeef", &entry, NULL));
    PASS();
}

TEST test_save_load_with_manifest_roundtrip(void)
{
    FileRegistry_Init();

    FILE_MANIFEST *m = make_test_manifest(3 * WH_CHUNK_SIZE + 100, "roundtrip.dat");
    ASSERT(m != NULL);
    ASSERT(FileRegistry_Save(m, "roundtrip.dat", FILE_STATUS_REPLICATING));

    FILE_REG_ENTRY entry;
    FILE_MANIFEST *loaded = NULL;
    ASSERT(FileRegistry_Load(m->manifest_hash, &entry, &loaded));
    ASSERT(loaded != NULL);

    // Verify all chunks match
    ASSERT_EQ(loaded->chunk_count, m->chunk_count);
    for (uint32_t i = 0; i < m->chunk_count; i++)
    {
        ASSERT_MEM_EQ(loaded->chunks[i].hash, m->chunks[i].hash, WH_HASH_SIZE);
        ASSERT_EQ(loaded->chunks[i].chunk_size, m->chunks[i].chunk_size);
    }

    Manifest_Destroy(loaded);
    Manifest_Destroy(m);
    PASS();
}

// ============================================================================
// Suites
// ============================================================================

SUITE(registry_suite)
{
    RUN_TEST(test_init);
    RUN_TEST(test_save_and_load);
    RUN_TEST(test_load_nonexistent);
    RUN_TEST(test_update_status);
}

SUITE(listing_suite)
{
    RUN_TEST(test_list_files);
    RUN_TEST(test_find_by_prefix);
    RUN_TEST(test_find_by_prefix_too_short);
    RUN_TEST(test_find_by_prefix_no_match);
}

SUITE(roundtrip_suite)
{
    RUN_TEST(test_save_load_with_manifest_roundtrip);
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    setup_test_home();
    atexit(cleanup_test_home);

    GREATEST_MAIN_BEGIN();
    RUN_SUITE(registry_suite);
    RUN_SUITE(listing_suite);
    RUN_SUITE(roundtrip_suite);
    GREATEST_MAIN_END();
}
