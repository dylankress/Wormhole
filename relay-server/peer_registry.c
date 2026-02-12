#define _GNU_SOURCE
//
// peer_registry.c
// Wormhole Relay Server - Peer Registration and Tracking
// by Dylan Kress
//

#include "peer_registry.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// FNV-1a hash (fast, good distribution for small keys)
static uint32_t hash_peer_id(const uint8_t peer_id[32], uint32_t bucket_count) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 32; i++) {
        hash ^= peer_id[i];
        hash *= 16777619u;
    }
    return hash & (bucket_count - 1);  // Assumes bucket_count is power of 2
}

// Generate unique session ID (simple incrementing counter + timestamp)
static uint64_t generate_session_id(void) {
    static uint64_t counter = 0;
    counter++;
    return ((uint64_t)time(NULL) << 32) | (counter & 0xFFFFFFFF);
}

// Compare two PeerIDs
static bool peer_id_equals(const uint8_t a[32], const uint8_t b[32]) {
    return memcmp(a, b, 32) == 0;
}

bool PeerRegistry_Init(PEER_REGISTRY* registry, uint32_t initial_capacity) {
    if (!registry || initial_capacity == 0) {
        return false;
    }
    
    // Round up to next power of 2
    uint32_t capacity = 16;
    while (capacity < initial_capacity) {
        capacity *= 2;
    }
    
    registry->buckets = (PEER_ENTRY**)calloc(capacity, sizeof(PEER_ENTRY*));
    if (!registry->buckets) {
        return false;
    }
    
    registry->bucket_count = capacity;
    registry->peer_count = 0;
    
#ifdef _WIN32
    InitializeCriticalSection(&registry->lock);
#else
    pthread_mutex_init(&registry->lock, NULL);
#endif
    
    printf("[PeerRegistry] Initialized with %u buckets\n", capacity);
    return true;
}

void PeerRegistry_Cleanup(PEER_REGISTRY* registry) {
    if (!registry || !registry->buckets) {
        return;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&registry->lock);
#else
    pthread_mutex_lock(&registry->lock);
#endif
    
    // Free all peer entries
    for (uint32_t i = 0; i < registry->bucket_count; i++) {
        PEER_ENTRY* entry = registry->buckets[i];
        while (entry) {
            PEER_ENTRY* next = entry->next;
            if (entry->ticket) {
                free(entry->ticket);
            }
            free(entry);
            entry = next;
        }
    }
    
    free(registry->buckets);
    registry->buckets = NULL;
    registry->bucket_count = 0;
    registry->peer_count = 0;
    
#ifdef _WIN32
    LeaveCriticalSection(&registry->lock);
    DeleteCriticalSection(&registry->lock);
#else
    pthread_mutex_unlock(&registry->lock);
    pthread_mutex_destroy(&registry->lock);
#endif
    
    printf("[PeerRegistry] Cleaned up\n");
}

