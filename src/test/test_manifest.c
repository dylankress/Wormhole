//
// test_manifest.c
// Unit tests for manifest.c — create, serialize, deserialize, and validation.
//

#include "greatest.h"
#include "../manifest.h"
#include "../wire_format.h"
#include <string.h>

GREATEST_MAIN_DEFS();

// Helper: fill a hash buffer with a repeating byte value.
static void fill_hash(uint8_t hash[WH_HASH_SIZE], uint8_t value)
{
    memset(hash, value, WH_HASH_SIZE);
}

// ============================================================================
// Manifest_Create tests
// ============================================================================

TEST test_create_basic(void)
{
    FILE_MANIFEST *m = Manifest_Create("test.txt", 1024 * 1024);  // 1 MB
    ASSERT(m != NULL);
    ASSERT_EQ(m->file_size, 1024ULL * 1024);
    ASSERT_EQ(m->chunk_size, (uint32_t)WH_CHUNK_SIZE);
    // 1MB / 256KB = 4 chunks exactly
    ASSERT_EQ(m->chunk_count, 4u);
    ASSERT_EQ(m->filename_length, 8);
    ASSERT(strcmp(m->filename, "test.txt") == 0);
    ASSERT(m->chunks != NULL);
    Manifest_Destroy(m);
    PASS();
}

TEST test_create_zero_size(void)
{
    FILE_MANIFEST *m = Manifest_Create("empty.bin", 0);
    ASSERT(m != NULL);
    ASSERT_EQ(m->file_size, 0ULL);
    ASSERT_EQ(m->chunk_count, 0u);
    ASSERT(m->chunks == NULL);
    Manifest_Destroy(m);
    PASS();
}

TEST test_create_partial_chunk(void)
{
    // 256KB + 1 byte = 2 chunks (second chunk is 1 byte)
    uint64_t size = (uint64_t)WH_CHUNK_SIZE + 1;
    FILE_MANIFEST *m = Manifest_Create("partial.dat", size);
    ASSERT(m != NULL);
    ASSERT_EQ(m->chunk_count, 2u);
    Manifest_Destroy(m);
    PASS();
}

TEST test_create_exact_boundary(void)
{
    // Exactly 256KB = 1 chunk
    FILE_MANIFEST *m = Manifest_Create("exact.dat", WH_CHUNK_SIZE);
    ASSERT(m != NULL);
    ASSERT_EQ(m->chunk_count, 1u);
    Manifest_Destroy(m);
    PASS();
}

TEST test_create_large_file(void)
{
    // 10 GB file
    uint64_t size = 10ULL * 1024 * 1024 * 1024;
    FILE_MANIFEST *m = Manifest_Create("huge.iso", size);
    ASSERT(m != NULL);
    // 10GB / 256KB = 40960 chunks
    ASSERT_EQ(m->chunk_count, 40960u);
    Manifest_Destroy(m);
    PASS();
}

TEST test_create_null_filename(void)
{
    FILE_MANIFEST *m = Manifest_Create(NULL, 1024);
    ASSERT(m == NULL);
    PASS();
}

// ============================================================================
// Serialize / Deserialize round-trip tests
// ============================================================================

