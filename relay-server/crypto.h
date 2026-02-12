//
// crypto.h
// Wormhole Relay Server - Cryptographic Operations (Ed25519)
// by Dylan Kress
//

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Ed25519 signature verification for REGISTER messages
// Signature format: Sign(peer_id || timestamp || endpoints_hash)
//
// Parameters:
//   peer_id: 32-byte Ed25519 public key
//   signature: 64-byte Ed25519 signature
//   timestamp: 8-byte Unix timestamp
//   endpoints: Array of ENDPOINT structures
//   endpoint_count: Number of endpoints
//
// Returns: true if signature is valid, false otherwise
bool Crypto_VerifyRegisterSignature(
    const uint8_t peer_id[32],
    const uint8_t signature[64],
    uint64_t timestamp,
    const void* endpoints,
    uint16_t endpoint_count
);

// Initialize libsodium
// Returns: true on success, false on failure
bool Crypto_Init(void);

// Cleanup (no-op for libsodium, but included for consistency)
void Crypto_Cleanup(void);
