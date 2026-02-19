//
// peer_id.c
// Wormhole - Ed25519 Keypair Management (Peer Identity)
// by Dylan Kress
//

#include "peer_id.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

bool PeerID_Init(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "[PeerID] Failed to initialize libsodium\n");
        return false;
    }
    return true;
}

bool PeerID_Generate(KEYPAIR* keypair) {
    if (!keypair) {
        return false;
    }
    
    // Generate Ed25519 keypair
    uint8_t full_keypair[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(keypair->public_key, full_keypair);
    
    // Extract seed (first 32 bytes of secret key)
    memcpy(keypair->secret_key, full_keypair, SECRET_KEY_SIZE);
    
    // Zero out temporary buffer
    sodium_memzero(full_keypair, sizeof(full_keypair));
    
    printf("[PeerID] Generated new keypair\n");
    return true;
}

char* PeerID_GetDefaultPath(void) {
#ifdef _WIN32
    char* appdata = getenv("APPDATA");
    if (!appdata) {
        fprintf(stderr, "[PeerID] Failed to get APPDATA path\n");
        return NULL;
    }
    
    size_t len = strlen(appdata) + strlen("\\.wormhole\\identity") + 1;
    char* path = (char*)malloc(len);
    if (!path) {
        return NULL;
    }
    
    snprintf(path, len, "%s\\.wormhole\\identity", appdata);
#else
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (!pw) {
            fprintf(stderr, "[PeerID] Failed to get home directory\n");
            return NULL;
        }
        home = pw->pw_dir;
    }
    
    size_t len = strlen(home) + strlen("/.wormhole/identity") + 1;
    char* path = (char*)malloc(len);
    if (!path) {
        return NULL;
    }
    
    snprintf(path, len, "%s/.wormhole/identity", home);
#endif
    
    return path;
}

static bool create_directory(const char* path) {
#ifdef _WIN32
    if (CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }
    fprintf(stderr, "[PeerID] Failed to create directory: %s\n", path);
    return false;
#else
    if (mkdir(path, 0700) == 0 || errno == EEXIST) {
        return true;
    }
    fprintf(stderr, "[PeerID] Failed to create directory: %s (%s)\n", path, strerror(errno));
    return false;
#endif
}

bool PeerID_Save(const KEYPAIR* keypair, const char* path) {
    if (!keypair || !path) {
        return false;
    }
    
    // Create directory if needed
    char* dir_path = strdup(path);
    if (!dir_path) {
        return false;
    }
    
    // Find last path separator
    char* last_sep = strrchr(dir_path, '/');
#ifdef _WIN32
    char* last_sep_win = strrchr(dir_path, '\\');
    if (last_sep_win > last_sep) {
        last_sep = last_sep_win;
    }
#endif
    
    if (last_sep) {
        *last_sep = '\0';
        if (!create_directory(dir_path)) {
            free(dir_path);
            return false;
        }
    }
    free(dir_path);
    
    // Open file for writing (binary mode, create if doesn't exist, truncate)
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[PeerID] Failed to open file for writing: %s (%s)\n",
                path, strerror(errno));
        return false;
    }
    
    // Write keypair (64 bytes: 32 public + 32 secret)
    size_t written = fwrite(keypair->public_key, 1, PEER_ID_SIZE, f);
    written += fwrite(keypair->secret_key, 1, SECRET_KEY_SIZE, f);
    
    fclose(f);
    
    if (written != KEYPAIR_SIZE) {
        fprintf(stderr, "[PeerID] Failed to write keypair to file\n");
        return false;
    }
    
    // Set file permissions (owner read/write only)
#ifndef _WIN32
    chmod(path, 0600);
#endif
    
    printf("[PeerID] Saved keypair to %s\n", path);
    return true;
}

static bool load_from_file(KEYPAIR* keypair, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    
    // Read keypair
    size_t read_bytes = fread(keypair->public_key, 1, PEER_ID_SIZE, f);
    read_bytes += fread(keypair->secret_key, 1, SECRET_KEY_SIZE, f);
    
    fclose(f);
    
    if (read_bytes != KEYPAIR_SIZE) {
        fprintf(stderr, "[PeerID] Invalid keypair file (expected %d bytes, got %zu)\n",
                KEYPAIR_SIZE, read_bytes);
        return false;
    }
    
    printf("[PeerID] Loaded keypair from %s\n", path);
    return true;
}

bool PeerID_LoadOrGenerate(KEYPAIR* keypair, const char* path) {
    if (!keypair || !path) {
        return false;
    }
    
    // Try to load existing keypair
    if (load_from_file(keypair, path)) {
        return true;
    }
    
    // File doesn't exist, generate new keypair
    printf("[PeerID] No existing keypair found, generating new one...\n");
    if (!PeerID_Generate(keypair)) {
        return false;
    }
    
    // Save for future use
    return PeerID_Save(keypair, path);
}

bool PeerID_Sign(const KEYPAIR* keypair, const uint8_t* message, size_t message_len,
                 uint8_t signature_out[64]) {
    if (!keypair || !message || !signature_out) {
        return false;
    }
    
    // Reconstruct full secret key from seed
    uint8_t full_secret[crypto_sign_SECRETKEYBYTES];
    crypto_sign_seed_keypair(full_secret + crypto_sign_PUBLICKEYBYTES, full_secret,
                            keypair->secret_key);
    
    // Sign message (detached signature)
    unsigned long long sig_len;
    if (crypto_sign_detached(signature_out, &sig_len, message, message_len, full_secret) != 0) {
        sodium_memzero(full_secret, sizeof(full_secret));
        return false;
    }
    
    sodium_memzero(full_secret, sizeof(full_secret));
    return true;
}

bool PeerID_Verify(const uint8_t public_key[PEER_ID_SIZE], const uint8_t* message,
                   size_t message_len, const uint8_t signature[64]) {
    if (!public_key || !message || !signature) {
        return false;
    }
    
    return crypto_sign_verify_detached(signature, message, message_len, public_key) == 0;
}

void PeerID_ToHex(const uint8_t public_key[PEER_ID_SIZE], char hex_out[65]) {
    if (!public_key || !hex_out) {
        return;
    }
    
    for (int i = 0; i < PEER_ID_SIZE; i++) {
        snprintf(hex_out + (i * 2), 3, "%02x", public_key[i]);
    }
    hex_out[64] = '\0';
}

static int hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool PeerID_FromHex(const char* hex, uint8_t public_key[PEER_ID_SIZE]) {
    if (!hex || !public_key) {
        return false;
    }
    
    if (strlen(hex) != 64) {
        return false;
    }
    
    for (int i = 0; i < PEER_ID_SIZE; i++) {
        int high = hex_char_to_int(hex[i * 2]);
        int low = hex_char_to_int(hex[i * 2 + 1]);
        
        if (high < 0 || low < 0) {
            return false;
        }
        
        public_key[i] = (uint8_t)((high << 4) | low);
    }
    
    return true;
}