TEST test_roundtrip_basic(void)
{
    // Create manifest with 2 chunks
    uint64_t file_size = (uint64_t)WH_CHUNK_SIZE + 100;
    FILE_MANIFEST *m = Manifest_Create("roundtrip.txt", file_size);
    ASSERT(m != NULL);
    ASSERT_EQ(m->chunk_count, 2u);

    // Fill in chunk info
    uint8_t hash1[WH_HASH_SIZE], hash2[WH_HASH_SIZE];
    fill_hash(hash1, 0xAA);
    fill_hash(hash2, 0xBB);
    Manifest_AddChunk(m, 0, hash1, WH_CHUNK_SIZE);
    Manifest_AddChunk(m, 1, hash2, 100);

    // Compute hash and serialize
    Manifest_ComputeHash(m);
    size_t sz = 0;
    uint8_t *data = Manifest_Serialize(m, &sz);
    ASSERT(data != NULL);
    ASSERT(sz > 0);

    // Deserialize and compare all fields
    FILE_MANIFEST *m2 = Manifest_Deserialize(data, sz);
    ASSERT(m2 != NULL);
    ASSERT_EQ(m2->file_size, m->file_size);
    ASSERT_EQ(m2->chunk_size, m->chunk_size);
    ASSERT_EQ(m2->chunk_count, m->chunk_count);
    ASSERT_EQ(m2->filename_length, m->filename_length);
    ASSERT(strcmp(m2->filename, m->filename) == 0);
    ASSERT_MEM_EQ(m2->manifest_hash, m->manifest_hash, WH_HASH_SIZE);

    for (uint32_t i = 0; i < m->chunk_count; i++)
    {
        ASSERT_MEM_EQ(m2->chunks[i].hash, m->chunks[i].hash, WH_HASH_SIZE);
        ASSERT_EQ(m2->chunks[i].chunk_size, m->chunks[i].chunk_size);
    }

    free(data);
    Manifest_Destroy(m);
    Manifest_Destroy(m2);
    PASS();
}

TEST test_roundtrip_single_chunk(void)
{
    FILE_MANIFEST *m = Manifest_Create("small.bin", 1000);
    ASSERT(m != NULL);
    ASSERT_EQ(m->chunk_count, 1u);

    uint8_t hash[WH_HASH_SIZE];
    fill_hash(hash, 0xCC);
    Manifest_AddChunk(m, 0, hash, 1000);
    Manifest_ComputeHash(m);

    size_t sz = 0;
    uint8_t *data = Manifest_Serialize(m, &sz);
    ASSERT(data != NULL);

    FILE_MANIFEST *m2 = Manifest_Deserialize(data, sz);
    ASSERT(m2 != NULL);
    ASSERT_EQ(m2->chunk_count, 1u);
    ASSERT_EQ(m2->chunks[0].chunk_size, 1000u);
    ASSERT_MEM_EQ(m2->chunks[0].hash, hash, WH_HASH_SIZE);

    free(data);
    Manifest_Destroy(m);
    Manifest_Destroy(m2);
    PASS();
}

TEST test_roundtrip_zero_size(void)
{
    FILE_MANIFEST *m = Manifest_Create("empty.bin", 0);
    ASSERT(m != NULL);
    ASSERT_EQ(m->chunk_count, 0u);

    Manifest_ComputeHash(m);

    size_t sz = 0;
    uint8_t *data = Manifest_Serialize(m, &sz);
    ASSERT(data != NULL);

    FILE_MANIFEST *m2 = Manifest_Deserialize(data, sz);
    ASSERT(m2 != NULL);
    ASSERT_EQ(m2->file_size, 0ULL);
    ASSERT_EQ(m2->chunk_count, 0u);
    ASSERT(strcmp(m2->filename, "empty.bin") == 0);

    free(data);
    Manifest_Destroy(m);
    Manifest_Destroy(m2);
    PASS();
}

TEST test_roundtrip_long_filename(void)
{
    // Create a 200-char filename
    char long_name[201];
    memset(long_name, 'x', 200);
    memcpy(long_name, "file_", 5);
    memcpy(long_name + 196, ".dat", 4);
    long_name[200] = '\0';

    FILE_MANIFEST *m = Manifest_Create(long_name, WH_CHUNK_SIZE);
    ASSERT(m != NULL);

    uint8_t hash[WH_HASH_SIZE];
    fill_hash(hash, 0xDD);
    Manifest_AddChunk(m, 0, hash, WH_CHUNK_SIZE);
    Manifest_ComputeHash(m);

    size_t sz = 0;
    uint8_t *data = Manifest_Serialize(m, &sz);
    ASSERT(data != NULL);

    FILE_MANIFEST *m2 = Manifest_Deserialize(data, sz);
    ASSERT(m2 != NULL);
    ASSERT(strcmp(m2->filename, long_name) == 0);
    ASSERT_EQ(m2->filename_length, 200);

    free(data);
    Manifest_Destroy(m);
    Manifest_Destroy(m2);
    PASS();
}

