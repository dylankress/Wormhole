//
// peer_id.h
// Wormhole - Ed25519 Keypair Management (Peer Identity)
// by Dylan Kress
//

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Ed25519 key sizes
#define PEER_ID_SIZE 32       // Public key size
#define SECRET_KEY_SIZE 32    // Secret key size (seed)
#define KEYPAIR_SIZE 64       // Full keypair size (libsodium format)

// Keypair structure
typedef struct {
    uint8_t public_key[PEER_ID_SIZE];     // Ed25519 public key (this is the PeerID)
    uint8_t secret_key[SECRET_KEY_SIZE];  // Ed25519 secret key (seed)
} KEYPAIR;

// Initialize keypair subsystem (initializes libsodium)
// Returns: true on success, false on failure
bool PeerID_Init(void);

// Generate a new random keypair
// Returns: true on success, false on failure
bool PeerID_Generate(KEYPAIR* keypair);

// Load keypair from file (or generate new if doesn't exist)
// Default path: ~/.wormhole/identity
// Returns: true on success, false on failure
bool PeerID_LoadOrGenerate(KEYPAIR* keypair, const char* path);

// Save keypair to file
// Returns: true on success, false on failure
bool PeerID_Save(const KEYPAIR* keypair, const char* path);

// Sign a message with the secret key
// signature_out must be at least 64 bytes
// Returns: true on success, false on failure
bool PeerID_Sign(const KEYPAIR* keypair, const uint8_t* message, size_t message_len,
                 uint8_t signature_out[64]);

// Verify a signature with a public key
// Returns: true if signature is valid, false otherwise
bool PeerID_Verify(const uint8_t public_key[PEER_ID_SIZE], const uint8_t* message,
                   size_t message_len, const uint8_t signature[64]);

// Get default identity file path
// Returns: Allocated string (caller must free), or NULL on error
char* PeerID_GetDefaultPath(void);

// Convert public key to hex string (for display)
// hex_out must be at least 65 bytes (64 hex chars + null terminator)
void PeerID_ToHex(const uint8_t public_key[PEER_ID_SIZE], char hex_out[65]);

// Parse hex string to public key
// Returns: true on success, false on failure
bool PeerID_FromHex(const char* hex, uint8_t public_key[PEER_ID_SIZE]);
