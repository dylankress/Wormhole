//
// rate_limiter.c
// Wormhole Relay Server - Per-IP Rate Limiting
// by Dylan Kress
//

#include "rate_limiter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Hash IP address
static uint32_t hash_ip(const uint8_t* ip, uint8_t addr_type, uint32_t bucket_count) {
    uint32_t hash = 2166136261u;
    int len = (addr_type == 0x04) ? 4 : 16;
    
    for (int i = 0; i < len; i++) {
        hash ^= ip[i];
        hash *= 16777619u;
    }
    
    return hash & (bucket_count - 1);
}

// Compare two IP addresses
static bool ip_equals(const uint8_t* a, const uint8_t* b, uint8_t addr_type) {
    int len = (addr_type == 0x04) ? 4 : 16;
    return memcmp(a, b, len) == 0;
}

// Extract IP address from sockaddr
static void extract_ip(const struct sockaddr* addr, uint8_t* ip_out, uint8_t* addr_type_out) {
    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in* addr4 = (const struct sockaddr_in*)addr;
        memcpy(ip_out, &addr4->sin_addr, 4);
        *addr_type_out = 0x04;
    } else if (addr->sa_family == AF_INET6) {
        const struct sockaddr_in6* addr6 = (const struct sockaddr_in6*)addr;
        memcpy(ip_out, &addr6->sin6_addr, 16);
        *addr_type_out = 0x06;
    }
}

bool RateLimiter_Init(RATE_LIMITER* limiter, uint32_t initial_capacity) {
    if (!limiter || initial_capacity == 0) {
        return false;
    }
    
    // Round up to power of 2
    uint32_t capacity = 16;
    while (capacity < initial_capacity) {
        capacity *= 2;
    }
    
    limiter->buckets = (IP_ENTRY**)calloc(capacity, sizeof(IP_ENTRY*));
    if (!limiter->buckets) {
        return false;
    }
    
    limiter->bucket_count = capacity;
    limiter->tracked_ips = 0;
    limiter->packets_per_second = MAX_PACKETS_PER_SECOND;
    limiter->window_seconds = RATE_LIMIT_WINDOW_SECONDS;
    
#ifdef _WIN32
    InitializeCriticalSection(&limiter->lock);
#else
    pthread_mutex_init(&limiter->lock, NULL);
#endif
    
    printf("[RateLimiter] Initialized with %u buckets (max %u packets/sec)\n",
           capacity, MAX_PACKETS_PER_SECOND);
    return true;
}

void RateLimiter_Cleanup(RATE_LIMITER* limiter) {
    if (!limiter || !limiter->buckets) {
        return;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&limiter->lock);
#else
    pthread_mutex_lock(&limiter->lock);
#endif
    
    // Free all entries
    for (uint32_t i = 0; i < limiter->bucket_count; i++) {
        IP_ENTRY* entry = limiter->buckets[i];
        while (entry) {
            IP_ENTRY* next = entry->next;
            free(entry);
            entry = next;
        }
    }
    
    free(limiter->buckets);
    limiter->buckets = NULL;
    limiter->bucket_count = 0;
    limiter->tracked_ips = 0;
    
#ifdef _WIN32
    LeaveCriticalSection(&limiter->lock);
    DeleteCriticalSection(&limiter->lock);
#else
    pthread_mutex_unlock(&limiter->lock);
    pthread_mutex_destroy(&limiter->lock);
#endif
    
    printf("[RateLimiter] Cleaned up\n");
}