// ============================================================================
// Hash validation / tamper detection tests
// ============================================================================

TEST test_tampered_body_rejected(void)
{
    FILE_MANIFEST *m = Manifest_Create("secure.txt", WH_CHUNK_SIZE * 2);
    ASSERT(m != NULL);

    uint8_t hash1[WH_HASH_SIZE], hash2[WH_HASH_SIZE];
    fill_hash(hash1, 0x11);
    fill_hash(hash2, 0x22);
    Manifest_AddChunk(m, 0, hash1, WH_CHUNK_SIZE);
    Manifest_AddChunk(m, 1, hash2, WH_CHUNK_SIZE);
    Manifest_ComputeHash(m);

    size_t sz = 0;
    uint8_t *data = Manifest_Serialize(m, &sz);
    ASSERT(data != NULL);

    // Tamper: flip a byte in the body region (offset 40, past the header)
    data[40] ^= 0xFF;

    FILE_MANIFEST *m2 = Manifest_Deserialize(data, sz);
    ASSERT(m2 == NULL);

    free(data);
    Manifest_Destroy(m);
    PASS();
}

TEST test_bad_magic_rejected(void)
{
    FILE_MANIFEST *m = Manifest_Create("magic.txt", 1024);
    ASSERT(m != NULL);

    uint8_t hash[WH_HASH_SIZE];
    fill_hash(hash, 0x33);
    Manifest_AddChunk(m, 0, hash, 1024);
    Manifest_ComputeHash(m);

    size_t sz = 0;
    uint8_t *data = Manifest_Serialize(m, &sz);
    ASSERT(data != NULL);

    // Corrupt the magic number
    data[0] = 0x00;

    FILE_MANIFEST *m2 = Manifest_Deserialize(data, sz);
    ASSERT(m2 == NULL);

    free(data);
    Manifest_Destroy(m);
    PASS();
}

TEST test_truncated_data_rejected(void)
{
    FILE_MANIFEST *m = Manifest_Create("trunc.txt", WH_CHUNK_SIZE);
    ASSERT(m != NULL);

    uint8_t hash[WH_HASH_SIZE];
    fill_hash(hash, 0x44);
    Manifest_AddChunk(m, 0, hash, WH_CHUNK_SIZE);
    Manifest_ComputeHash(m);

    size_t sz = 0;
    uint8_t *data = Manifest_Serialize(m, &sz);
    ASSERT(data != NULL);

    // Pass truncated data (only 10 bytes — less than header)
    FILE_MANIFEST *m2 = Manifest_Deserialize(data, 10);
    ASSERT(m2 == NULL);

    free(data);
    Manifest_Destroy(m);
    PASS();
}

// ============================================================================
// Wire format verification
// ============================================================================

TEST test_magic_bytes(void)
{
    FILE_MANIFEST *m = Manifest_Create("wire.txt", 1024);
    ASSERT(m != NULL);

    uint8_t hash[WH_HASH_SIZE];
    fill_hash(hash, 0x55);
    Manifest_AddChunk(m, 0, hash, 1024);
    Manifest_ComputeHash(m);

    size_t sz = 0;
    uint8_t *data = Manifest_Serialize(m, &sz);
    ASSERT(data != NULL);

    // WH_MANIFEST_MAGIC = 0x4C4D4857, written little-endian = "WHML"
    ASSERT_EQ(data[0], 0x57);  // 'W'
    ASSERT_EQ(data[1], 0x48);  // 'H'
    ASSERT_EQ(data[2], 0x4D);  // 'M'
    ASSERT_EQ(data[3], 0x4C);  // 'L'
    // Version byte
    ASSERT_EQ(data[4], WH_MANIFEST_VERSION);

    free(data);
    Manifest_Destroy(m);
    PASS();
}

// ============================================================================
// Suites
// ============================================================================

