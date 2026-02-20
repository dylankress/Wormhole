//
// file_crypto.h
// Client-side file encryption using libsodium crypto_secretstream.
// by Dylan Kress
//

#pragma once

#include "common.h"
#include <stdint.h>

// Encryption key size (XChaCha20-Poly1305 via crypto_secretstream)
#define FILE_CRYPTO_KEY_SIZE 32

// Generate a random 32-byte encryption key.
void FileCrypto_GenerateKey(uint8_t key[FILE_CRYPTO_KEY_SIZE]);

// Encrypt a file: reads input_path, writes encrypted data to output_path.
// Uses libsodium crypto_secretstream (XChaCha20-Poly1305) with streaming chunks.
// Returns TRUE on success.
BOOLEAN FileCrypto_EncryptFile(const char *input_path, const char *output_path,
                                const uint8_t key[FILE_CRYPTO_KEY_SIZE]);

// Decrypt a file: reads encrypted input_path, writes plaintext to output_path.
// Returns TRUE on success.
BOOLEAN FileCrypto_DecryptFile(const char *input_path, const char *output_path,
                                const uint8_t key[FILE_CRYPTO_KEY_SIZE]);
