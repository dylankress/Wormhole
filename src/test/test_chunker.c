//
// test_chunker.c
// Unit tests for chunker.c — file and directory chunking with Blake3 hashing.
//

#include "greatest.h"
#include "../chunker.h"
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
// Test environment — create temp files/directories
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
    snprintf(g_test_home, sizeof(g_test_home), "%swh_test_chk_%lu",
             temp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(g_test_home, NULL);

    snprintf(g_env_buf, sizeof(g_env_buf), "USERPROFILE=%s", g_test_home);
    _putenv(g_env_buf);
#else
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/wh_test_chk_%d", (int)getpid());
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
static void make_path(char *out, size_t out_len, const char *name)
{
#ifdef _WIN32
    snprintf(out, out_len, "%s\\%s", g_test_home, name);
#else
    snprintf(out, out_len, "%s/%s", g_test_home, name);
#endif
}

// Helper: create a file filled with a repeating byte pattern.
static BOOLEAN create_test_file(const char *path, size_t size, uint8_t fill)
{
    FILE *fh = fopen(path, "wb");
    if (!fh) return FALSE;

    uint8_t buf[4096];
    memset(buf, fill, sizeof(buf));

    size_t remaining = size;
    while (remaining > 0)
    {
        size_t to_write = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        if (fwrite(buf, 1, to_write, fh) != to_write)
        {
            fclose(fh);
            return FALSE;
        }
        remaining -= to_write;
    }

    fclose(fh);
    return TRUE;
}

// Helper: create a directory.
static BOOLEAN create_dir(const char *path)
{
#ifdef _WIN32
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return (mkdir(path, 0755) == 0 || errno == EEXIST);
#endif
}

// ============================================================================
// Tests
// ============================================================================

TEST test_build_manifest_small_file(void)
{
    char path[512];
    make_path(path, sizeof(path), "small.bin");

    // Create a file smaller than one chunk (1000 bytes)
    ASSERT(create_test_file(path, 1000, 0x42));

    FILE_MANIFEST *m = Chunker_BuildManifest(path);
    ASSERT(m != NULL);
    ASSERT_EQ(m->chunk_count, 1u);
    ASSERT_EQ(m->file_size, 1000ULL);
    ASSERT_EQ(m->version, WH_MANIFEST_VERSION);
    ASSERT_EQ(m->chunks[0].chunk_size, 1000u);

    // Verify hash is non-zero (was computed)
    uint8_t zero_hash[WH_HASH_SIZE];
    memset(zero_hash, 0, WH_HASH_SIZE);
    ASSERT(memcmp(m->chunks[0].hash, zero_hash, WH_HASH_SIZE) != 0);
    ASSERT(memcmp(m->manifest_hash, zero_hash, WH_HASH_SIZE) != 0);

    Manifest_Destroy(m);
    PASS();
}

TEST test_build_manifest_multi_chunk(void)
{
    char path[512];
    make_path(path, sizeof(path), "multi.bin");

    // 600KB = 256 + 256 + 88 = 3 chunks
    size_t file_size = 600 * 1024;
    ASSERT(create_test_file(path, file_size, 0xAA));

    FILE_MANIFEST *m = Chunker_BuildManifest(path);
    ASSERT(m != NULL);
    ASSERT_EQ(m->chunk_count, 3u);
    ASSERT_EQ(m->file_size, (uint64_t)file_size);
    ASSERT_EQ(m->chunks[0].chunk_size, (uint32_t)WH_CHUNK_SIZE);
    ASSERT_EQ(m->chunks[1].chunk_size, (uint32_t)WH_CHUNK_SIZE);
    ASSERT_EQ(m->chunks[2].chunk_size, (uint32_t)(file_size - 2 * WH_CHUNK_SIZE));

    Manifest_Destroy(m);
    PASS();
}

TEST test_build_manifest_deterministic(void)
{
    char path[512];
    make_path(path, sizeof(path), "determ.bin");

    ASSERT(create_test_file(path, 50000, 0xBB));

    FILE_MANIFEST *m1 = Chunker_BuildManifest(path);
    ASSERT(m1 != NULL);

    FILE_MANIFEST *m2 = Chunker_BuildManifest(path);
    ASSERT(m2 != NULL);

    // Manifest hashes should be identical
    ASSERT_MEM_EQ(m1->manifest_hash, m2->manifest_hash, WH_HASH_SIZE);

    // Chunk hashes should also match
    ASSERT_EQ(m1->chunk_count, m2->chunk_count);
    for (uint32_t i = 0; i < m1->chunk_count; i++)
    {
        ASSERT_MEM_EQ(m1->chunks[i].hash, m2->chunks[i].hash, WH_HASH_SIZE);
    }

    Manifest_Destroy(m1);
    Manifest_Destroy(m2);
    PASS();
}

TEST test_build_manifest_from_directory(void)
{
    // Create temp dir with 2 files
    char dir_path[512];
    make_path(dir_path, sizeof(dir_path), "testdir");
    ASSERT(create_dir(dir_path));

    char file1[512], file2[512];
#ifdef _WIN32
    snprintf(file1, sizeof(file1), "%s\\file_a.txt", dir_path);
    snprintf(file2, sizeof(file2), "%s\\file_b.txt", dir_path);
#else
    snprintf(file1, sizeof(file1), "%s/file_a.txt", dir_path);
    snprintf(file2, sizeof(file2), "%s/file_b.txt", dir_path);
#endif

    ASSERT(create_test_file(file1, 1000, 0x11));
    ASSERT(create_test_file(file2, 2000, 0x22));

    FILE_MANIFEST *m = Chunker_BuildManifestFromDirectory(dir_path);
    ASSERT(m != NULL);
    ASSERT_EQ(m->version, WH_MANIFEST_VERSION_2);
    ASSERT_EQ(m->file_count, 2u);
    ASSERT_EQ(m->file_size, 3000ULL);

    // Each file < 256KB = 1 chunk each, 2 total
    ASSERT_EQ(m->chunk_count, 2u);

    // Verify file entries have correct sizes
    uint64_t size_sum = 0;
    for (uint32_t i = 0; i < m->file_count; i++)
    {
        size_sum += m->files[i].file_size;
        ASSERT(m->files[i].relative_path != NULL);
        ASSERT(m->files[i].path_length > 0);
    }
    ASSERT_EQ(size_sum, m->file_size);

    Manifest_Destroy(m);
    PASS();
}

TEST test_build_manifest_nested_dir(void)
{
    // Create temp dir with subdirectory
    char dir_path[512];
    make_path(dir_path, sizeof(dir_path), "nested");
    ASSERT(create_dir(dir_path));

    char sub_path[512];
#ifdef _WIN32
    snprintf(sub_path, sizeof(sub_path), "%s\\subdir", dir_path);
#else
    snprintf(sub_path, sizeof(sub_path), "%s/subdir", dir_path);
#endif
    ASSERT(create_dir(sub_path));

    // Create file in subdirectory
    char file_path[512];
#ifdef _WIN32
    snprintf(file_path, sizeof(file_path), "%s\\nested_file.txt", sub_path);
#else
    snprintf(file_path, sizeof(file_path), "%s/nested_file.txt", sub_path);
#endif
    ASSERT(create_test_file(file_path, 500, 0x33));

    FILE_MANIFEST *m = Chunker_BuildManifestFromDirectory(dir_path);
    ASSERT(m != NULL);
    ASSERT_EQ(m->version, WH_MANIFEST_VERSION_2);
    ASSERT_EQ(m->file_count, 1u);

    // Verify relative path uses '/' separator
    ASSERT(m->files[0].relative_path != NULL);
    ASSERT(strstr(m->files[0].relative_path, "subdir/nested_file.txt") != NULL);

    // Should not contain backslashes in the relative path
    ASSERT(strchr(m->files[0].relative_path, '\\') == NULL);

    Manifest_Destroy(m);
    PASS();
}

// ============================================================================
// Suite
// ============================================================================

SUITE(chunker_suite)
{
    RUN_TEST(test_build_manifest_small_file);
    RUN_TEST(test_build_manifest_multi_chunk);
    RUN_TEST(test_build_manifest_deterministic);
    RUN_TEST(test_build_manifest_from_directory);
    RUN_TEST(test_build_manifest_nested_dir);
}

int main(void)
{
    setup_test_home();
    atexit(cleanup_test_home);

    GREATEST_MAIN_BEGIN();
    RUN_SUITE(chunker_suite);
    GREATEST_MAIN_END();
}