SUITE(create_suite)
{
    RUN_TEST(test_create_basic);
    RUN_TEST(test_create_zero_size);
    RUN_TEST(test_create_partial_chunk);
    RUN_TEST(test_create_exact_boundary);
    RUN_TEST(test_create_large_file);
    RUN_TEST(test_create_null_filename);
}

SUITE(roundtrip_suite)
{
    RUN_TEST(test_roundtrip_basic);
    RUN_TEST(test_roundtrip_single_chunk);
    RUN_TEST(test_roundtrip_zero_size);
    RUN_TEST(test_roundtrip_long_filename);
}

SUITE(validation_suite)
{
    RUN_TEST(test_tampered_body_rejected);
    RUN_TEST(test_bad_magic_rejected);
    RUN_TEST(test_truncated_data_rejected);
    RUN_TEST(test_magic_bytes);
}

// ============================================================================
// Multi-file (v2) manifest tests
// ============================================================================

TEST test_create_multifile_basic(void)
{
    const char *paths[] = {"file_a.bin", "file_b.bin"};
    uint64_t sizes[] = {100 * 1024, 300 * 1024};  // 100KB + 300KB

    FILE_MANIFEST *m = Manifest_CreateMultiFile("mydir", paths, sizes, 2);
    ASSERT(m != NULL);
    ASSERT_EQ(m->version, WH_MANIFEST_VERSION_2);
    ASSERT_EQ(m->file_count, 2u);

    // 100KB < 256KB = 1 chunk, 300KB > 256KB = 2 chunks
    ASSERT_EQ(m->chunk_count, 3u);
    ASSERT_EQ(m->file_size, 400ULL * 1024);

    Manifest_Destroy(m);
    PASS();
}

TEST test_multifile_chunk_ranges(void)
{
    const char *paths[] = {"a.dat", "b.dat", "c.dat"};
    // a: 256KB = 1 chunk, b: 512KB = 2 chunks, c: 100KB = 1 chunk
    uint64_t sizes[] = {256 * 1024, 512 * 1024, 100 * 1024};

    FILE_MANIFEST *m = Manifest_CreateMultiFile("testdir", paths, sizes, 3);
    ASSERT(m != NULL);
    ASSERT_EQ(m->file_count, 3u);
    ASSERT_EQ(m->chunk_count, 4u);  // 1 + 2 + 1

    // Verify chunk ranges are contiguous and non-overlapping
    ASSERT_EQ(m->files[0].chunk_start, 0u);
    ASSERT_EQ(m->files[0].chunk_count, 1u);

    ASSERT_EQ(m->files[1].chunk_start, 1u);
    ASSERT_EQ(m->files[1].chunk_count, 2u);

    ASSERT_EQ(m->files[2].chunk_start, 3u);
    ASSERT_EQ(m->files[2].chunk_count, 1u);

    Manifest_Destroy(m);
    PASS();
}

TEST test_multifile_roundtrip(void)
{
    const char *paths[] = {"dir/alpha.txt", "beta.bin"};
    uint64_t sizes[] = {1000, 2000};

    FILE_MANIFEST *m = Manifest_CreateMultiFile("myfiles", paths, sizes, 2);
    ASSERT(m != NULL);
    ASSERT_EQ(m->chunk_count, 2u);  // each < 256KB = 1 chunk each

    // Add chunk hashes
    uint8_t hash1[WH_HASH_SIZE], hash2[WH_HASH_SIZE];
    fill_hash(hash1, 0xE1);
    fill_hash(hash2, 0xE2);
    Manifest_AddChunk(m, 0, hash1, 1000);
    Manifest_AddChunk(m, 1, hash2, 2000);

    Manifest_ComputeHash(m);

    // Serialize
    size_t sz = 0;
    uint8_t *data = Manifest_Serialize(m, &sz);
    ASSERT(data != NULL);
    ASSERT(sz > 0);

    // Deserialize
    FILE_MANIFEST *m2 = Manifest_Deserialize(data, sz);
    ASSERT(m2 != NULL);

    // Verify all fields
    ASSERT_EQ(m2->version, WH_MANIFEST_VERSION_2);
    ASSERT_EQ(m2->file_count, 2u);
    ASSERT_EQ(m2->file_size, m->file_size);
    ASSERT_EQ(m2->chunk_count, m->chunk_count);
    ASSERT_MEM_EQ(m2->manifest_hash, m->manifest_hash, WH_HASH_SIZE);
    ASSERT(strcmp(m2->filename, "myfiles") == 0);

    // Verify file entries
    for (uint32_t i = 0; i < m->file_count; i++)
    {
        ASSERT(strcmp(m2->files[i].relative_path, m->files[i].relative_path) == 0);
        ASSERT_EQ(m2->files[i].file_size, m->files[i].file_size);
        ASSERT_EQ(m2->files[i].chunk_start, m->files[i].chunk_start);
        ASSERT_EQ(m2->files[i].chunk_count, m->files[i].chunk_count);
    }

    // Verify chunk hashes
    for (uint32_t i = 0; i < m->chunk_count; i++)
    {
        ASSERT_MEM_EQ(m2->chunks[i].hash, m->chunks[i].hash, WH_HASH_SIZE);
        ASSERT_EQ(m2->chunks[i].chunk_size, m->chunks[i].chunk_size);
    }

    free(data);
    Manifest_Destroy(m);
    Manifest_Destroy(m2);
    PASS();
}