uint64_t PeerRegistry_RegisterPeer(
    PEER_REGISTRY* registry,
    const uint8_t peer_id[32],
    const SOCKET_ADDR* socket_addr,
    socklen_t addr_len,
    const ENDPOINT* endpoints,
    uint16_t endpoint_count
) {
    if (!registry || !peer_id || !socket_addr || endpoint_count > MAX_ENDPOINTS) {
        return 0;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&registry->lock);
#else
    pthread_mutex_lock(&registry->lock);
#endif
    
    // Check if peer already exists
    uint32_t bucket = hash_peer_id(peer_id, registry->bucket_count);
    PEER_ENTRY* entry = registry->buckets[bucket];
    while (entry) {
        if (peer_id_equals(entry->peer_id, peer_id)) {
            // Update existing peer
            memcpy(&entry->socket_addr, socket_addr, addr_len);
            entry->addr_len = addr_len;
            memcpy(entry->endpoints, endpoints, endpoint_count * sizeof(ENDPOINT));
            entry->endpoint_count = endpoint_count;
            entry->last_keepalive = time(NULL);
            
            uint64_t session_id = entry->session_id;
            
#ifdef _WIN32
            LeaveCriticalSection(&registry->lock);
#else
            pthread_mutex_unlock(&registry->lock);
#endif
            
            printf("[PeerRegistry] Updated existing peer (session %llu)\n", 
                   (unsigned long long)session_id);
            return session_id;
        }
        entry = entry->next;
    }
    
    // Create new peer entry
    PEER_ENTRY* new_entry = (PEER_ENTRY*)malloc(sizeof(PEER_ENTRY));
    if (!new_entry) {
#ifdef _WIN32
        LeaveCriticalSection(&registry->lock);
#else
        pthread_mutex_unlock(&registry->lock);
#endif
        return 0;
    }
    
    memcpy(new_entry->peer_id, peer_id, 32);
    new_entry->session_id = generate_session_id();
    memcpy(&new_entry->socket_addr, socket_addr, addr_len);
    new_entry->addr_len = addr_len;
    memcpy(new_entry->endpoints, endpoints, endpoint_count * sizeof(ENDPOINT));
    new_entry->endpoint_count = endpoint_count;
    new_entry->registered_at = time(NULL);
    new_entry->last_keepalive = time(NULL);
    new_entry->ticket = NULL;
    
    // Insert at head of bucket
    new_entry->next = registry->buckets[bucket];
    registry->buckets[bucket] = new_entry;
    registry->peer_count++;
    
    uint64_t session_id = new_entry->session_id;
    
#ifdef _WIN32
    LeaveCriticalSection(&registry->lock);
#else
    pthread_mutex_unlock(&registry->lock);
#endif
    
    printf("[PeerRegistry] Registered new peer (session %llu, total peers: %u)\n",
           (unsigned long long)session_id, registry->peer_count);
    return session_id;
}

PEER_ENTRY* PeerRegistry_FindByPeerID(PEER_REGISTRY* registry, const uint8_t peer_id[32]) {
    if (!registry || !peer_id) {
        return NULL;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&registry->lock);
#else
    pthread_mutex_lock(&registry->lock);
#endif
    
    uint32_t bucket = hash_peer_id(peer_id, registry->bucket_count);
    PEER_ENTRY* entry = registry->buckets[bucket];
    while (entry) {
        if (peer_id_equals(entry->peer_id, peer_id)) {
#ifdef _WIN32
            LeaveCriticalSection(&registry->lock);
#else
            pthread_mutex_unlock(&registry->lock);
#endif
            return entry;
        }
        entry = entry->next;
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&registry->lock);
#else
    pthread_mutex_unlock(&registry->lock);
#endif
    
    return NULL;
}

PEER_ENTRY* PeerRegistry_FindBySessionID(PEER_REGISTRY* registry, uint64_t session_id) {
    if (!registry || session_id == 0) {
        return NULL;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&registry->lock);
#else
    pthread_mutex_lock(&registry->lock);
#endif
    
    // Linear search (not optimized, but session lookups are infrequent)
    for (uint32_t i = 0; i < registry->bucket_count; i++) {
        PEER_ENTRY* entry = registry->buckets[i];
        while (entry) {
            if (entry->session_id == session_id) {
#ifdef _WIN32
                LeaveCriticalSection(&registry->lock);
#else
                pthread_mutex_unlock(&registry->lock);
#endif
                return entry;
            }
            entry = entry->next;
        }
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&registry->lock);
#else
    pthread_mutex_unlock(&registry->lock);
#endif
    
    return NULL;
}

PEER_ENTRY* PeerRegistry_FindByTicket(PEER_REGISTRY* registry, const char* ticket) {
    if (!registry || !ticket) {
        return NULL;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&registry->lock);
#else
    pthread_mutex_lock(&registry->lock);
#endif
    
    // Linear search (not optimized, but ticket lookups are infrequent)
    for (uint32_t i = 0; i < registry->bucket_count; i++) {
        PEER_ENTRY* entry = registry->buckets[i];
        while (entry) {
            if (entry->ticket && strcmp(entry->ticket, ticket) == 0) {
#ifdef _WIN32
                LeaveCriticalSection(&registry->lock);
#else
                pthread_mutex_unlock(&registry->lock);
#endif
                return entry;
            }
            entry = entry->next;
        }
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&registry->lock);
#else
    pthread_mutex_unlock(&registry->lock);
#endif
    
    return NULL;
}

