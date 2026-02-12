//
// crypto.c
// Wormhole Relay Server - Cryptographic Operations (Ed25519)
// by Dylan Kress
//

#include "crypto.h"
#include "relay_protocol.h"
#include <sodium.h>
#include <stdio.h>
#include <string.h>

bool Crypto_Init(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "[Crypto] Failed to initialize libsodium\n");
        return false;
    }
    
    printf("[Crypto] libsodium initialized (version: %s)\n", sodium_version_string());
    return true;
}

void Crypto_Cleanup(void) {
    // libsodium doesn't require cleanup
}

bool Crypto_VerifyRegisterSignature(
    const uint8_t peer_id[32],
    const uint8_t signature[64],
    uint64_t timestamp,
    const void* endpoints,
    uint16_t endpoint_count
) {
    // Build the message that was signed
    // Format: peer_id (32) || timestamp (8) || blake2b_hash(endpoints)
    
    // Calculate endpoints hash using BLAKE2b (32 bytes)
    uint8_t endpoints_hash[crypto_generichash_BYTES];
    size_t endpoints_size = endpoint_count * sizeof(ENDPOINT);
    
    if (crypto_generichash(endpoints_hash, sizeof(endpoints_hash),
                          endpoints, endpoints_size,
                          NULL, 0) != 0) {
        fprintf(stderr, "[Crypto] Failed to hash endpoints\n");
        return false;
    }
    
    // Build signed message: peer_id || timestamp || endpoints_hash
    uint8_t message[32 + 8 + 32];
    memcpy(message, peer_id, 32);
    
    // Write timestamp in little-endian
    message[32] = (timestamp >> 0) & 0xFF;
    message[33] = (timestamp >> 8) & 0xFF;
    message[34] = (timestamp >> 16) & 0xFF;
    message[35] = (timestamp >> 24) & 0xFF;
    message[36] = (timestamp >> 32) & 0xFF;
    message[37] = (timestamp >> 40) & 0xFF;
    message[38] = (timestamp >> 48) & 0xFF;
    message[39] = (timestamp >> 56) & 0xFF;
    
    memcpy(message + 40, endpoints_hash, 32);
    
    // Verify Ed25519 signature
    if (crypto_sign_verify_detached(signature, message, sizeof(message), peer_id) != 0) {
        fprintf(stderr, "[Crypto] Ed25519 signature verification failed\n");
        return false;
    }
    
    printf("[Crypto] Ed25519 signature verified successfully\n");
    return true;
}