TEST test_multifile_total_size(void)
{
    const char *paths[] = {"x.bin", "y.bin", "z.bin"};
    uint64_t sizes[] = {10000, 20000, 30000};

    FILE_MANIFEST *m = Manifest_CreateMultiFile("sizedir", paths, sizes, 3);
    ASSERT(m != NULL);

    uint64_t sum = 0;
    for (uint32_t i = 0; i < m->file_count; i++)
    {
        sum += m->files[i].file_size;
    }
    ASSERT_EQ(sum, m->file_size);
    ASSERT_EQ(m->file_size, 60000ULL);

    Manifest_Destroy(m);
    PASS();
}

TEST test_multifile_single_file(void)
{
    const char *paths[] = {"only.dat"};
    uint64_t sizes[] = {500};

    FILE_MANIFEST *m = Manifest_CreateMultiFile("onedir", paths, sizes, 1);
    ASSERT(m != NULL);
    ASSERT_EQ(m->version, WH_MANIFEST_VERSION_2);
    ASSERT_EQ(m->file_count, 1u);
    ASSERT_EQ(m->chunk_count, 1u);
    ASSERT_EQ(m->file_size, 500ULL);
    ASSERT_EQ(m->files[0].chunk_start, 0u);
    ASSERT_EQ(m->files[0].chunk_count, 1u);

    Manifest_Destroy(m);
    PASS();
}

TEST test_multifile_empty_file(void)
{
    const char *paths[] = {"nonempty.bin", "empty.bin"};
    uint64_t sizes[] = {1024, 0};

    FILE_MANIFEST *m = Manifest_CreateMultiFile("mixdir", paths, sizes, 2);
    ASSERT(m != NULL);
    ASSERT_EQ(m->file_count, 2u);
    ASSERT_EQ(m->chunk_count, 1u);  // only nonempty contributes a chunk

    // nonempty: 1 chunk starting at 0
    ASSERT_EQ(m->files[0].chunk_start, 0u);
    ASSERT_EQ(m->files[0].chunk_count, 1u);
    ASSERT_EQ(m->files[0].file_size, 1024ULL);

    // empty: 0 chunks
    ASSERT_EQ(m->files[1].chunk_start, 1u);
    ASSERT_EQ(m->files[1].chunk_count, 0u);
    ASSERT_EQ(m->files[1].file_size, 0ULL);

    Manifest_Destroy(m);
    PASS();
}

SUITE(multifile_suite)
{
    RUN_TEST(test_create_multifile_basic);
    RUN_TEST(test_multifile_chunk_ranges);
    RUN_TEST(test_multifile_roundtrip);
    RUN_TEST(test_multifile_total_size);
    RUN_TEST(test_multifile_single_file);
    RUN_TEST(test_multifile_empty_file);
}

