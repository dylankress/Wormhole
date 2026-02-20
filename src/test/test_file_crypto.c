//
// test_file_crypto.c
// Unit tests for file_crypto.c — encryption/decryption roundtrip.
//

#include "../file_crypto.h"
#include "greatest.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sodium.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

GREATEST_MAIN_DEFS();

// ============================================================================
// Test environment
// ============================================================================

static char g_test_home[512];

static void setup_test_home(void)
{
    if (sodium_init() < 0)
    {
        fprintf(stderr, "Failed to initialize libsodium\n");
        exit(1);
    }

#ifdef _WIN32
    char temp[512];
    GetTempPathA(sizeof(temp), temp);
    snprintf(g_test_home, sizeof(g_test_home), "%swh_crypto_test_%lu",
             temp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(g_test_home, NULL);
#else
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/wh_crypto_test_%d", (int)getpid());
    mkdir(g_test_home, 0755);
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

static void make_path(char *out, size_t len, const char *name)
{
#ifdef _WIN32
    snprintf(out, len, "%s\\%s", g_test_home, name);
#else
    snprintf(out, len, "%s/%s", g_test_home, name);
#endif
}

// Create a test file with deterministic content
static BOOLEAN create_test_file(const char *path, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return FALSE;

    for (size_t i = 0; i < size; i++)
    {
        uint8_t b = (uint8_t)((i * 37 + 13) % 256);
        fwrite(&b, 1, 1, f);
    }
    fclose(f);
    return TRUE;
}

// Compare two files byte-by-byte
static BOOLEAN files_equal(const char *path_a, const char *path_b)
{
    FILE *fa = fopen(path_a, "rb");
    FILE *fb = fopen(path_b, "rb");
    if (!fa || !fb)
    {
        if (fa) fclose(fa);
        if (fb) fclose(fb);
        return FALSE;
    }

    BOOLEAN equal = TRUE;
    for (;;)
    {
        uint8_t a, b;
        size_t ra = fread(&a, 1, 1, fa);
        size_t rb = fread(&b, 1, 1, fb);
        if (ra != rb) { equal = FALSE; break; }
        if (ra == 0) break;  // Both EOF
        if (a != b) { equal = FALSE; break; }
    }

    fclose(fa);
    fclose(fb);
    return equal;
}

// ============================================================================
// Tests
// ============================================================================

TEST test_encrypt_decrypt_small(void)
{
    char plain[512], enc[512], dec[512];
    make_path(plain, sizeof(plain), "small.bin");
    make_path(enc, sizeof(enc), "small.enc");
    make_path(dec, sizeof(dec), "small.dec");

    ASSERT(create_test_file(plain, 1000));

    uint8_t key[FILE_CRYPTO_KEY_SIZE];
    FileCrypto_GenerateKey(key);

    ASSERT(FileCrypto_EncryptFile(plain, enc, key));
    ASSERT(FileCrypto_DecryptFile(enc, dec, key));
    ASSERT(files_equal(plain, dec));

    PASS();
}

TEST test_encrypt_decrypt_large(void)
{
    char plain[512], enc[512], dec[512];
    make_path(plain, sizeof(plain), "large.bin");
    make_path(enc, sizeof(enc), "large.enc");
    make_path(dec, sizeof(dec), "large.dec");

    // 1MB file — multiple stream chunks
    ASSERT(create_test_file(plain, 1024 * 1024));

    uint8_t key[FILE_CRYPTO_KEY_SIZE];
    FileCrypto_GenerateKey(key);

    ASSERT(FileCrypto_EncryptFile(plain, enc, key));
    ASSERT(FileCrypto_DecryptFile(enc, dec, key));
    ASSERT(files_equal(plain, dec));

    PASS();
}

TEST test_encrypt_decrypt_empty(void)
{
    char plain[512], enc[512], dec[512];
    make_path(plain, sizeof(plain), "empty.bin");
    make_path(enc, sizeof(enc), "empty.enc");
    make_path(dec, sizeof(dec), "empty.dec");

    ASSERT(create_test_file(plain, 0));

    uint8_t key[FILE_CRYPTO_KEY_SIZE];
    FileCrypto_GenerateKey(key);

    ASSERT(FileCrypto_EncryptFile(plain, enc, key));
    ASSERT(FileCrypto_DecryptFile(enc, dec, key));
    ASSERT(files_equal(plain, dec));

    PASS();
}

TEST test_wrong_key_fails(void)
{
    char plain[512], enc[512], dec[512];
    make_path(plain, sizeof(plain), "wrongkey.bin");
    make_path(enc, sizeof(enc), "wrongkey.enc");
    make_path(dec, sizeof(dec), "wrongkey.dec");

    ASSERT(create_test_file(plain, 5000));

    uint8_t key1[FILE_CRYPTO_KEY_SIZE], key2[FILE_CRYPTO_KEY_SIZE];
    FileCrypto_GenerateKey(key1);
    FileCrypto_GenerateKey(key2);

    ASSERT(FileCrypto_EncryptFile(plain, enc, key1));
    // Decryption with wrong key should fail
    ASSERT_FALSE(FileCrypto_DecryptFile(enc, dec, key2));

    PASS();
}

TEST test_ciphertext_differs(void)
{
    char plain[512], enc[512];
    make_path(plain, sizeof(plain), "differ.bin");
    make_path(enc, sizeof(enc), "differ.enc");

    ASSERT(create_test_file(plain, 10000));

    uint8_t key[FILE_CRYPTO_KEY_SIZE];
    FileCrypto_GenerateKey(key);

    ASSERT(FileCrypto_EncryptFile(plain, enc, key));

    // Ciphertext must differ from plaintext
    ASSERT_FALSE(files_equal(plain, enc));

    PASS();
}

TEST test_null_args(void)
{
    uint8_t key[FILE_CRYPTO_KEY_SIZE];
    FileCrypto_GenerateKey(key);

    ASSERT_FALSE(FileCrypto_EncryptFile(NULL, "out", key));
    ASSERT_FALSE(FileCrypto_EncryptFile("in", NULL, key));
    ASSERT_FALSE(FileCrypto_EncryptFile("in", "out", NULL));
    ASSERT_FALSE(FileCrypto_DecryptFile(NULL, "out", key));
    ASSERT_FALSE(FileCrypto_DecryptFile("in", NULL, key));
    ASSERT_FALSE(FileCrypto_DecryptFile("in", "out", NULL));

    PASS();
}

// ============================================================================
// Suite
// ============================================================================

SUITE(file_crypto_suite)
{
    RUN_TEST(test_encrypt_decrypt_small);
    RUN_TEST(test_encrypt_decrypt_large);
    RUN_TEST(test_encrypt_decrypt_empty);
    RUN_TEST(test_wrong_key_fails);
    RUN_TEST(test_ciphertext_differs);
    RUN_TEST(test_null_args);
}

int main(void)
{
    setup_test_home();
    atexit(cleanup_test_home);

    GREATEST_MAIN_BEGIN();
    RUN_SUITE(file_crypto_suite);
    GREATEST_MAIN_END();
}
