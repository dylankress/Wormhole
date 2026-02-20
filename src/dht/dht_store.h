//
// dht_store.h
// Wormhole DHT - Local value store for chunk location metadata.
// by Dylan Kress
//

#pragma once

#include "dht_protocol.h"
#include "../common.h"
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

#ifndef BOOLEAN
typedef unsigned char BOOLEAN;
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// Maximum entries in the DHT value store
#define DHT_STORE_CAPACITY      4096
// Maximum location entries per key
#define DHT_STORE_MAX_LOCATIONS 16
// Entry expiry (24 hours)
#define DHT_STORE_EXPIRY_SEC    86400

// ============================================================================
// Types
// ============================================================================

// A single chunk location entry: which node has the chunk
typedef struct {
    uint8_t  node_id[32];
    uint8_t  addr_type;
    uint8_t  addr[16];
    uint16_t port;
} DHT_LOCATION;

// A stored value: key (chunk hash) -> list of nodes that have it
typedef struct {
    uint8_t       key[32];
    DHT_LOCATION  locations[DHT_STORE_MAX_LOCATIONS];
    uint32_t      location_count;
    time_t        stored_at;
    time_t        last_refreshed;
} DHT_VALUE_ENTRY;

// The local DHT value store (thread-safe — IPC thread reads, main thread writes)
typedef struct {
    DHT_VALUE_ENTRY entries[DHT_STORE_CAPACITY];
    uint32_t        count;
#ifdef _WIN32
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t  lock;
#endif
} DHT_VALUE_STORE;

// ============================================================================
// Public API
// ============================================================================

// Initialize the value store (zero out, init lock)
void DhtStore_Init(DHT_VALUE_STORE *store);

// Destroy the value store (cleanup lock)
void DhtStore_Destroy(DHT_VALUE_STORE *store);

// Store or merge locations for a key. If the key already exists,
// new locations are merged (deduped by node_id). Returns TRUE on success.
BOOLEAN DhtStore_Put(DHT_VALUE_STORE *store, const uint8_t key[32],
                     const DHT_LOCATION *locations, uint32_t location_count);

// Get locations for a key. Copies into out, returns count found (0 if not found).
uint32_t DhtStore_Get(const DHT_VALUE_STORE *store, const uint8_t key[32],
                      DHT_LOCATION *out, uint32_t max);

// Check if a key exists in the store
BOOLEAN DhtStore_Has(const DHT_VALUE_STORE *store, const uint8_t key[32]);

// Remove a specific key from the store. Returns TRUE if found and removed.
BOOLEAN DhtStore_Remove(DHT_VALUE_STORE *store, const uint8_t key[32]);

// Remove entries older than DHT_STORE_EXPIRY_SEC
void DhtStore_ExpireOld(DHT_VALUE_STORE *store);

// Get total entry count
uint32_t DhtStore_GetCount(const DHT_VALUE_STORE *store);

// Save store to file
// Format: [4B magic "DHVS"][4B version][4B count][entries...]
BOOLEAN DhtStore_Save(const DHT_VALUE_STORE *store, const char *path);

// Load store from file
BOOLEAN DhtStore_Load(DHT_VALUE_STORE *store, const char *path);