// ============================================================================
// Hardening tests (overflow, path traversal)
// ============================================================================

TEST test_overflow_chunk_count(void)
{
    // Craft raw manifest bytes with chunk_count = 0xFFFFFFFF
    // Should be rejected by the 16M sanity cap before any allocation
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    uint8_t *p = buf;

    // Header: magic + version + dummy hash
    WriteUint32LE(p, WH_MANIFEST_MAGIC); p += 4;
    *p = WH_MANIFEST_VERSION; p += 1;
    memset(p, 0, WH_HASH_SIZE); p += WH_HASH_SIZE;

    // Body: file_size + chunk_size + chunk_count + filename
    WriteUint64LE(p, 0); p += 8;
    WriteUint32LE(p, WH_CHUNK_SIZE); p += 4;
    WriteUint32LE(p, 0xFFFFFFFF); p += 4;
    WriteUint16LE(p, 4); p += 2;
    memcpy(p, "test", 4); p += 4;

    FILE_MANIFEST *m = Manifest_Deserialize(buf, (size_t)(p - buf));
    ASSERT(m == NULL);
    PASS();
}

TEST test_large_file_count_rejected(void)
{
    // Craft raw manifest bytes with file_count > 65536
    uint8_t buf[80];
    memset(buf, 0, sizeof(buf));
    uint8_t *p = buf;

    WriteUint32LE(p, WH_MANIFEST_MAGIC); p += 4;
    *p = WH_MANIFEST_VERSION_2; p += 1;
    memset(p, 0, WH_HASH_SIZE); p += WH_HASH_SIZE;

    WriteUint64LE(p, 0); p += 8;
    WriteUint32LE(p, WH_CHUNK_SIZE); p += 4;
    WriteUint32LE(p, 0); p += 4;  // chunk_count = 0
    WriteUint16LE(p, 4); p += 2;
    memcpy(p, "test", 4); p += 4;
    WriteUint32LE(p, 100000); p += 4;  // file_count > 65536

    FILE_MANIFEST *m = Manifest_Deserialize(buf, (size_t)(p - buf));
    ASSERT(m == NULL);
    PASS();
}

TEST test_path_traversal_rejected(void)
{
    // Create v2 manifest with "../evil.txt" path — should be rejected
    const char *paths[] = {"../evil.txt"};
    uint64_t sizes[] = {1000};

    FILE_MANIFEST *m = Manifest_CreateMultiFile("mydir", paths, sizes, 1);
    ASSERT(m != NULL);

    uint8_t hash[WH_HASH_SIZE];
    fill_hash(hash, 0xAB);
    Manifest_AddChunk(m, 0, hash, 1000);
    Manifest_ComputeHash(m);

    size_t sz = 0;
    uint8_t *data = Manifest_Serialize(m, &sz);
    ASSERT(data != NULL);

    // Deserialize should fail due to path traversal
    FILE_MANIFEST *m2 = Manifest_Deserialize(data, sz);
    ASSERT(m2 == NULL);

    free(data);
    Manifest_Destroy(m);
    PASS();
}

TEST test_absolute_path_rejected(void)
{
    // Create v2 manifest with absolute path — should be rejected
    const char *paths[] = {"/etc/passwd"};
    uint64_t sizes[] = {1000};

    FILE_MANIFEST *m = Manifest_CreateMultiFile("mydir", paths, sizes, 1);
    ASSERT(m != NULL);

    uint8_t hash[WH_HASH_SIZE];
    fill_hash(hash, 0xAC);
    Manifest_AddChunk(m, 0, hash, 1000);
    Manifest_ComputeHash(m);

    size_t sz = 0;
    uint8_t *data = Manifest_Serialize(m, &sz);
    ASSERT(data != NULL);

    FILE_MANIFEST *m2 = Manifest_Deserialize(data, sz);
    ASSERT(m2 == NULL);

    free(data);
    Manifest_Destroy(m);
    PASS();
}

