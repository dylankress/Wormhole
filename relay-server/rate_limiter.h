//
// rate_limiter.h
// Wormhole Relay Server - Per-IP Rate Limiting
// by Dylan Kress
//

#pragma once
#ifndef _WIN32
#include <pthread.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

// Rate limit configuration
#define MAX_PACKETS_PER_SECOND 10000
#define RATE_LIMIT_WINDOW_SECONDS 1
#define MAX_TRACKED_IPS 10000

// IP address entry (for rate limiting)
typedef struct IP_ENTRY {
    uint8_t ip_addr[16];              // IPv4 (first 4 bytes) or IPv6
    uint8_t addr_type;                // 0x04 = IPv4, 0x06 = IPv6
    
    uint32_t packet_count;            // Packets in current window
    time_t window_start;              // Start of current rate limit window
    
    struct IP_ENTRY* next;            // Hash table collision chain
} IP_ENTRY;

// Rate limiter (hash table of IP addresses)
typedef struct {
    IP_ENTRY** buckets;               // Hash table buckets
    uint32_t bucket_count;            // Number of buckets
    uint32_t tracked_ips;             // Current number of tracked IPs
    
    uint32_t packets_per_second;      // Rate limit threshold
    uint32_t window_seconds;          // Time window for rate limiting
    
#ifdef _WIN32
    CRITICAL_SECTION lock;            // Thread safety
#else
    pthread_mutex_t lock;
#endif
} RATE_LIMITER;

// Initialize rate limiter
// Returns: true on success, false on failure
bool RateLimiter_Init(RATE_LIMITER* limiter, uint32_t initial_capacity);

// Cleanup rate limiter
void RateLimiter_Cleanup(RATE_LIMITER* limiter);

// Check if IP is allowed to send packet (and update counter)
// Returns: true if allowed, false if rate limit exceeded
bool RateLimiter_CheckAndUpdate(RATE_LIMITER* limiter, const struct sockaddr* addr, socklen_t addr_len);

// Remove stale IP entries (no activity for >60 seconds)
// Returns: Number of IPs removed
uint32_t RateLimiter_RemoveStaleEntries(RATE_LIMITER* limiter, time_t current_time);

// Get statistics
void RateLimiter_GetStats(RATE_LIMITER* limiter, uint32_t* tracked_ips, uint32_t* capacity);