bool PeerRegistry_UpdateKeepalive(PEER_REGISTRY* registry, uint64_t session_id) {
    PEER_ENTRY* entry = PeerRegistry_FindBySessionID(registry, session_id);
    if (!entry) {
        return false;
    }
    
    entry->last_keepalive = time(NULL);
    return true;
}

bool PeerRegistry_SetTicket(PEER_REGISTRY* registry, uint64_t session_id, const char* ticket) {
    if (!registry || !ticket) {
        return false;
    }
    
    PEER_ENTRY* entry = PeerRegistry_FindBySessionID(registry, session_id);
    if (!entry) {
        return false;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&registry->lock);
#else
    pthread_mutex_lock(&registry->lock);
#endif
    
    // Free old ticket if exists
    if (entry->ticket) {
        free(entry->ticket);
    }
    
    entry->ticket = strdup(ticket);
    
#ifdef _WIN32
    LeaveCriticalSection(&registry->lock);
#else
    pthread_mutex_unlock(&registry->lock);
#endif
    
    printf("[PeerRegistry] Associated ticket '%s' with session %llu\n",
           ticket, (unsigned long long)session_id);
    return true;
}

bool PeerRegistry_RemovePeer(PEER_REGISTRY* registry, uint64_t session_id) {
    if (!registry || session_id == 0) {
        return false;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&registry->lock);
#else
    pthread_mutex_lock(&registry->lock);
#endif
    
    // Search all buckets
    for (uint32_t i = 0; i < registry->bucket_count; i++) {
        PEER_ENTRY** prev = &registry->buckets[i];
        PEER_ENTRY* entry = registry->buckets[i];
        
        while (entry) {
            if (entry->session_id == session_id) {
                // Remove from chain
                *prev = entry->next;
                
                if (entry->ticket) {
                    free(entry->ticket);
                }
                free(entry);
                
                registry->peer_count--;
                
#ifdef _WIN32
                LeaveCriticalSection(&registry->lock);
#else
                pthread_mutex_unlock(&registry->lock);
#endif
                
                printf("[PeerRegistry] Removed peer (session %llu, total peers: %u)\n",
                       (unsigned long long)session_id, registry->peer_count);
                return true;
            }
            
            prev = &entry->next;
            entry = entry->next;
        }
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&registry->lock);
#else
    pthread_mutex_unlock(&registry->lock);
#endif
    
    return false;
}

uint32_t PeerRegistry_RemoveStalePeers(PEER_REGISTRY* registry, time_t current_time) {
    if (!registry) {
        return 0;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&registry->lock);
#else
    pthread_mutex_lock(&registry->lock);
#endif
    
    uint32_t removed = 0;
    const time_t timeout = 60;  // 60 seconds without keepalive = stale
    
    for (uint32_t i = 0; i < registry->bucket_count; i++) {
        PEER_ENTRY** prev = &registry->buckets[i];
        PEER_ENTRY* entry = registry->buckets[i];
        
        while (entry) {
            if (current_time - entry->last_keepalive > timeout) {
                // Stale peer, remove it
                *prev = entry->next;
                
                printf("[PeerRegistry] Removing stale peer (session %llu, last keepalive %lds ago)\n",
                       (unsigned long long)entry->session_id,
                       (long)(current_time - entry->last_keepalive));
                
                if (entry->ticket) {
                    free(entry->ticket);
                }
                
                PEER_ENTRY* to_free = entry;
                entry = entry->next;
                free(to_free);
                
                registry->peer_count--;
                removed++;
            } else {
                prev = &entry->next;
                entry = entry->next;
            }
        }
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&registry->lock);
#else
    pthread_mutex_unlock(&registry->lock);
#endif
    
    if (removed > 0) {
        printf("[PeerRegistry] Removed %u stale peers (total peers: %u)\n",
               removed, registry->peer_count);
    }
    
    return removed;
}

void PeerRegistry_GetStats(PEER_REGISTRY* registry, uint32_t* peer_count, uint32_t* bucket_count) {
    if (!registry) {
        return;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&registry->lock);
#else
    pthread_mutex_lock(&registry->lock);
#endif
    
    if (peer_count) {
        *peer_count = registry->peer_count;
    }
    if (bucket_count) {
        *bucket_count = registry->bucket_count;
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&registry->lock);
#else
    pthread_mutex_unlock(&registry->lock);
#endif
}