bool RateLimiter_CheckAndUpdate(RATE_LIMITER* limiter, const struct sockaddr* addr, socklen_t addr_len) {
    if (!limiter || !addr) {
        return false;
    }
    
    uint8_t ip[16];
    uint8_t addr_type;
    extract_ip(addr, ip, &addr_type);
    
#ifdef _WIN32
    EnterCriticalSection(&limiter->lock);
#else
    pthread_mutex_lock(&limiter->lock);
#endif
    
    time_t now = time(NULL);
    uint32_t bucket = hash_ip(ip, addr_type, limiter->bucket_count);
    
    // Find existing entry
    IP_ENTRY* entry = limiter->buckets[bucket];
    while (entry) {
        if (entry->addr_type == addr_type && ip_equals(entry->ip_addr, ip, addr_type)) {
            // Found existing entry
            
            // Check if we need to reset window
            if (now - entry->window_start >= limiter->window_seconds) {
                entry->window_start = now;
                entry->packet_count = 0;
            }
            
            // Check rate limit
            if (entry->packet_count >= limiter->packets_per_second) {
#ifdef _WIN32
                LeaveCriticalSection(&limiter->lock);
#else
                pthread_mutex_unlock(&limiter->lock);
#endif
                // Rate limit exceeded
                return false;
            }
            
            // Increment counter
            entry->packet_count++;
            
#ifdef _WIN32
            LeaveCriticalSection(&limiter->lock);
#else
            pthread_mutex_unlock(&limiter->lock);
#endif
            return true;
        }
        entry = entry->next;
    }
    
    // Cap table size to prevent unbounded growth
    if (limiter->tracked_ips >= MAX_TRACKED_IPS) {
        // Force cleanup of stale entries while still holding the lock
        uint32_t removed = 0;
        const time_t stale_timeout = 60;
        for (uint32_t b = 0; b < limiter->bucket_count; b++) {
            IP_ENTRY** p = &limiter->buckets[b];
            IP_ENTRY* e = limiter->buckets[b];
            while (e) {
                if (now - e->window_start > stale_timeout) {
                    *p = e->next;
                    IP_ENTRY* to_free = e;
                    e = e->next;
                    free(to_free);
                    limiter->tracked_ips--;
                    removed++;
                } else {
                    p = &e->next;
                    e = e->next;
                }
            }
        }
        // If still at capacity after cleanup, reject new entry
        if (limiter->tracked_ips >= MAX_TRACKED_IPS) {
#ifdef _WIN32
            LeaveCriticalSection(&limiter->lock);
#else
            pthread_mutex_unlock(&limiter->lock);
#endif
            return false;
        }
    }

    // Create new entry
    IP_ENTRY* new_entry = (IP_ENTRY*)malloc(sizeof(IP_ENTRY));
    if (!new_entry) {
#ifdef _WIN32
        LeaveCriticalSection(&limiter->lock);
#else
        pthread_mutex_unlock(&limiter->lock);
#endif
        return false;
    }

    memcpy(new_entry->ip_addr, ip, 16);
    new_entry->addr_type = addr_type;
    new_entry->packet_count = 1;
    new_entry->window_start = now;

    // Insert at head
    new_entry->next = limiter->buckets[bucket];
    limiter->buckets[bucket] = new_entry;
    limiter->tracked_ips++;
    
#ifdef _WIN32
    LeaveCriticalSection(&limiter->lock);
#else
    pthread_mutex_unlock(&limiter->lock);
#endif
    
    return true;
}

uint32_t RateLimiter_RemoveStaleEntries(RATE_LIMITER* limiter, time_t current_time) {
    if (!limiter) {
        return 0;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&limiter->lock);
#else
    pthread_mutex_lock(&limiter->lock);
#endif
    
    uint32_t removed = 0;
    const time_t timeout = 60;  // 60 seconds of inactivity
    
    for (uint32_t i = 0; i < limiter->bucket_count; i++) {
        IP_ENTRY** prev = &limiter->buckets[i];
        IP_ENTRY* entry = limiter->buckets[i];
        
        while (entry) {
            if (current_time - entry->window_start > timeout) {
                // Stale entry, remove it
                *prev = entry->next;
                
                IP_ENTRY* to_free = entry;
                entry = entry->next;
                free(to_free);
                
                limiter->tracked_ips--;
                removed++;
            } else {
                prev = &entry->next;
                entry = entry->next;
            }
        }
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&limiter->lock);
#else
    pthread_mutex_unlock(&limiter->lock);
#endif
    
    if (removed > 0) {
        printf("[RateLimiter] Removed %u stale IP entries (tracked: %u)\n",
               removed, limiter->tracked_ips);
    }
    
    return removed;
}

void RateLimiter_GetStats(RATE_LIMITER* limiter, uint32_t* tracked_ips, uint32_t* capacity) {
    if (!limiter) {
        return;
    }
    
#ifdef _WIN32
    EnterCriticalSection(&limiter->lock);
#else
    pthread_mutex_lock(&limiter->lock);
#endif
    
    if (tracked_ips) {
        *tracked_ips = limiter->tracked_ips;
    }
    if (capacity) {
        *capacity = limiter->bucket_count;
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&limiter->lock);
#else
    pthread_mutex_unlock(&limiter->lock);
#endif
}
