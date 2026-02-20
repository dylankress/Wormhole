//
// file_crypto.c
// Client-side file encryption using libsodium crypto_secretstream.
// by Dylan Kress
//

#include "file_crypto.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Stream chunk size for encryption (64KB — small enough for memory, large enough for throughput)
#define STREAM_CHUNK_SIZE (64 * 1024)

void FileCrypto_GenerateKey(uint8_t key[FILE_CRYPTO_KEY_SIZE])
{
    randombytes_buf(key, FILE_CRYPTO_KEY_SIZE);
}

BOOLEAN FileCrypto_EncryptFile(const char *input_path, const char *output_path,
                                const uint8_t key[FILE_CRYPTO_KEY_SIZE])
{
    if (!input_path || !output_path || !key) return FALSE;

    FILE *in = fopen(input_path, "rb");
    if (!in) return FALSE;

    FILE *out = fopen(output_path, "wb");
    if (!out)
    {
        fclose(in);
        return FALSE;
    }

    // Initialize secretstream
    crypto_secretstream_xchacha20poly1305_state state;
    uint8_t header[crypto_secretstream_xchacha20poly1305_HEADERBYTES];

    if (crypto_secretstream_xchacha20poly1305_init_push(&state, header, key) != 0)
    {
        fclose(in);
        fclose(out);
        return FALSE;
    }

    // Write header to output
    if (fwrite(header, 1, sizeof(header), out) != sizeof(header))
    {
        fclose(in);
        fclose(out);
        return FALSE;
    }

    uint8_t *plaintext = (uint8_t *)malloc(STREAM_CHUNK_SIZE);
    uint8_t *ciphertext = (uint8_t *)malloc(STREAM_CHUNK_SIZE +
        crypto_secretstream_xchacha20poly1305_ABYTES);
    if (!plaintext || !ciphertext)
    {
        free(plaintext);
        free(ciphertext);
        fclose(in);
        fclose(out);
        return FALSE;
    }

    BOOLEAN success = TRUE;

    for (;;)
    {
        size_t bytes_read = fread(plaintext, 1, STREAM_CHUNK_SIZE, in);
        if (bytes_read == 0) break;

        // Check if this is the last chunk
        int eof = feof(in);
        uint8_t tag = eof ? crypto_secretstream_xchacha20poly1305_TAG_FINAL
                          : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE;

        unsigned long long ciphertext_len;
        if (crypto_secretstream_xchacha20poly1305_push(&state, ciphertext, &ciphertext_len,
                plaintext, bytes_read, NULL, 0, tag) != 0)
        {
            success = FALSE;
            break;
        }

        if (fwrite(ciphertext, 1, (size_t)ciphertext_len, out) != (size_t)ciphertext_len)
        {
            success = FALSE;
            break;
        }

        if (eof) break;
    }

    free(plaintext);
    free(ciphertext);
    fclose(in);
    fclose(out);

    if (!success)
        remove(output_path);

    return success;
}

BOOLEAN FileCrypto_DecryptFile(const char *input_path, const char *output_path,
                                const uint8_t key[FILE_CRYPTO_KEY_SIZE])
{
    if (!input_path || !output_path || !key) return FALSE;

    FILE *in = fopen(input_path, "rb");
    if (!in) return FALSE;

    // Read header
    uint8_t header[crypto_secretstream_xchacha20poly1305_HEADERBYTES];
    if (fread(header, 1, sizeof(header), in) != sizeof(header))
    {
        fclose(in);
        return FALSE;
    }

    // Initialize secretstream for decryption
    crypto_secretstream_xchacha20poly1305_state state;
    if (crypto_secretstream_xchacha20poly1305_init_pull(&state, header, key) != 0)
    {
        fclose(in);
        return FALSE;
    }

    FILE *out = fopen(output_path, "wb");
    if (!out)
    {
        fclose(in);
        return FALSE;
    }

    size_t ct_chunk_size = STREAM_CHUNK_SIZE +
        crypto_secretstream_xchacha20poly1305_ABYTES;
    uint8_t *ciphertext = (uint8_t *)malloc(ct_chunk_size);
    uint8_t *plaintext = (uint8_t *)malloc(STREAM_CHUNK_SIZE);
    if (!ciphertext || !plaintext)
    {
        free(ciphertext);
        free(plaintext);
        fclose(in);
        fclose(out);
        return FALSE;
    }

    BOOLEAN success = TRUE;

    for (;;)
    {
        size_t bytes_read = fread(ciphertext, 1, ct_chunk_size, in);
        if (bytes_read == 0) break;

        unsigned long long plaintext_len;
        uint8_t tag;
        if (crypto_secretstream_xchacha20poly1305_pull(&state, plaintext, &plaintext_len,
                &tag, ciphertext, bytes_read, NULL, 0) != 0)
        {
            success = FALSE;  // Decryption/authentication failed
            break;
        }

        if (fwrite(plaintext, 1, (size_t)plaintext_len, out) != (size_t)plaintext_len)
        {
            success = FALSE;
            break;
        }

        if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL)
            break;
    }

    free(ciphertext);
    free(plaintext);
    fclose(in);
    fclose(out);

    if (!success)
        remove(output_path);

    return success;
}
