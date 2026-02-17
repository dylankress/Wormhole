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
    
    // Debug: Log stored endpoints
    printf("[PeerRegistry] [DEBUG] Storing %u endpoints:\n", endpoint_count);
    for (uint16_t i = 0; i < endpoint_count; i++) {
        printf("[PeerRegistry] [DEBUG]   EP[%u]: type=0x%02x, port=0x%04x (%u), priority=%u\n",
               i, endpoints[i].addr_type, endpoints[i].port,
               endpoints[i].port, endpoints[i].priority);
    }
    
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

// Compare two socket addresses (IP + port)
static bool sockaddr_equals(const struct sockaddr_storage* a, socklen_t a_len,
                            const struct sockaddr* b, socklen_t b_len) {
    (void)a_len;
    (void)b_len;
    if (a->ss_family != b->sa_family) {
        return false;
    }

    if (a->ss_family == AF_INET) {
        const struct sockaddr_in* sa = (const struct sockaddr_in*)a;
        const struct sockaddr_in* sb = (const struct sockaddr_in*)b;
        return sa->sin_port == sb->sin_port &&
               sa->sin_addr.s_addr == sb->sin_addr.s_addr;
    } else if (a->ss_family == AF_INET6) {
        const struct sockaddr_in6* sa = (const struct sockaddr_in6*)a;
        const struct sockaddr_in6* sb = (const struct sockaddr_in6*)b;
        return sa->sin6_port == sb->sin6_port &&
               memcmp(&sa->sin6_addr, &sb->sin6_addr, sizeof(struct in6_addr)) == 0;
    }

    return false;
}

PEER_ENTRY* PeerRegistry_FindByAddress(PEER_REGISTRY* registry,
                                       const struct sockaddr* addr, socklen_t addr_len) {
    if (!registry || !addr) {
        return NULL;
    }

#ifdef _WIN32
    EnterCriticalSection(&registry->lock);
#else
    pthread_mutex_lock(&registry->lock);
#endif

    for (uint32_t i = 0; i < registry->bucket_count; i++) {
        PEER_ENTRY* entry = registry->buckets[i];
        while (entry) {
            if (sockaddr_equals(&entry->socket_addr, entry->addr_len, addr, addr_len)) {
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

uint32_t PeerRegistry_FindActivePeers(
    PEER_REGISTRY* registry,
    const uint8_t exclude_peer_id[32],
    time_t current_time,
    time_t min_age_sec,
    PEER_ENTRY** peers_out,
    uint32_t max_peers
) {
    if (!registry || !peers_out || max_peers == 0) {
        return 0;
    }

#ifdef _WIN32
    EnterCriticalSection(&registry->lock);
#else
    pthread_mutex_lock(&registry->lock);
#endif

    uint32_t found = 0;
    const time_t keepalive_timeout = 60;  // Must have keepalive within 60s

    for (uint32_t i = 0; i < registry->bucket_count && found < max_peers; i++) {
        PEER_ENTRY* entry = registry->buckets[i];
        while (entry && found < max_peers) {
            // Skip the requesting peer
            if (exclude_peer_id && peer_id_equals(entry->peer_id, exclude_peer_id)) {
                entry = entry->next;
                continue;
            }

            // Must be registered for at least min_age_sec
            if (current_time - entry->registered_at < min_age_sec) {
                entry = entry->next;
                continue;
            }

            // Must have recent keepalive
            if (current_time - entry->last_keepalive > keepalive_timeout) {
                entry = entry->next;
                continue;
            }

            peers_out[found++] = entry;
            entry = entry->next;
        }
    }

#ifdef _WIN32
    LeaveCriticalSection(&registry->lock);
#else
    pthread_mutex_unlock(&registry->lock);
#endif

    return found;
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