TEST test_backslash_traversal_rejected(void)
{
    // Windows-style path traversal with backslash
    const char *paths[] = {"..\\evil.txt"};
    uint64_t sizes[] = {1000};

    FILE_MANIFEST *m = Manifest_CreateMultiFile("mydir", paths, sizes, 1);
    ASSERT(m != NULL);

    uint8_t hash[WH_HASH_SIZE];
    fill_hash(hash, 0xAD);
    Manifest_AddChunk(m, 0, hash, 1000);
    Manifest_ComputeHash(m);

    size_t sz = 0;
    uint8_t *data = Manifest_Serialize(m, &sz);
    ASSERT(data != NULL);

    FILE_MANIFEST *m2 = Manifest_Deserialize(data, sz);
    ASSERT(m2 == NULL);

    free(data);
    Manifest_Destroy(m);
    PASS();
}

TEST test_mid_path_traversal_rejected(void)
{
    // Traversal in the middle of a path: "foo/../../etc/passwd"
    const char *paths[] = {"foo/../../etc/passwd"};
    uint64_t sizes[] = {1000};

    FILE_MANIFEST *m = Manifest_CreateMultiFile("mydir", paths, sizes, 1);
    ASSERT(m != NULL);

    uint8_t hash[WH_HASH_SIZE];
    fill_hash(hash, 0xAE);
    Manifest_AddChunk(m, 0, hash, 1000);
    Manifest_ComputeHash(m);

    size_t sz = 0;
    uint8_t *data = Manifest_Serialize(m, &sz);
    ASSERT(data != NULL);

    FILE_MANIFEST *m2 = Manifest_Deserialize(data, sz);
    ASSERT(m2 == NULL);

    free(data);
    Manifest_Destroy(m);
    PASS();
}

TEST test_unknown_version_rejected(void)
{
    // Craft raw manifest bytes with version = 0 (invalid)
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    uint8_t *p = buf;

    WriteUint32LE(p, WH_MANIFEST_MAGIC); p += 4;
    *p = 0; p += 1;  // version 0 — invalid
    memset(p, 0, WH_HASH_SIZE); p += WH_HASH_SIZE;

    WriteUint64LE(p, 1024); p += 8;
    WriteUint32LE(p, WH_CHUNK_SIZE); p += 4;
    WriteUint32LE(p, 1); p += 4;
    WriteUint16LE(p, 4); p += 2;
    memcpy(p, "test", 4); p += 4;

    FILE_MANIFEST *m = Manifest_Deserialize(buf, (size_t)(p - buf));
    ASSERT(m == NULL);
    PASS();
}

TEST test_future_version_rejected(void)
{
    // Craft raw manifest bytes with version = 255 (future/unknown)
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    uint8_t *p = buf;

    WriteUint32LE(p, WH_MANIFEST_MAGIC); p += 4;
    *p = 255; p += 1;  // version 255 — unknown future version
    memset(p, 0, WH_HASH_SIZE); p += WH_HASH_SIZE;

    WriteUint64LE(p, 1024); p += 8;
    WriteUint32LE(p, WH_CHUNK_SIZE); p += 4;
    WriteUint32LE(p, 1); p += 4;
    WriteUint16LE(p, 4); p += 2;
    memcpy(p, "test", 4); p += 4;

    FILE_MANIFEST *m = Manifest_Deserialize(buf, (size_t)(p - buf));
    ASSERT(m == NULL);
    PASS();
}

SUITE(hardening_suite)
{
    RUN_TEST(test_overflow_chunk_count);
    RUN_TEST(test_large_file_count_rejected);
    RUN_TEST(test_path_traversal_rejected);
    RUN_TEST(test_absolute_path_rejected);
    RUN_TEST(test_backslash_traversal_rejected);
    RUN_TEST(test_mid_path_traversal_rejected);
    RUN_TEST(test_unknown_version_rejected);
    RUN_TEST(test_future_version_rejected);
}

int main(void)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(create_suite);
    RUN_SUITE(roundtrip_suite);
    RUN_SUITE(validation_suite);
    RUN_SUITE(multifile_suite);
    RUN_SUITE(hardening_suite);
    GREATEST_MAIN_END();
}
